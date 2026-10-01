/* wtsn_tsn CNC OPC UA server — runs on the RPi (the CNC).
 *
 * The wired RPi<->STM32 link is **OPC UA only** (opc.tcp://). This process hosts
 * the open62541 server that the STM32 endpoint connects to as a client:
 *   - the STM32 (client) WRITES its telemetry variables;
 *   - the STM32 (client) MONITORS the command variables (cmd_*);
 *   - the GUI / SCADA (client, also on the RPi) reads telemetry and writes
 *     command_* to drive the endpoint.
 *
 * The ESP32 (wireless) side is separate and uses MQTT directly — nothing on
 * that path touches this OPC UA server. NodeIds are defined in wtsn_opcua_ids.h.
 */
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/types_generated.h>
#include <open62541/nodeids.h>
#include "wtsn_opcua_ids.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>

static UA_Server *server;
static volatile UA_Boolean running = UA_TRUE;
static UA_UInt16 ns = WTSN_OPCUA_NS;
static UA_NodeId obj;

static void on_signal(int s) { (void)s; running = UA_FALSE; }

static UA_StatusCode add_scalar(UA_UInt32 id, const char *name, size_t typeIdx,
                                const void *val) {
    UA_VariableAttributes attr;
    UA_VariableAttributes_init(&attr);
    attr.description = UA_LOCALIZEDTEXT((char *)"en-US", (char *)name);
    attr.displayName = UA_LOCALIZEDTEXT((char *)"en-US", (char *)name);
    attr.dataType = UA_TYPES[typeIdx].typeId;
    attr.valueRank = -1;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    UA_Variant_setScalarCopy(&attr.value, val, &UA_TYPES[typeIdx]);
    return UA_Server_addVariableNode(server, UA_NODEID_NUMERIC(WTSN_OPCUA_NS, id), obj,
        UA_NS0ID(HASCOMPONENT), UA_QUALIFIEDNAME(ns, (char *)name),
        UA_NS0ID(BASEDATAVARIABLETYPE), attr, NULL, NULL);
}

static UA_StatusCode add_sweep(UA_UInt32 id, const char *name) {
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
    return UA_Server_addVariableNode(server, UA_NODEID_NUMERIC(WTSN_OPCUA_NS, id), obj,
        UA_NS0ID(HASCOMPONENT), UA_QUALIFIEDNAME(ns, (char *)name),
        UA_NS0ID(BASEDATAVARIABLETYPE), attr, NULL, NULL);
}

static UA_StatusCode build_address_space(void) {
    ns = UA_Server_addNamespace(server, "urn:wtsn:stm32");

    UA_ObjectAttributes oattr;
    UA_ObjectAttributes_init(&oattr);
    oattr.description = UA_LOCALIZEDTEXT((char *)"en-US", (char *)"wtsn_tsn endpoint");
    oattr.displayName = UA_LOCALIZEDTEXT((char *)"en-US", (char *)"stm32-tsn-01");
    UA_StatusCode res = UA_Server_addObjectNode(server,
        UA_NODEID_NUMERIC(WTSN_OPCUA_NS, WTSN_ID_OBJ),
        UA_NS0ID(OBJECTSFOLDER), UA_NS0ID(HASCOMPONENT),
        UA_QUALIFIEDNAME(ns, (char *)"stm32-tsn-01"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), oattr, NULL, &obj);
    if (res != UA_STATUSCODE_GOOD) return res;

    UA_NodeId tmp;
    int16_t i16 = 90; int32_t i32 = 0; int64_t i64 = 0;
    uint16_t u16 = 0; uint8_t u8 = 2; bool b = false;
    (void)tmp;

    add_sweep(WTSN_ID_SONAR_SWEEP, "sonar_sweep");
    add_scalar(WTSN_ID_SONAR_SWEEP_ID, "sonar_sweep_id", UA_TYPES_INT32,  &i32);
    add_scalar(WTSN_ID_SERVO_ANGLE,    "servo_angle",    UA_TYPES_INT16,   &i16);
    add_scalar(WTSN_ID_RELAY_ON,       "relay_on",       UA_TYPES_BOOLEAN, &b);
    add_scalar(WTSN_ID_BUZZER_HZ,      "buzzer_hz",      UA_TYPES_UINT16,  &u16);
    add_scalar(WTSN_ID_BUZZER_MS,      "buzzer_ms",      UA_TYPES_UINT16,  &u16);
    add_scalar(WTSN_ID_BTN1, "btn1", UA_TYPES_BOOLEAN, &b);
    add_scalar(WTSN_ID_BTN2, "btn2", UA_TYPES_BOOLEAN, &b);
    add_scalar(WTSN_ID_BTN3, "btn3", UA_TYPES_BOOLEAN, &b);
    add_scalar(WTSN_ID_BTN4, "btn4", UA_TYPES_BOOLEAN, &b);
    add_scalar(WTSN_ID_PTP_OFFSET_NS, "ptp_offset_ns",  UA_TYPES_INT64,    &i64);
    add_scalar(WTSN_ID_PTP_STATE,     "ptp_state",      UA_TYPES_BYTE,     &u8);
    add_scalar(WTSN_ID_PTP_LOCKED,    "ptp_locked",     UA_TYPES_BOOLEAN,  &b);
    add_scalar(WTSN_ID_LAST_SEEN,     "last_seen",      UA_TYPES_INT64,    &i64);

    i16 = 90;
    add_scalar(WTSN_ID_CMD_SERVO_ANGLE, "cmd_servo_angle",   UA_TYPES_INT16,   &i16);
    add_scalar(WTSN_ID_CMD_RELAY_ON,    "cmd_relay_on",      UA_TYPES_BOOLEAN, &b);
    i16 = 0;
    add_scalar(WTSN_ID_CMD_BEEP_MS,     "cmd_beep_ms",       UA_TYPES_INT16,   &i16);
    add_scalar(WTSN_ID_CMD_SONAR_TRIG,  "cmd_sonar_trigger", UA_TYPES_BOOLEAN, &b);
    add_scalar(WTSN_ID_CMD_REBOOT,      "cmd_reboot",        UA_TYPES_BOOLEAN, &b);
    return UA_STATUSCODE_GOOD;
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    server = UA_Server_new();                       /* default config: opc.tcp :4840 */

    if (build_address_space() != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "failed to build address space\n");
        return 1;
    }

    UA_Server_run_startup(server);
    {
        UA_Variant sv;
        UA_Variant_init(&sv);
        UA_StatusCode s = UA_Server_readValue(server,
            UA_NODEID_NUMERIC(WTSN_OPCUA_NS, WTSN_ID_SERVO_ANGLE), &sv);
        printf("SELFTEST servo_angle: %s scalar=%d data=%p val=%d\n",
               UA_StatusCode_name(s), (int)UA_Variant_isScalar(&sv), sv.data,
               (UA_Variant_isScalar(&sv) && sv.data) ? (int)*(UA_Int16 *)sv.data : -1);
        UA_Variant_clear(&sv);
    }
    printf("wtsn_tsn OPC UA server on opc.tcp://0.0.0.0:4840 (ns %d = urn:wtsn:stm32)\n",
           (int)WTSN_OPCUA_NS);
    while (running)
        UA_Server_run_iterate(server, UA_TRUE);
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    return 0;
}
