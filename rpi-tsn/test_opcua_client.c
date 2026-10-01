/* test_opcua_client.c — verify cnc_opcua with a real open62541 client.
 * Connects to opc.tcp://127.0.0.1:4840, reads servo_angle (90), writes 45,
 * reads it back, writes+reads an Int16 sweep array. Run the server first:
 *   ./cnc_opcua &      ./test_opcua_client
 */
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/types.h>
#include <open62541/types_generated.h>
#include "wtsn_opcua_ids.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    UA_Client *c = UA_Client_new();
    UA_StatusCode rv = UA_Client_connect(c, "opc.tcp://127.0.0.1:4840");
    if (rv != UA_STATUSCODE_GOOD) {
        printf("connect FAIL: %s\n", UA_StatusCode_name(rv));
        return 1;
    }
    printf("connected to opc.tcp://127.0.0.1:4840\n");
    int ok = 1;

    /* read servo_angle (expect initial 90) */
    UA_Variant v; UA_Variant_init(&v);
    rv = UA_Client_readValueAttribute(c,
        UA_NODEID_NUMERIC(WTSN_OPCUA_NS, WTSN_ID_SERVO_ANGLE), &v);
    int16_t servo = -1;
    if (rv == UA_STATUSCODE_GOOD && UA_Variant_isScalar(&v))
        servo = *(UA_Int16 *)v.data;
    printf("read servo_angle = %d (expect 90)\n", (int)servo);
    ok &= (servo == 90);
    UA_Variant_clear(&v);

    /* write servo_angle = 45, read back */
    UA_Int16 ang = 45;
    rv = UA_Client_writeValueAttribute_scalar(c,
        UA_NODEID_NUMERIC(WTSN_OPCUA_NS, WTSN_ID_SERVO_ANGLE), &ang,
        &UA_TYPES[UA_TYPES_INT16]);
    printf("write servo_angle=45 -> %s\n", UA_StatusCode_name(rv));
    ok &= (rv == UA_STATUSCODE_GOOD);

    UA_Variant_init(&v);
    UA_Client_readValueAttribute(c,
        UA_NODEID_NUMERIC(WTSN_OPCUA_NS, WTSN_ID_SERVO_ANGLE), &v);
    if (UA_Variant_isScalar(&v)) servo = *(UA_Int16 *)v.data;
    printf("read back servo_angle = %d (expect 45)\n", (int)servo);
    ok &= (servo == 45);
    UA_Variant_clear(&v);

    /* write + read an Int16 sweep array */
    UA_Int16 sweep[8] = { 0, 10, 20, 30, 40, 50, 60, 70 };
    UA_Variant sv; UA_Variant_init(&sv);
    UA_Variant_setArrayCopy(&sv, sweep, 8, &UA_TYPES[UA_TYPES_INT16]);
    rv = UA_Client_writeValueAttribute(c,
        UA_NODEID_NUMERIC(WTSN_OPCUA_NS, WTSN_ID_SONAR_SWEEP), &sv);
    printf("write sonar_sweep[8] -> %s\n", UA_StatusCode_name(rv));
    ok &= (rv == UA_STATUSCODE_GOOD);

    UA_Variant rv2; UA_Variant_init(&rv2);
    UA_Client_readValueAttribute(c,
        UA_NODEID_NUMERIC(WTSN_OPCUA_NS, WTSN_ID_SONAR_SWEEP), &rv2);
    int first = rv2.data ? (int)((const UA_Int16 *)rv2.data)[0] : -1;
    printf("read back sonar_sweep len=%u first=%d (expect 8, 0)\n",
           (unsigned)rv2.arrayLength, first);
    ok &= (rv2.arrayLength == 8 && first == 0);
    UA_Variant_clear(&sv);
    UA_Variant_clear(&rv2);

    /* read a command var (cmd_servo_angle, expect 90) */
    UA_Variant_init(&v);
    UA_Client_readValueAttribute(c,
        UA_NODEID_NUMERIC(WTSN_OPCUA_NS, WTSN_ID_CMD_SERVO_ANGLE), &v);
    int16_t cmd = -1;
    if (UA_Variant_isScalar(&v)) cmd = *(UA_Int16 *)v.data;
    printf("read cmd_servo_angle = %d (expect 90)\n", (int)cmd);
    ok &= (cmd == 90);
    UA_Variant_clear(&v);

    UA_Client_disconnect(c);
    UA_Client_delete(c);
    printf(ok ? "TEST OK\n" : "TEST FAIL\n");
    return ok ? 0 : 1;
}
