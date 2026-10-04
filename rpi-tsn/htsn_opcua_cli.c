/* htsn_opcua_cli — tiny open62541 client the GUI uses to reach the CNC server.
 *
 * The GUI (Python) has no OPC UA dependency, so it shells out to this helper:
 *
 *   htsn_opcua_cli read              -> JSON object of all telemetry to stdout
 *   htsn_opcua_cli write <name> <v>  -> write a command node, print "ok"/"err"
 *
 * URL (default opc.tcp://127.0.0.1:4840) via HTSN_OPCUA_URL. Node names/ids are
 * shared with the server via htsn_opcua_ids.h.
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
#include <unistd.h>

typedef struct {
    const char *name;
    UA_UInt32   id;
    int         type;      /* index into UA_TYPES[] */
    int         is_array;  /* Int16[] (sonar sweep) */
    int         is_cmd;    /* command node (writable by the GUI) */
} NodeDef;

static const NodeDef NODES[] = {
    { "sonar_sweep",       HTSN_ID_SONAR_SWEEP,     UA_TYPES_INT16,   1, 0 },
    { "sonar_sweep_id",    HTSN_ID_SONAR_SWEEP_ID,  UA_TYPES_INT32,   0, 0 },
    { "servo_angle",       HTSN_ID_SERVO_ANGLE,     UA_TYPES_INT16,   0, 0 },
    { "relay_on",          HTSN_ID_RELAY_ON,        UA_TYPES_BOOLEAN, 0, 0 },
    { "buzzer_hz",         HTSN_ID_BUZZER_HZ,       UA_TYPES_UINT16,  0, 0 },
    { "buzzer_ms",         HTSN_ID_BUZZER_MS,       UA_TYPES_UINT16,  0, 0 },
    { "btn1",              HTSN_ID_BTN1,            UA_TYPES_BOOLEAN, 0, 0 },
    { "btn2",              HTSN_ID_BTN2,            UA_TYPES_BOOLEAN, 0, 0 },
    { "btn3",              HTSN_ID_BTN3,            UA_TYPES_BOOLEAN, 0, 0 },
    { "btn4",              HTSN_ID_BTN4,            UA_TYPES_BOOLEAN, 0, 0 },
    { "ptp_offset_ns",     HTSN_ID_PTP_OFFSET_NS,   UA_TYPES_INT64,   0, 0 },
    { "ptp_state",         HTSN_ID_PTP_STATE,       UA_TYPES_BYTE,    0, 0 },
    { "ptp_locked",        HTSN_ID_PTP_LOCKED,      UA_TYPES_BOOLEAN, 0, 0 },
    { "last_seen",         HTSN_ID_LAST_SEEN,       UA_TYPES_INT64,   0, 0 },
    { "cmd_servo_angle",   HTSN_ID_CMD_SERVO_ANGLE, UA_TYPES_INT16,   0, 1 },
    { "cmd_relay_on",      HTSN_ID_CMD_RELAY_ON,    UA_TYPES_BOOLEAN, 0, 1 },
    { "cmd_beep_ms",       HTSN_ID_CMD_BEEP_MS,     UA_TYPES_INT16,   0, 1 },
    { "cmd_sonar_trigger", HTSN_ID_CMD_SONAR_TRIG,  UA_TYPES_BOOLEAN, 0, 1 },
    { "cmd_reboot",        HTSN_ID_CMD_REBOOT,      UA_TYPES_BOOLEAN, 0, 1 },
};
static const size_t NN = sizeof(NODES) / sizeof(NODES[0]);

static const char *url(void) {
    const char *u = getenv("HTSN_OPCUA_URL");
    return (u && *u) ? u : "opc.tcp://127.0.0.1:4840";
}

static UA_Client *connect(void) {
    UA_Client *c = UA_Client_new();
    if (UA_Client_connect(c, url()) != UA_STATUSCODE_GOOD) {
        UA_Client_delete(c);
        return NULL;
    }
    return c;
}

static void emit_value(const NodeDef *nd, const UA_Variant *v) {
    if (nd->is_array) {
        const UA_Int16 *a = (const UA_Int16 *)v->data;
        size_t n = v->arrayLength;
        printf("\"%s\":[", nd->name);
        for (size_t i = 0; i < n; i++)
            printf("%s%d", i ? "," : "", (int)a[i]);
        printf("]");
        return;
    }
    if (!UA_Variant_isScalar(v) || !v->data) {
        printf("\"%s\":null", nd->name);
        return;
    }
    switch (nd->type) {
    case UA_TYPES_BOOLEAN: printf("\"%s\":%s", nd->name, *(const UA_Boolean *)v->data ? "true" : "false"); break;
    case UA_TYPES_INT16:   printf("\"%s\":%d", nd->name, (int)*(const UA_Int16 *)v->data); break;
    case UA_TYPES_INT32:   printf("\"%s\":%d", nd->name, (int)*(const UA_Int32 *)v->data); break;
    case UA_TYPES_INT64:   printf("\"%s\":%lld", nd->name, (long long)*(const UA_Int64 *)v->data); break;
    case UA_TYPES_UINT16:  printf("\"%s\":%u", nd->name, (unsigned)*(const UA_UInt16 *)v->data); break;
    case UA_TYPES_BYTE:    printf("\"%s\":%u", nd->name, (unsigned)*(const UA_Byte *)v->data); break;
    default:               printf("\"%s\":null", nd->name); break;
    }
}

static int cmd_read(void) {
    UA_Client *c = connect();
    if (!c) { printf("{\"ok\":false,\"err\":\"connect\"}\n"); return 1; }
    printf("{\"ok\":true,");
    int first = 1;
    for (size_t i = 0; i < NN; i++) {
        if (NODES[i].is_cmd) continue;
        UA_Variant v;
        UA_Variant_init(&v);
        UA_StatusCode rv = UA_Client_readValueAttribute(c,
            UA_NODEID_NUMERIC(HTSN_OPCUA_NS, NODES[i].id), &v);
        if (rv != UA_STATUSCODE_GOOD) {
            fprintf(stderr, "  %s read: %s\n", NODES[i].name, UA_StatusCode_name(rv));
            printf("%s\"%s\":null", first ? "" : ",", NODES[i].name);
        } else if (!UA_Variant_isScalar(&v) && !v.arrayLength && !v.data) {
            fprintf(stderr, "  %s read: GOOD but empty variant\n", NODES[i].name);
            printf("%s\"%s\":null", first ? "" : ",", NODES[i].name);
        } else {
            printf("%s", first ? "" : ",");
            emit_value(&NODES[i], &v);
        }
        UA_Variant_clear(&v);
        first = 0;
    }
    printf("}\n");
    UA_Client_disconnect(c);
    UA_Client_delete(c);
    return 0;
}

static int parse_bool(const char *s) {
    return (s[0] == '1' || s[0] == 't' || s[0] == 'T' ||
            s[0] == 'y' || s[0] == 'Y' || s[0] == 'o' || s[0] == 'O') ? 1 : 0;
}

static int cmd_write(const char *name, const char *val) {
    const NodeDef *nd = NULL;
    for (size_t i = 0; i < NN; i++)
        if (strcmp(NODES[i].name, name) == 0) { nd = &NODES[i]; break; }
    if (!nd) { printf("err unknown node '%s'\n", name); return 1; }

    /* A fresh session per attempt: an early/poisoned session can fail a write,
     * so retry with a new connection (the server is normally warm by now). */
    for (int attempt = 0; attempt < 4; attempt++) {
        UA_Client *c = connect();
        if (!c) { usleep(200000); continue; }
        UA_StatusCode rv;
        union { UA_Int16 i16; UA_Int32 i32; UA_Int64 i64; UA_UInt16 u16; UA_Byte b; UA_Boolean bl; } u;
        switch (nd->type) {
        case UA_TYPES_INT16:  u.i16 = (UA_Int16)atoi(val);  rv = UA_Client_writeValueAttribute_scalar(c, UA_NODEID_NUMERIC(HTSN_OPCUA_NS, nd->id), &u.i16, &UA_TYPES[UA_TYPES_INT16]); break;
        case UA_TYPES_INT32:  u.i32 = (UA_Int32)atoi(val);  rv = UA_Client_writeValueAttribute_scalar(c, UA_NODEID_NUMERIC(HTSN_OPCUA_NS, nd->id), &u.i32, &UA_TYPES[UA_TYPES_INT32]); break;
        case UA_TYPES_INT64:  u.i64 = (UA_Int64)atoll(val); rv = UA_Client_writeValueAttribute_scalar(c, UA_NODEID_NUMERIC(HTSN_OPCUA_NS, nd->id), &u.i64, &UA_TYPES[UA_TYPES_INT64]); break;
        case UA_TYPES_UINT16: u.u16 = (UA_UInt16)atoi(val); rv = UA_Client_writeValueAttribute_scalar(c, UA_NODEID_NUMERIC(HTSN_OPCUA_NS, nd->id), &u.u16, &UA_TYPES[UA_TYPES_UINT16]); break;
        case UA_TYPES_BYTE:   u.b   = (UA_Byte)atoi(val);   rv = UA_Client_writeValueAttribute_scalar(c, UA_NODEID_NUMERIC(HTSN_OPCUA_NS, nd->id), &u.b,   &UA_TYPES[UA_TYPES_BYTE]); break;
        case UA_TYPES_BOOLEAN:u.bl  = parse_bool(val) ? UA_TRUE : UA_FALSE; rv = UA_Client_writeValueAttribute_scalar(c, UA_NODEID_NUMERIC(HTSN_OPCUA_NS, nd->id), &u.bl, &UA_TYPES[UA_TYPES_BOOLEAN]); break;
        default: printf("err bad type\n"); UA_Client_delete(c); return 1;
        }
        UA_Client_disconnect(c);
        UA_Client_delete(c);
        if (rv == UA_STATUSCODE_GOOD) { printf("ok\n"); return 0; }
        usleep(200000);
    }
    printf("err write (retries exhausted)\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "read") == 0)
        return cmd_read();
    if (argc >= 4 && strcmp(argv[1], "write") == 0)
        return cmd_write(argv[2], argv[3]);
    fprintf(stderr, "usage: htsn_opcua_cli read | htsn_opcua_cli write <name> <value>\n");
    return 2;
}
