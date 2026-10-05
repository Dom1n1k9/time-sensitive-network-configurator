/* htsn_pubsub — OPC UA FX field-level transport for the RPi<->STM32 wired link.
 *
 * "OPC UA FX" at the wire level is OPC UA PubSub carried over UDP-UADP
 * (deterministic / TSN-ready). It replaces the raw-UDP htsn_frame data plane:
 * the same node model is used, but the transport is PubSub instead of UDP.
 *
 * Two unidirectional datasets (unicast, one each way):
 *   C2D  Controller(RPi) -> Device(STM) : command/control dataset
 *   D2C  Device(STM) -> Controller(RPi) : telemetry dataset
 *
 * The field order in the *_FIELDS tables (htsn_pubsub.c) IS the wire contract;
 * the numeric ids below must match on both ends. The STM (Zephyr/open62541)
 * reuses these same attach functions, so keep this header the single source.
 */
#ifndef HTSN_PUBSUB_H
#define HTSN_PUBSUB_H

#include <open62541/server.h>
#include <open62541/types.h>

#define HTSN_PUBSUB_TRANSPORT_UDP  "http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp"

/* C2D command dataset (RPi publishes, STM subscribes) */
#define HTSN_PUBSUB_CMD_FIELDS     6
#define HTSN_PUBSUB_RPI_PUBID      0x4843   /* "HC" — RPi command publisher id */
#define HTSN_PUBSUB_RPI_WGID       0x0001
#define HTSN_PUBSUB_RPI_DSWID      0x1000

/* D2C telemetry dataset (STM publishes, RPi subscribes) */
#define HTSN_PUBSUB_TELEM_FIELDS   22
#define HTSN_PUBSUB_STM_PUBID      0x5354   /* "ST" — STM telemetry publisher id */
#define HTSN_PUBSUB_STM_WGID       0x0001
#define HTSN_PUBSUB_STM_DSWID      0x2000

/* Default endpoints (override via env; see cnc_opcua.c / run.sh).
 * The RPi (CNC) is 192.168.1.10 on the TSN LAN; the endpoint (STM32) is assumed
 * to be the next address. Set HTSN_PUBSUB_PUB_ADDR to the STM32's real IP. */
#define HTSN_PUBSUB_PUB_ADDR_DEF   "opc.udp://192.168.1.11:8899/" /* RPi publishes commands to the STM:port */
#define HTSN_PUBSUB_SUB_PORT_DEF   8898                           /* RPi listens for telemetry here        */

/* Attach the PubSub plane to a server.
 *
 * Controller (RPi/CNC) side: publish the command dataset to `pub_addr`, and
 * subscribe to the telemetry dataset on local `sub_port` (values land in the
 * existing telemetry nodes).
 *
 * Device (STM) side: the mirror — publish telemetry to `pub_addr` (the RPi),
 * and subscribe to the command dataset on local `sub_port` (values land in
 * the existing command nodes).
 *
 * `ns` is the namespace index of the node model (the value returned by
 * htsn_build_address_space) — the datasets reference those nodes.
 *
 * `iface` is the network-interface name (empty = OS routing). `sub_port` is the
 * UDP port to bind for the subscriber. Returns UA_STATUSCODE_GOOD on success.
 * The caller must then enable the components (UA_Server_enableAllPubSubComponents).
 */
UA_StatusCode htsn_ps_attach_controller(UA_Server *server, UA_UInt16 ns, const char *pub_addr,
                                        int sub_port, const char *iface);
UA_StatusCode htsn_ps_attach_device(UA_Server *server, UA_UInt16 ns, const char *pub_addr,
                                    int sub_port, const char *iface);

#endif /* HTSN_PUBSUB_H */
