/* htsn_nodes — implementation of the htsn_tsn OPC UA address space.
 *
 * Extracted verbatim from cnc_opcua.c so the identical node model can be built
 * on more than one server instance (the production server + the PubSub test).
 * The helpers take a small context so two servers can coexist in one process.
 */
#include "htsn_nodes.h"
#include "htsn_opcua_ids.h"
#include <open62541/types_generated.h>
#include <open62541/nodeids.h>
#include <string.h>

typedef struct {
    UA_Server *s;
    UA_NodeId obj;
    UA_UInt16 ns;
} nsctx;

static UA_StatusCode add_scalar(nsctx *c, UA_UInt32 id, const char *name,
                                size_t typeIdx, const void *val) {
    UA_VariableAttributes attr;
    UA_VariableAttributes_init(&attr);
    attr.description = UA_LOCALIZEDTEXT((char *)"en-US", (char *)name);
    attr.displayName = UA_LOCALIZEDTEXT((char *)"en-US", (char *)name);
    attr.dataType = UA_TYPES[typeIdx].typeId;
    attr.valueRank = -1;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    UA_Variant_setScalarCopy(&attr.value, val, &UA_TYPES[typeIdx]);
    return UA_Server_addVariableNode(c->s, UA_NODEID_NUMERIC(c->ns, id), c->obj,
        UA_NS0ID(HASCOMPONENT), UA_QUALIFIEDNAME(c->ns, (char *)name),
        UA_NS0ID(BASEDATAVARIABLETYPE), attr, NULL, NULL);
}

static UA_StatusCode add_sweep(nsctx *c, UA_UInt32 id, const char *name) {
    UA_VariableAttributes attr;
    UA_VariableAttributes_init(&attr);
    attr.description = UA_LOCALIZEDTEXT((char *)"en-US", (char *)name);
    attr.displayName = UA_LOCALIZEDTEXT((char *)"en-US", (char *)name);
    attr.dataType = UA_TYPES[UA_TYPES_INT16].typeId;
    attr.valueRank = 1;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    UA_UInt32 dims[1] = { 0 };
    attr.arrayDimensionsSize = 1;
    attr.arrayDimensions = dims;
    UA_Variant_setArrayCopy(&attr.value, (const void *)NULL, 0, &UA_TYPES[UA_TYPES_INT16]);
    return UA_Server_addVariableNode(c->s, UA_NODEID_NUMERIC(c->ns, id), c->obj,
        UA_NS0ID(HASCOMPONENT), UA_QUALIFIEDNAME(c->ns, (char *)name),
        UA_NS0ID(BASEDATAVARIABLETYPE), attr, NULL, NULL);
}

static UA_StatusCode add_string(nsctx *c, UA_UInt32 id, const char *name) {
    UA_VariableAttributes attr;
    UA_VariableAttributes_init(&attr);
    attr.description = UA_LOCALIZEDTEXT((char *)"en-US", (char *)name);
    attr.displayName = UA_LOCALIZEDTEXT((char *)"en-US", (char *)name);
    attr.dataType = UA_TYPES[UA_TYPES_STRING].typeId;
    attr.valueRank = -1;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    const UA_String s = UA_STRING_NULL;
    UA_Variant_setScalarCopy(&attr.value, &s, &UA_TYPES[UA_TYPES_STRING]);
    return UA_Server_addVariableNode(c->s, UA_NODEID_NUMERIC(c->ns, id), c->obj,
        UA_NS0ID(HASCOMPONENT), UA_QUALIFIEDNAME(c->ns, (char *)name),
        UA_NS0ID(BASEDATAVARIABLETYPE), attr, NULL, NULL);
}

UA_UInt16 htsn_build_address_space(UA_Server *server) {
    nsctx c; c.s = server; c.ns = 0;
    c.ns = UA_Server_addNamespace(server, "urn:htsn:stm32");
    if (c.ns == 0) return 0;

    UA_ObjectAttributes oattr;
    UA_ObjectAttributes_init(&oattr);
    oattr.description = UA_LOCALIZEDTEXT((char *)"en-US", (char *)"htsn_tsn endpoint");
    oattr.displayName = UA_LOCALIZEDTEXT((char *)"en-US", (char *)"stm32-tsn-01");
    UA_StatusCode res = UA_Server_addObjectNode(server,
        UA_NODEID_NUMERIC(c.ns, HTSN_ID_OBJ),
        UA_NS0ID(OBJECTSFOLDER), UA_NS0ID(HASCOMPONENT),
        UA_QUALIFIEDNAME(c.ns, (char *)"stm32-tsn-01"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), oattr, NULL, &c.obj);
    if (res != UA_STATUSCODE_GOOD) return 0;

    int16_t i16 = 90; int32_t i32 = 0; int64_t i64 = 0;
    uint16_t u16 = 0; uint8_t u8 = 2; bool b = false;

    add_sweep(&c, HTSN_ID_SONAR_SWEEP, "sonar_sweep");
    add_scalar(&c, HTSN_ID_SONAR_SWEEP_ID, "sonar_sweep_id", UA_TYPES_INT32,  &i32);
    add_scalar(&c, HTSN_ID_SERVO_ANGLE,    "servo_angle",    UA_TYPES_INT16,   &i16);
    add_scalar(&c, HTSN_ID_RELAY_ON,       "relay_on",       UA_TYPES_BOOLEAN, &b);
    add_scalar(&c, HTSN_ID_BUZZER_HZ,      "buzzer_hz",      UA_TYPES_UINT16,  &u16);
    add_scalar(&c, HTSN_ID_BUZZER_MS,      "buzzer_ms",      UA_TYPES_UINT16,  &u16);
    add_scalar(&c, HTSN_ID_BTN1, "btn1", UA_TYPES_BOOLEAN, &b);
    add_scalar(&c, HTSN_ID_BTN2, "btn2", UA_TYPES_BOOLEAN, &b);
    add_scalar(&c, HTSN_ID_BTN3, "btn3", UA_TYPES_BOOLEAN, &b);
    add_scalar(&c, HTSN_ID_BTN4, "btn4", UA_TYPES_BOOLEAN, &b);
    add_scalar(&c, HTSN_ID_PTP_OFFSET_NS, "ptp_offset_ns",  UA_TYPES_INT64,    &i64);
    add_scalar(&c, HTSN_ID_PTP_STATE,     "ptp_state",      UA_TYPES_BYTE,     &u8);
    add_scalar(&c, HTSN_ID_PTP_LOCKED,    "ptp_locked",     UA_TYPES_BOOLEAN,  &b);
    add_scalar(&c, HTSN_ID_LAST_SEEN,     "last_seen",      UA_TYPES_INT64,    &i64);

    i16 = 0; u8 = 0;
    add_scalar(&c, HTSN_ID_TSN_APP_VLAN,    "tsn_app_vlan",    UA_TYPES_INT16, &i16);
    add_scalar(&c, HTSN_ID_TSN_APP_PRIO,    "tsn_app_prio",    UA_TYPES_BYTE,  &u8);
    add_scalar(&c, HTSN_ID_TSN_APP_PREEMPT, "tsn_app_preempt", UA_TYPES_BYTE,  &u8);
    add_scalar(&c, HTSN_ID_TSN_APP_TIMESYNC,"tsn_app_timesync",UA_TYPES_BYTE,  &u8);
    add_scalar(&c, HTSN_ID_TSN_APP_STROLE,  "tsn_app_strole",  UA_TYPES_BYTE,  &u8);
    add_scalar(&c, HTSN_ID_TSN_APP_STVLAN,  "tsn_app_stvlan",  UA_TYPES_INT16, &i16);
    add_scalar(&c, HTSN_ID_TSN_APP_TASCYC,  "tsn_app_tascyc",  UA_TYPES_INT64, &i64);
    add_scalar(&c, HTSN_ID_TSN_FEATURES,    "tsn_features",    UA_TYPES_INT32, &i32);

    i16 = 90;
    add_scalar(&c, HTSN_ID_CMD_SERVO_ANGLE, "cmd_servo_angle",   UA_TYPES_INT16,   &i16);
    add_scalar(&c, HTSN_ID_CMD_RELAY_ON,    "cmd_relay_on",      UA_TYPES_BOOLEAN, &b);
    i16 = 0;
    add_scalar(&c, HTSN_ID_CMD_BEEP_MS,     "cmd_beep_ms",       UA_TYPES_INT16,   &i16);
    add_scalar(&c, HTSN_ID_CMD_SONAR_TRIG,  "cmd_sonar_trig",    UA_TYPES_BOOLEAN, &b);
    add_scalar(&c, HTSN_ID_CMD_REBOOT,      "cmd_reboot",        UA_TYPES_BOOLEAN, &b);
    add_string(&c, HTSN_ID_CMD_TSN_CONFIG,  "cmd_tsn_config");
    return c.ns;
}
