# htsn_tsn RPi (CNC) OPC UA server + test.
#
# Build:   make PFX=/tmp/opencode/o62541-prefix
# Run:     ./cnc_opcua        (opc.tcp://:4840)   and  ./test_opcua_client
#
# PFX = prefix where open62541 was installed (headers + lib). Build open62541
# with -DUA_ENABLE_SUBSCRIBERS=ON (the STM32 client uses monotoring of cmd_*).
PFX     ?= /tmp/opencode/o62541-prefix
CFLAGS  += -O2 -Wall -I. -I$(PFX)/include
LDFLAGS += -L$(PFX)/lib -Wl,-rpath,$(PFX)/lib

all: cnc_opcua test_opcua_client htsn_opcua_cli tsn_opcua_link tsn_opcua_bridge

cnc_opcua: cnc_opcua.c htsn_opcua_ids.h
	$(CC) $(CFLAGS) -o $@ cnc_opcua.c $(LDFLAGS) -lopen62541

tsn_opcua_bridge: tsn_opcua_bridge.c htsn_opcua_ids.h htsn_frame.h
	$(CC) $(CFLAGS) -o $@ tsn_opcua_bridge.c $(LDFLAGS) -lopen62541

test_opcua_client: test_opcua_client.c htsn_opcua_ids.h
	$(CC) $(CFLAGS) -o $@ test_opcua_client.c $(LDFLAGS) -lopen62541

htsn_opcua_cli: htsn_opcua_cli.c htsn_opcua_ids.h
	$(CC) $(CFLAGS) -o $@ htsn_opcua_cli.c $(LDFLAGS) -lopen62541

tsn_opcua_link: tsn_opcua_link.c htsn_opcua_ids.h
	$(CC) $(CFLAGS) -o $@ tsn_opcua_link.c $(LDFLAGS) -lopen62541

clean:
	rm -f cnc_opcua test_opcua_client htsn_opcua_cli tsn_opcua_link

.PHONY: all clean
