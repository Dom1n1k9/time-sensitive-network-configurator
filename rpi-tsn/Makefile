# htsn_tsn RPi (CNC) OPC UA server + OPC UA FX PubSub plane + tests.
#
# Build:   make PFX=/tmp/opencode/o62541-prefix
# Run:     ./cnc_opcua             (opc.tcp://:4840 + PubSub FX plane)
#          ./htsn_pubsub_test      (loopback PubSub round-trip, no board)
#
# PFX = prefix where open62541 was installed (headers + lib). Build open62541
# with -DUA_ENABLE_SUBSCRIBERS=ON -DUA_ENABLE_PUBSUB=ON (the STM32 uses PubSub).
PFX     ?= /tmp/opencode/o62541-prefix
CFLAGS  += -O2 -Wall -I. -I$(PFX)/include
LDFLAGS += -L$(PFX)/lib -Wl,-rpath,$(PFX)/lib

# Shared objects: the node model + the PubSub plane (used by the server + test).
OBJS_COMMON = htsn_nodes.c htsn_pubsub.c

all: cnc_opcua test_opcua_client htsn_opcua_cli tsn_opcua_link tsn_opcua_bridge htsn_pubsub_test

cnc_opcua: cnc_opcua.c $(OBJS_COMMON) htsn_opcua_ids.h htsn_nodes.h htsn_pubsub.h
	$(CC) $(CFLAGS) -o $@ cnc_opcua.c $(OBJS_COMMON) $(LDFLAGS) -lopen62541

htsn_pubsub_test: htsn_pubsub_test.c $(OBJS_COMMON) htsn_opcua_ids.h htsn_nodes.h htsn_pubsub.h
	$(CC) $(CFLAGS) -o $@ htsn_pubsub_test.c $(OBJS_COMMON) $(LDFLAGS) -lopen62541

tsn_opcua_bridge: tsn_opcua_bridge.c htsn_opcua_ids.h htsn_frame.h
	$(CC) $(CFLAGS) -o $@ tsn_opcua_bridge.c $(LDFLAGS) -lopen62541

test_opcua_client: test_opcua_client.c htsn_opcua_ids.h
	$(CC) $(CFLAGS) -o $@ test_opcua_client.c $(LDFLAGS) -lopen62541

htsn_opcua_cli: htsn_opcua_cli.c htsn_opcua_ids.h
	$(CC) $(CFLAGS) -o $@ htsn_opcua_cli.c $(LDFLAGS) -lopen62541

tsn_opcua_link: tsn_opcua_link.c htsn_opcua_ids.h
	$(CC) $(CFLAGS) -o $@ tsn_opcua_link.c $(LDFLAGS) -lopen62541

clean:
	rm -f cnc_opcua test_opcua_client htsn_opcua_cli tsn_opcua_link tsn_opcua_bridge htsn_pubsub_test

.PHONY: all clean
