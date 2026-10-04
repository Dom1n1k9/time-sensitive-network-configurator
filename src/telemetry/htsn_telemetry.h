#ifndef HTSN_TELEMETRY_H
#define HTSN_TELEMETRY_H

#include "common/common.h"
#include "db/db.h"
#include "device/device_manager.h"
#include "mqtt/mqtt_client.h"
#include "timesync/timesync_manager.h"
#include "trace/trace.h"

typedef struct htsn_telemetry htsn_telemetry;

htsn_telemetry *htsn_telemetry_create(htsn_device_manager *devices,
                                       htsn_timesync_manager *timesync,
                                       htsn_trace *trace);
void htsn_telemetry_destroy(htsn_telemetry *t);

/* Wire the handler onto an MQTT client and subscribe to status/heartbeat topics. */
htsn_error htsn_telemetry_attach(htsn_telemetry *t, htsn_mqtt_client *mqtt);
void htsn_telemetry_detach(htsn_telemetry *t);

#endif
