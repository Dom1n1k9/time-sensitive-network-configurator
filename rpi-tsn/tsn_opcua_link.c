/* tsn_opcua_link — persistent OPC UA poller (the wired RPi<->STM32 read path).
 *
 * Holds ONE long-lived client connection to the CNC server and, every ~1 s,
 * reads all telemetry and atomically writes it to a JSON file. The GUI reads
 * that file (no OPC UA dependency, no connect churn). Commands are rare and
 * user-triggered, so they go through the one-shot `htsn_opcua_cli write`.
 *
 *   HTSN_OPCUA_URL   server url        (default opc.tcp://127.0.0.1:4840)
 *   HTSN_OPCUA_OUT   output json path  (default /tmp/htsn_tsn_opcua.json)
 *   HTSN_OPCUA_HZ    poll rate ~Hz     (default 1)
 */
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/types.h>
#include <open62541/types_generated.h>
#include "htsn_opcua_ids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    const char *name;
    UA_UInt32   id;
    int         type;
    int         is_array;
} NodeDef;

static const NodeDef NODES[] = {
    { "sonar_sweep",    HTSN_ID_SONAR_SWEEP,    UA_TYPES_INT16,   1 },
    { "sonar_sweep_id", HTSN_ID_SONAR_SWEEP_ID, UA_TYPES_INT32,   0 },
    { "servo_angle",    HTSN_ID_SERVO_ANGLE,    UA_TYPES_INT16,   0 },
    { "relay_on",       HTSN_ID_RELAY_ON,       UA_TYPES_BOOLEAN, 0 },
    { "buzzer_hz",      HTSN_ID_BUZZER_HZ,      UA_TYPES_UINT16,  0 },
    { "buzzer_ms",      HTSN_ID_BUZZER_MS,      UA_TYPES_UINT16,  0 },
    { "btn1",           HTSN_ID_BTN1,           UA_TYPES_BOOLEAN, 0 },
    { "btn2",           HTSN_ID_BTN2,           UA_TYPES_BOOLEAN, 0 },
    { "btn3",           HTSN_ID_BTN3,           UA_TYPES_BOOLEAN, 0 },
    { "btn4",           HTSN_ID_BTN4,           UA_TYPES_BOOLEAN, 0 },
    { "ptp_offset_ns",  HTSN_ID_PTP_OFFSET_NS,  UA_TYPES_INT64,   0 },
    { "ptp_state",      HTSN_ID_PTP_STATE,      UA_TYPES_BYTE,    0 },
    { "ptp_locked",     HTSN_ID_PTP_LOCKED,     UA_TYPES_BOOLEAN, 0 },
    { "last_seen",      HTSN_ID_LAST_SEEN,      UA_TYPES_INT64,   0 },
};
static const size_t NN = sizeof(NODES) / sizeof(NODES[0]);

static volatile UA_Boolean running = UA_TRUE;
static void on_signal(int s) { (void)s; running = UA_FALSE; }

static const char *env_or(const char *k, const char *d) {
    const char *v = getenv(k);
    return (v && *v) ? v : d;
}

static void emit_value(const NodeDef *nd, const UA_Variant *v, char *buf, size_t cap) {
    if (nd->is_array) {
        size_t off = 0;
        const UA_Int16 *a = (const UA_Int16 *)v->data;
        size_t n = v->arrayLength;
        off += (size_t)snprintf(buf + off, cap - off, "\"%s\":[", nd->name);
        for (size_t i = 0; i < n && off + 1 < cap; i++)
            off += (size_t)snprintf(buf + off, cap - off, "%s%d", i ? "," : "", (int)a[i]);
        snprintf(buf + off, cap - off, "]");
        return;
    }
    if (!UA_Variant_isScalar(v) || !v->data) { snprintf(buf, cap, "\"%s\":null", nd->name); return; }
    switch (nd->type) {
    case UA_TYPES_BOOLEAN: snprintf(buf, cap, "\"%s\":%s", nd->name, *(const UA_Boolean *)v->data ? "true" : "false"); break;
    case UA_TYPES_INT16:   snprintf(buf, cap, "\"%s\":%d", nd->name, (int)*(const UA_Int16 *)v->data); break;
    case UA_TYPES_INT32:   snprintf(buf, cap, "\"%s\":%d", nd->name, (int)*(const UA_Int32 *)v->data); break;
    case UA_TYPES_INT64:   snprintf(buf, cap, "\"%s\":%lld", nd->name, (long long)*(const UA_Int64 *)v->data); break;
    case UA_TYPES_UINT16:  snprintf(buf, cap, "\"%s\":%u", nd->name, (unsigned)*(const UA_UInt16 *)v->data); break;
    case UA_TYPES_BYTE:    snprintf(buf, cap, "\"%s\":%u", nd->name, (unsigned)*(const UA_Byte *)v->data); break;
    default:               snprintf(buf, cap, "\"%s\":null", nd->name); break;
    }
}

/* Returns the number of nodes read successfully (0 during the server's
 * post-startup warmup, when every read returns BadNodeIdUnknown). */
static int read_all(UA_Client *c, char *json, size_t cap) {
    size_t off = 0;
    int okc = 0;
    int dbg = getenv("HTSN_OPCUA_DEBUG") != NULL;
    off += (size_t)snprintf(json + off, cap - off, "{\"ok\":true,\"ts\":%ld,", (long)time(NULL));
    for (size_t i = 0; i < NN; i++) {
        UA_Variant v;
        UA_Variant_init(&v);
        UA_StatusCode rv = UA_Client_readValueAttribute(c,
            UA_NODEID_NUMERIC(HTSN_OPCUA_NS, NODES[i].id), &v);
        char field[256];
        if (rv == UA_STATUSCODE_GOOD) {
            emit_value(&NODES[i], &v, field, sizeof(field));
            okc++;
        } else {
            snprintf(field, sizeof(field), "\"%s\":null", NODES[i].name);
        }
        if (dbg)
            fprintf(stderr, "  DBG %-16s rv=%s field=[%s]\n",
                    NODES[i].name, UA_StatusCode_name(rv), field);
        UA_Variant_clear(&v);
        off += (size_t)snprintf(json + off, cap - off, "%s%s", i ? "," : "", field);
    }
    snprintf(json + off, cap - off, "}\n");
    return okc;
}

static void atomic_write(const char *path, const char *data) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fputs(data, f);
    fclose(f);
    rename(tmp, path);
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    const char *url = env_or("HTSN_OPCUA_URL", "opc.tcp://127.0.0.1:4840");
    const char *out = env_or("HTSN_OPCUA_OUT", "/tmp/htsn_tsn_opcua.json");
    long hz = atol(env_or("HTSN_OPCUA_HZ", "1"));
    if (hz < 1) hz = 1;
    if (hz > 20) hz = 20;
    long sleep_us = 1000000L / hz;

    UA_Client *c = NULL;
    char json[8192];
    while (running) {
        if (!c) {
            c = UA_Client_new();
            if (UA_Client_connect(c, url) != UA_STATUSCODE_GOOD) {
                fprintf(stderr, "tsn_opcua_link: not connected to %s (retrying)\n", url);
                UA_Client_delete(c);
                c = NULL;
                for (long i = 0; i < 2000000L && running; i += 100000L)
                    usleep(100000L);
                continue;
            }
            fprintf(stderr, "tsn_opcua_link: connected to %s\n", url);
        }
        int okc = read_all(c, json, sizeof(json));
        if (okc > 0) {
            atomic_write(out, json);   /* keep the last-good file during warmup */
            long interval = sleep_us;
            long waited = 0;
            while (running && waited < interval) {
                UA_Client_run_iterate(c, 50);
                long chunk = 50000L < (interval - waited) ? 50000L : (interval - waited);
                usleep((useconds_t)chunk);
                waited += chunk;
            }
        } else {
            /* All reads failed: the server is still warming up OR this early
             * session is poisoned. Drop it and reconnect; a fresh session made
             * once the server is warm reads fine. */
            UA_Client_disconnect(c);
            UA_Client_delete(c);
            c = NULL;
            for (long i = 0; i < 1000000L && running; i += 100000L)
                usleep(100000L);
        }
    }
    if (c) { UA_Client_disconnect(c); UA_Client_delete(c); }
    return 0;
}
