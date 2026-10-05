/* htsn_pubsub_test — proves the OPC UA FX PubSub plane end-to-end, no board.
 *
 * Two open62541 servers run in one process over localhost UDP (loopback):
 *   - "controller" (the RPi/CNC role):  htsn_ps_attach_controller
 *       publishes the command dataset, subscribes the telemetry dataset.
 *   - "device"  (a fake STM32 role):    htsn_ps_attach_device
 *       publishes the telemetry dataset, subscribes the command dataset.
 *
 * It verifies BOTH directions over the real UADP wire, including:
 *   - a mid-run value change (dynamic propagation, not just the first keyframe);
 *   - the sonar_sweep 1-D Int16 array field.
 *
 * Exits 0 if every check passes, 1 otherwise. Run:  ./htsn_pubsub_test
 */
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/types_generated.h>
#include <open62541/nodeids.h>
#include "htsn_opcua_ids.h"
#include "htsn_nodes.h"
#include "htsn_pubsub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define RPI_PUB_ADDR "opc.udp://127.0.0.1:8899/"  /* controller cmd  -> device */
#define STM_PUB_ADDR "opc.udp://127.0.0.1:8898/"  /* device   telem -> controller */
#define RPI_SUB_PORT 8898                          /* controller receives telem   */
#define STM_SUB_PORT 8899                          /* device  receives cmd        */

static int g_fail = 0;
static UA_UInt16 test_ns;   /* namespace index the two servers resolve the URI to */

static void check(const char *what, int ok, const char *detail) {
    printf("  [%s] %s %s\n", ok ? "PASS" : "FAIL", what, detail);
    if (!ok) g_fail++;
}

static void w_scalar(UA_Server *s, UA_UInt32 id, size_t t, const void *v) {
    UA_Variant var; UA_Variant_init(&var);
    UA_Variant_setScalarCopy(&var, v, &UA_TYPES[t]);
    UA_Server_writeValue(s, UA_NODEID_NUMERIC(test_ns, id), var);
    UA_Variant_clear(&var);
}

static void w_int16arr(UA_Server *s, UA_UInt32 id, const int16_t *a, size_t n) {
    UA_Variant var; UA_Variant_init(&var);
    UA_Variant_setArrayCopy(&var, (void *)a, n, &UA_TYPES[UA_TYPES_INT16]);
    UA_Server_writeValue(s, UA_NODEID_NUMERIC(test_ns, id), var);
    UA_Variant_clear(&var);
}

static int r_scalar(UA_Server *s, UA_UInt32 id, size_t t, void *out) {
    UA_Variant var; UA_Variant_init(&var);
    UA_StatusCode rv = UA_Server_readValue(s, UA_NODEID_NUMERIC(test_ns, id), &var);
    int ok = (rv == UA_STATUSCODE_GOOD && var.data && var.type == &UA_TYPES[t]);
    if (ok) memcpy(out, var.data, UA_TYPES[t].memSize);
    UA_Variant_clear(&var);
    return ok;
}

static int r_int16arr(UA_Server *s, UA_UInt32 id, int16_t *out, size_t maxn, size_t *outn) {
    UA_Variant var; UA_Variant_init(&var);
    UA_StatusCode rv = UA_Server_readValue(s, UA_NODEID_NUMERIC(test_ns, id), &var);
    int ok = (rv == UA_STATUSCODE_GOOD && var.arrayLength > 0 &&
              var.type == &UA_TYPES[UA_TYPES_INT16]);
    size_t cnt = ok ? (var.arrayLength < maxn ? var.arrayLength : maxn) : 0;
    if (ok) memcpy(out, var.data, cnt * sizeof(int16_t));
    if (outn) *outn = var.arrayLength;
    UA_Variant_clear(&var);
    return ok && cnt > 0;
}

static double now_s(struct timespec *ref) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - ref->tv_sec) + (now.tv_nsec - ref->tv_nsec) / 1e9;
}

/* A full open62541 server on a chosen opc.tcp port (default UA_Server_new() uses
 * :4840; the two test servers must not clash). TCP is unused by the test — only
 * the PubSub UDP plane matters — but the listener must still bind a free port. */
static UA_Server *make_server(UA_UInt16 port) {
    UA_ServerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));   /* buffer/bufferSize must start empty (cf. UA_Server_new) */
    UA_ServerConfig_setMinimal(&cfg, port, NULL);
    cfg.applicationDescription.applicationUri = UA_STRING("urn:htsn:stm32");  /* -> ns1, like prod */
    cfg.pubSubConfig.enableDeltaFrames = UA_FALSE;  /* fixed small datasets: all keyframes */
    return UA_Server_newWithConfig(&cfg);
}

int main(void) {
    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);

    /* ---- controller (RPi/CNC) ---- */
    UA_Server *rpi = make_server(4841);          /* opc.tcp unused here; avoid :4840 */
    test_ns = htsn_build_address_space(rpi);
    if (test_ns == 0) {
        printf("FAIL: controller address space\n"); return 1;
    }
    UA_StatusCode rv = htsn_ps_attach_controller(rpi, test_ns, RPI_PUB_ADDR, RPI_SUB_PORT, "");
    if (rv != UA_STATUSCODE_GOOD) {
        printf("FAIL: attach_controller -> %s\n", UA_StatusCode_name(rv)); return 1;
    }
    UA_Server_run_startup(rpi);
    UA_Server_enableAllPubSubComponents(rpi);

    /* ---- device (fake STM32) ---- */
    UA_Server *stm = make_server(4842);
    UA_UInt16 sns = htsn_build_address_space(stm);
    if (sns == 0) {
        printf("FAIL: device address space\n"); return 1;
    }
    if (sns != test_ns) {
        printf("FAIL: namespace mismatch (controller=%d device=%d)\n",
               (int)test_ns, (int)sns); return 1;
    }
    rv = htsn_ps_attach_device(stm, sns, STM_PUB_ADDR, STM_SUB_PORT, "");
    if (rv != UA_STATUSCODE_GOOD) {
        printf("FAIL: attach_device -> %s\n", UA_StatusCode_name(rv)); return 1;
    }
    UA_Server_run_startup(stm);
    UA_Server_enableAllPubSubComponents(stm);

    printf("pubsub test: controller + device up, running ~2.5s over loopback UDP\n");

    /* ---- initial values (should reach the peer as the first keyframes) ---- */
    {
        int16_t cs = 123;                       w_scalar(rpi, HTSN_ID_CMD_SERVO_ANGLE, UA_TYPES_INT16, &cs);
        int16_t sa = 42;                        w_scalar(stm, HTSN_ID_SERVO_ANGLE,     UA_TYPES_INT16, &sa);
        bool b1 = true;                         w_scalar(stm, HTSN_ID_BTN1,            UA_TYPES_BOOLEAN, &b1);
        bool pl = true;                         w_scalar(stm, HTSN_ID_PTP_LOCKED,      UA_TYPES_BOOLEAN, &pl);
        int16_t tv = 100;                       w_scalar(stm, HTSN_ID_TSN_APP_VLAN,    UA_TYPES_INT16, &tv);
        int32_t tf = 3;                         w_scalar(stm, HTSN_ID_TSN_FEATURES,    UA_TYPES_INT32, &tf);
        int16_t sweep[3] = { 100, 200, 300 };   w_int16arr(stm, HTSN_ID_SONAR_SWEEP, sweep, 3);
    }

    int changes_applied = 0;
    for (;;) {
        UA_Server_run_iterate(rpi, UA_FALSE);
        UA_Server_run_iterate(stm, UA_FALSE);
        double el = now_s(&t0);
        if (!changes_applied && el > 1.0) {
            bool on = true;                     w_scalar(rpi, HTSN_ID_CMD_RELAY_ON, UA_TYPES_BOOLEAN, &on);
            int16_t sa = 77;                    w_scalar(stm, HTSN_ID_SERVO_ANGLE,  UA_TYPES_INT16, &sa);
            changes_applied = 1;
            fprintf(stderr, "[test] +1.0s: mid-run change (cmd_relay=true, servo=77)\n");
        }
        if (el > 2.4) break;
        usleep(2000);
    }
    /* settle: a few more iterations to flush the last keyframes */
    for (int i = 0; i < 20; i++) { UA_Server_run_iterate(rpi, UA_FALSE); UA_Server_run_iterate(stm, UA_FALSE); usleep(5000); }

    printf("\nresults:\n");
    /* C2D — controller -> device (read the device's command nodes) */
    {
        int16_t v; bool b;
        char d[64];
        if (r_scalar(stm, HTSN_ID_CMD_SERVO_ANGLE, UA_TYPES_INT16, &v))
            snprintf(d, sizeof(d), "= %d (want 123)", (int)v);
        else snprintf(d, sizeof(d), "= <read failed>");
        check("C2D cmd_servo_angle initial", r_scalar(stm, HTSN_ID_CMD_SERVO_ANGLE, UA_TYPES_INT16, &v) && v == 123, d);

        if (r_scalar(stm, HTSN_ID_CMD_RELAY_ON, UA_TYPES_BOOLEAN, &b))
            snprintf(d, sizeof(d), "= %d (want 1)", (int)b);
        else snprintf(d, sizeof(d), "= <read failed>");
        check("C2D cmd_relay_on mid-run change", r_scalar(stm, HTSN_ID_CMD_RELAY_ON, UA_TYPES_BOOLEAN, &b) && b, d);
    }
    /* D2C — device -> controller (read the controller's telemetry nodes) */
    {
        int16_t v; bool b; int32_t i32; char d[64];
        if (r_scalar(rpi, HTSN_ID_SERVO_ANGLE, UA_TYPES_INT16, &v))
            snprintf(d, sizeof(d), "= %d (want 77)", (int)v);
        else snprintf(d, sizeof(d), "= <read failed>");
        check("D2C servo_angle mid-run change", r_scalar(rpi, HTSN_ID_SERVO_ANGLE, UA_TYPES_INT16, &v) && v == 77, d);

        if (r_scalar(rpi, HTSN_ID_BTN1, UA_TYPES_BOOLEAN, &b))
            snprintf(d, sizeof(d), "= %d (want 1)", (int)b);
        else snprintf(d, sizeof(d), "= <read failed>");
        check("D2C btn1 initial", r_scalar(rpi, HTSN_ID_BTN1, UA_TYPES_BOOLEAN, &b) && b, d);

        if (r_scalar(rpi, HTSN_ID_PTP_LOCKED, UA_TYPES_BOOLEAN, &b))
            snprintf(d, sizeof(d), "= %d (want 1)", (int)b);
        else snprintf(d, sizeof(d), "= <read failed>");
        check("D2C ptp_locked initial", r_scalar(rpi, HTSN_ID_PTP_LOCKED, UA_TYPES_BOOLEAN, &b) && b, d);

        if (r_scalar(rpi, HTSN_ID_TSN_APP_VLAN, UA_TYPES_INT16, &v))
            snprintf(d, sizeof(d), "= %d (want 100)", (int)v);
        else snprintf(d, sizeof(d), "= <read failed>");
        check("D2C tsn_app_vlan initial", r_scalar(rpi, HTSN_ID_TSN_APP_VLAN, UA_TYPES_INT16, &v) && v == 100, d);

        if (r_scalar(rpi, HTSN_ID_TSN_FEATURES, UA_TYPES_INT32, &i32))
            snprintf(d, sizeof(d), "= %d (want 3)", (int)i32);
        else snprintf(d, sizeof(d), "= <read failed>");
        check("D2C tsn_features initial", r_scalar(rpi, HTSN_ID_TSN_FEATURES, UA_TYPES_INT32, &i32) && i32 == 3, d);

        int16_t got[8]; size_t n = 0;
        int ok = r_int16arr(rpi, HTSN_ID_SONAR_SWEEP, got, 8, &n);
        int allmatch = ok && n == 3 && got[0] == 100 && got[1] == 200 && got[2] == 300;
        snprintf(d, sizeof(d), "n=%zu [100,200,300]?", (unsigned long)n);
        check("D2C sonar_sweep Int16 array", allmatch, d);
    }

    printf("\n%s (%d check%s failed)\n",
           g_fail ? "RESULT: FAIL" : "RESULT: PASS",
           g_fail, g_fail == 1 ? "" : "s");

    UA_Server_disableAllPubSubComponents(rpi); UA_Server_run_shutdown(rpi); UA_Server_delete(rpi);
    UA_Server_disableAllPubSubComponents(stm); UA_Server_run_shutdown(stm); UA_Server_delete(stm);
    return g_fail ? 1 : 0;
}
