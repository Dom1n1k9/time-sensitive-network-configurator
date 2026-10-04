#ifndef HTSN_APP_H
#define HTSN_APP_H

#include "config_version/config_version_manager.h"
#include "common/common.h"
#include "db/db.h"
#include "device/device_manager.h"
#include "domain/domain_manager.h"
#include "fxmqtt/fxmqtt.h"
#include "mqtt/mqtt_client.h"
#include "mvc/event_bus.h"
#include "plugin/plugin_manager.h"
#include "qos/qos_manager.h"
#include "radio/htsn_radio.h"
#include "sensors/sensor_manager.h"
#include "stream/tsn_manager.h"
#include "tas/tas_manager.h"
#include "telemetry/htsn_telemetry.h"
#include "timesync/timesync_manager.h"
#include "trace/trace.h"
#include "vlan/vlan_manager.h"

typedef struct {
    char db_path[HTSN_MAX_STR];
    char plugin_dir[HTSN_MAX_STR];
    char mqtt_host[128];
    int mqtt_port;
    char mqtt_user[128];
    char mqtt_pass[128];
    bool headless;
} htsn_app_config;

typedef struct htsn_app {
    htsn_db db;
    htsn_event_bus *bus;
    htsn_plugin_manager *plugins;
    htsn_device_manager *devices;
    htsn_qos_manager *qos;
    htsn_vlan_manager *vlan;
    htsn_timesync_manager *timesync;
    htsn_tas_manager *tas;
    htsn_sensor_manager *sensors;
    htsn_tsn_manager *tsn;
    htsn_mqtt_client *mqtt;
    htsn_fxmqtt *fxmqtt;
    htsn_trace *trace;
    htsn_domain_manager *domains;
    htsn_config_version_manager *cfgver;
    htsn_telemetry *telemetry;
    htsn_app_config config;
} htsn_app;

htsn_error htsn_app_init(htsn_app *app, const htsn_app_config *cfg);
void htsn_app_shutdown(htsn_app *app);
htsn_error htsn_app_run(htsn_app *app);

#endif
