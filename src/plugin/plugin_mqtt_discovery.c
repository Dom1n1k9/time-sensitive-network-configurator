#include "plugin/plugin_api.h"
#include "discovery/discovery.h"
#include "common/str_util.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char broker[HTSN_MAX_STR];
    int port;
    char topic_prefix[HTSN_MAX_STR];
    struct mosquitto *client;
} mqtt_plugin_data;

static htsn_error mqtt_discover(htsn_plugin *self, htsn_device *out, int max, int *count) {
    mqtt_plugin_data *d = (mqtt_plugin_data *)self->userdata;
    (void)d;
    /* Intentionally minimal: depends on libmosquitto for real results.
       This builds without hard external linkage beyond the plugin wrapper. */
    if (max > 0 && count) *count = 0;
    return HTSN_OK;
}

static htsn_error mqtt_probe(htsn_plugin *self, const char *discovery_data) {
    (void)self;
    if (!discovery_data) return HTSN_ERR_INVALID_ARG;
    /* A probe scans a discovery payload for an MQTT broker announcement. */
    return htsn_str_starts_with(discovery_data, "mqtt:") ? HTSN_OK : HTSN_ERR_NOT_FOUND;
}

static htsn_error mqtt_shutdown(htsn_plugin *self) {
    mqtt_plugin_data *d = (mqtt_plugin_data *)self->userdata;
    if (d) free(d);
    return HTSN_OK;
}

htsn_plugin *htsn_plugin_create(void) {
    htsn_plugin *p = calloc(1, sizeof(htsn_plugin));
    if (!p) return NULL;
    p->api_version = HTSN_PLUGIN_API_VERSION;
    htsn_strlcpy(p->name, "mqtt-discovery", sizeof(p->name));
    mqtt_plugin_data *d = calloc(1, sizeof(mqtt_plugin_data));
    if (!d) { free(p); return NULL; }
    htsn_strlcpy(d->broker, "localhost", sizeof(d->broker));
    d->port = 1883;
    htsn_strlcpy(d->topic_prefix, "tsn/discovery", sizeof(d->topic_prefix));
    p->userdata = d;
    p->probe = mqtt_probe;
    p->discover = mqtt_discover;
    p->shutdown = mqtt_shutdown;
    return p;
}
