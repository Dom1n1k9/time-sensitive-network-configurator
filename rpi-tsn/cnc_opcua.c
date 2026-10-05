/* htsn_tsn CNC OPC UA server — runs on the RPi (the CNC / Controller).
 *
 * Two data paths, both on this process:
 *
 *  1) OPC UA client-server (opc.tcp://:4840) — the management plane. The GUI,
 *     the read-only poller (tsn_opcua_link) and the one-shot CLI (htsn_opcua_cli)
 *     read telemetry and write command_* here.
 *
 *  2) OPC UA FX field-level PubSub (UDP-UADP) — the RPi<->STM32 data plane.
 *     Replaces the raw-UDP htsn_frame bridge. This server PUBLISHES the command
 *     dataset (C2D) to the STM and SUBSCRIBES the telemetry dataset (D2C) back
 *     into the same telemetry nodes the poller reads, so the GUI is unchanged.
 *
 * NodeIds / the node model live in htsn_opcua_ids.h + htsn_nodes.c. The PubSub
 * wire contract (ids, dataset field order, defaults) lives in htsn_pubsub.h.
 *
 * PubSub env (all optional):
 *   HTSN_PUBSUB_ENABLE   1/0  enable the PubSub plane (default 1)
 *   HTSN_PUBSUB_PUB_ADDR opc.udp://<stm-ip>:<port>/  publish commands here
 *   HTSN_PUBSUB_SUB_PORT int  local UDP port to receive telemetry (default 8898)
 *   HTSN_PUBSUB_IFACE    eth0  network interface (default: OS routing)
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
#include <signal.h>

static UA_Server *server;
static volatile UA_Boolean running = UA_TRUE;
static UA_Boolean pubsub_up = UA_FALSE;

static void on_signal(int s) { (void)s; running = UA_FALSE; }

static const char *env_or(const char *k, const char *d) {
    const char *v = getenv(k);
    return (v && *v) ? v : d;
}

static UA_UInt16 ns_index;   /* set in main from htsn_build_address_space */

static UA_StatusCode setup_pubsub(void) {
    const char *enable = env_or("HTSN_PUBSUB_ENABLE", "1");
    if (strcmp(enable, "0") == 0 || strcmp(enable, "false") == 0 ||
        strcmp(enable, "off") == 0 || strcmp(enable, "no") == 0) {
        printf("pubsub: disabled (HTSN_PUBSUB_ENABLE=0)\n");
        return UA_STATUSCODE_GOOD;
    }
    const char *pub_addr = env_or("HTSN_PUBSUB_PUB_ADDR", HTSN_PUBSUB_PUB_ADDR_DEF);
    const char *port_s   = env_or("HTSN_PUBSUB_SUB_PORT", "8898");
    const char *iface    = env_or("HTSN_PUBSUB_IFACE", "");
    int sub_port = (int)atol(port_s);
    if (sub_port <= 0) sub_port = HTSN_PUBSUB_SUB_PORT_DEF;

    UA_StatusCode rv = htsn_ps_attach_controller(server, ns_index, pub_addr, sub_port, iface);
    if (rv != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "pubsub: attach failed (%s) — continuing client-server only\n",
                UA_StatusCode_name(rv));
        return rv;
    }
    pubsub_up = UA_TRUE;
    printf("pubsub: controller plane up (cmd -> %s, telem <- :%d%s)\n",
           pub_addr, sub_port, (*iface ? " on " : ""));
    return UA_STATUSCODE_GOOD;
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* Default opc.tcp config on :4840, but with PubSub delta-frames disabled:
     * the datasets are small and fixed, so every publish is a keyframe (the
     * endpoint's reader only consumes keyframes). UA_Server_new() == setMinimal(4840).
     *
     * CRITICAL: pin the application URI to "urn:htsn:stm32" so open62541 registers
     * it as namespace 1 (the app URI is always reserved as ns1). This is what makes
     * the whole system's hardcoded ns==1 (GUI / poller / CLI / endpoint) line up,
     * regardless of how many other built-in namespaces a build registers. */
    UA_ServerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    UA_ServerConfig_setMinimal(&cfg, 4840, NULL);
    cfg.applicationDescription.applicationUri = UA_STRING("urn:htsn:stm32");
    cfg.pubSubConfig.enableDeltaFrames = UA_FALSE;
    server = UA_Server_newWithConfig(&cfg);

    ns_index = htsn_build_address_space(server);
    if (ns_index == 0) {
        fprintf(stderr, "failed to build address space\n");
        return 1;
    }

    /* PubSub components are added disabled; they start after run_startup. */
    setup_pubsub();

    UA_Server_run_startup(server);
    if (pubsub_up) {
        UA_StatusCode rv = UA_Server_enableAllPubSubComponents(server);
        if (rv != UA_STATUSCODE_GOOD)
            fprintf(stderr, "pubsub: enableAll -> %s\n", UA_StatusCode_name(rv));
    }

    UA_Variant sv;
    UA_Variant_init(&sv);
    UA_StatusCode s = UA_Server_readValue(server,
        UA_NODEID_NUMERIC(ns_index, HTSN_ID_SERVO_ANGLE), &sv);
    printf("SELFTEST servo_angle: %s scalar=%d data=%p val=%d\n",
           UA_StatusCode_name(s), (int)UA_Variant_isScalar(&sv), sv.data,
           (UA_Variant_isScalar(&sv) && sv.data) ? (int)*(UA_Int16 *)sv.data : -1);
    UA_Variant_clear(&sv);
    printf("htsn_tsn OPC UA server on opc.tcp://0.0.0.0:4840 (ns %d = urn:htsn:stm32)%s\n",
           (int)ns_index, pubsub_up ? "  [+ OPC UA FX PubSub]" : "");

    while (running)
        UA_Server_run_iterate(server, UA_TRUE);

    if (pubsub_up)
        UA_Server_disableAllPubSubComponents(server);
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    return 0;
}
