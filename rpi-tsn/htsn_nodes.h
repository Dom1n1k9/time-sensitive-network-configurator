/* htsn_nodes — the htsn_tsn OPC UA address space (namespace "urn:htsn:stm32").
 *
 * Built identically on every endpoint that hosts the node model:
 *   - the RPi/CNC server (cnc_opcua)  - the source of truth for the GUI;
 *   - the STM32 (when it runs the same open62541 server for its own nodes);
 *   - the PubSub round-trip test (two local servers).
 *
 * NodeIds come from htsn_opcua_ids.h (the numeric id + ns index are the contract).
 */
#ifndef HTSN_NODES_H
#define HTSN_NODES_H

#include <open62541/server.h>

/* Build the htsn_tsn address space on `server`. Returns the namespace index of
 * "urn:htsn:stm32" (>=1), or 0 on failure. Callers must reference the nodes with
 * THIS returned index.
 *
 * The index is pinned to 1 by the caller setting the server's application URI to
 * "urn:htsn:stm32" (open62541 always reserves the app URI as namespace 1). That
 * is what keeps the whole system's hardcoded ns==1 (GUI / poller / CLI / endpoint)
 * aligned regardless of how many other built-in namespaces a build registers (a
 * discovery-enabled build otherwise reserves ns1 and would push this to ns2). */
UA_UInt16 htsn_build_address_space(UA_Server *server);

#endif /* HTSN_NODES_H */
