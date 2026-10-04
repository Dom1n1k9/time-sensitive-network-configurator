#include "app/app.h"

#include "config_version/config_version_manager.h"

#include "db/db.h"

#include "device/device_manager.h"

#include "domain/domain_manager.h"

#include "mvc/event_bus.h"

#include "fxmqtt/fxmqtt.h"

#include "mqtt/mqtt_client.h"

#include "plugin/plugin_manager.h"

#include "qos/qos_manager.h"

#include "sensors/sensor_manager.h"

#include "tas/tas_manager.h"

#include "telemetry/htsn_telemetry.h"

#include "timesync/timesync_manager.h"

#include "trace/trace.h"

#include "stream/tsn_manager.h"

#include "vlan/vlan_manager.h"

#include "common/log.h"
#include "common/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static htsn_error load_plugins(htsn_app *app) {
    if (strlen(app->config.plugin_dir) == 0) return HTSN_OK;
    char path[HTSN_MAX_STR];
    snprintf(path, sizeof(path), "%s/plugin-mqtt-discovery.so", app->config.plugin_dir);
    htsn_plugin_manager_load(app->plugins, path);
    return HTSN_OK;
}

htsn_error htsn_app_init(htsn_app *app, const htsn_app_config *cfg) {
    if (!app || !cfg) return HTSN_ERR_INVALID_ARG;
    memset(app, 0, sizeof(*app));
    app->config = *cfg;

    htsn_log_init(HTSN_LOG_INFO, NULL);

    htsn_error e = htsn_db_open(&app->db, cfg->db_path[0] ? cfg->db_path : "htsn.db");
    if (e != HTSN_OK) return e;

    app->bus = htsn_event_bus_create();
    if (!app->bus) { htsn_db_close(&app->db); return HTSN_ERR_NO_MEMORY; }

    app->trace = htsn_trace_create_persistent(app->bus, &app->db, 20000);

    app->plugins = htsn_plugin_manager_create();
    app->devices = htsn_device_manager_create(&app->db, app->bus, app->plugins);
    app->domains = htsn_domain_manager_create(&app->db, app->bus);
    app->qos = htsn_qos_manager_create(&app->db, app->bus);
    app->vlan = htsn_vlan_manager_create(&app->db, app->bus);
    app->timesync = htsn_timesync_manager_create(&app->db, app->bus);
    app->tas = htsn_tas_manager_create(&app->db, app->bus);
    app->sensors = htsn_sensor_manager_create(&app->db, app->bus);
    app->cfgver = htsn_cfg_ver_manager_create(&app->db, app->bus);

    htsn_tsn_manager_config tcfg;
    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.db = &app->db;
    tcfg.bus = app->bus;
    tcfg.mqtt = NULL;   /* assigned below once the MQTT client exists */
    app->tsn = htsn_tsn_manager_create(&tcfg);

    if (!app->devices || !app->qos || !app->vlan || !app->timesync ||
        !app->tas || !app->sensors || !app->tsn || !app->domains || !app->cfgver)
        return HTSN_ERR_NO_MEMORY;

    load_plugins(app);
    htsn_device_manager_discover_once(app->devices);

    /* OPC UA FX over MQTT: the single communication channel (PubSub, C2C). */
    app->mqtt = NULL;
    app->fxmqtt = htsn_fxmqtt_create();
    if (!app->fxmqtt) return HTSN_ERR_NO_MEMORY;

    if (strlen(cfg->mqtt_host) > 0) {
        app->mqtt = htsn_mqtt_client_create(app->bus);
        htsn_mqtt_client_connect(app->mqtt, cfg->mqtt_host, cfg->mqtt_port,
                                 "htsn-configurator",
                                 cfg->mqtt_user[0] ? cfg->mqtt_user : NULL,
                                 cfg->mqtt_pass[0] ? cfg->mqtt_pass : NULL);
        htsn_mqtt_client_loop_start(app->mqtt);
        htsn_tsn_manager_set_mqtt(app->tsn, app->mqtt);
        app->telemetry = htsn_telemetry_create(app->devices, app->timesync, app->trace);
        htsn_telemetry_attach(app->telemetry, app->mqtt);
        htsn_fxmqtt          *fcfg = app->fxmqtt;
        fcfg->broker_port = cfg->mqtt_port;
        htsn_strlcpy(fcfg->broker_host, cfg->mqtt_host, sizeof(fcfg->broker_host));
        htsn_fxmqtt_configure(app->fxmqtt, fcfg);
        htsn_fxmqtt_start(app->fxmqtt, app->mqtt);
        if (app->trace) htsn_trace_add_config(app->trace, "fxmqtt",
                "OPC UA FX over MQTT started");
        if (app->trace) htsn_trace_add_config(app->trace, "telemetry",
                "Monitoring tsn/status/# and tsn/telemetry/# subscribed");
    }

    return HTSN_OK;
}

void htsn_app_shutdown(htsn_app *app) {
    if (!app) return;
    if (app->fxmqtt) htsn_fxmqtt_destroy(app->fxmqtt);
    if (app->telemetry) htsn_telemetry_destroy(app->telemetry);
    if (app->mqtt) htsn_mqtt_client_destroy(app->mqtt);
    if (app->trace) htsn_trace_destroy(app->trace);
    if (app->cfgver) htsn_cfg_ver_manager_destroy(app->cfgver);
    if (app->sensors) htsn_sensor_manager_destroy(app->sensors);
    if (app->tsn) htsn_tsn_manager_destroy(app->tsn);
    if (app->tas) htsn_tas_manager_destroy(app->tas);
    if (app->timesync) htsn_timesync_manager_destroy(app->timesync);
    if (app->vlan) htsn_vlan_manager_destroy(app->vlan);
    if (app->qos) htsn_qos_manager_destroy(app->qos);
    if (app->domains) htsn_domain_manager_destroy(app->domains);
    if (app->devices) htsn_device_manager_destroy(app->devices);
    htsn_plugin_manager_destroy(app->plugins);
    htsn_event_bus_destroy(app->bus);
    htsn_db_close(&app->db);
}

htsn_error htsn_app_run(htsn_app *app) {
    if (!app) return HTSN_ERR_INVALID_ARG;
    if (app->config.headless) {
        htsn_log(HTSN_LOG_INFO, "headless mode: running ops loop (ctrl-c to stop)");
        for (;;) sleep(1);
    }
    return HTSN_OK;
}
