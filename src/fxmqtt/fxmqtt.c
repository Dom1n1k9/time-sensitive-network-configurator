#include "fxmqtt/fxmqtt.h"

#include "mqtt/mqtt_client.h"

#include "common/log.h"
#include "common/str_util.h"

#include <stdlib.h>
#include <string.h>

htsn_fxmqtt *htsn_fxmqtt_create(void) {
    htsn_fxmqtt *f = calloc(1, sizeof(htsn_fxmqtt));
    if (!f) return NULL;
    f->server_type = HTSN_FXMQTT_SERVER_PC;
    htsn_strlcpy(f->broker_host, "127.0.0.1", sizeof(f->broker_host));
    f->broker_port = 1883;
    return f;
}

void htsn_fxmqtt_destroy(htsn_fxmqtt *f) {
    free(f);
}

htsn_error htsn_fxmqtt_configure(htsn_fxmqtt *f, const htsn_fxmqtt *cfg) {
    if (!f || !cfg) return HTSN_ERR_INVALID_ARG;
    htsn_strlcpy(f->device_id, cfg->device_id, sizeof(f->device_id));
    f->server_type = cfg->server_type;
    htsn_strlcpy(f->broker_host, cfg->broker_host[0] ? cfg->broker_host : "127.0.0.1",
                 sizeof(f->broker_host));
    f->broker_port = cfg->broker_port > 0 ? cfg->broker_port : 1883;
    return HTSN_OK;
}

htsn_error htsn_fxmqtt_start(htsn_fxmqtt *f, htsn_mqtt_client *mqtt) {
    if (!f || !mqtt) return HTSN_ERR_INVALID_ARG;
    /* subscribe to the C2C field topics on the broker */
    htsn_mqtt_client_subscribe(mqtt, HTSN_FXMQTT_TOPIC_FIELD);
    htsn_mqtt_client_subscribe(mqtt, HTSN_FXMQTT_TOPIC_DATA);
    htsn_mqtt_client_subscribe(mqtt, HTSN_FXMQTT_TOPIC_NODE);
    f->mqtt = mqtt;
    f->started = true;
    htsn_log(HTSN_LOG_INFO, "fx over mqtt started server_type=%d broker %s:%d",
             (int)f->server_type, f->broker_host, f->broker_port);
    return HTSN_OK;
}

/* Send an FX payload destined for a specific node topic */
htsn_error htsn_fxmqtt_field_publish_ex(htsn_fxmqtt *f, const char *topic,
                                        const char *payload) {
    if (!f || !f->mqtt || !f->started) return HTSN_ERR_NOT_READY;
    if (!payload) return HTSN_ERR_INVALID_ARG;
    const char *t = topic && topic[0] ? topic : HTSN_FXMQTT_TOPIC_FIELD;
    if (htsn_mqtt_client_publish(f->mqtt, t, payload) == HTSN_OK) {
        htsn_log(HTSN_LOG_INFO, "fx field -> %s: %s", t, payload);
        return HTSN_OK;
    }
    return HTSN_ERR_NET;
}

htsn_error htsn_fxmqtt_field_publish(htsn_fxmqtt *f, const char *payload) {
    return htsn_fxmqtt_field_publish_ex(f, HTSN_FXMQTT_TOPIC_FIELD, payload);
}

/* Send a C2C field-exchange message to topic tsn/fx/<node> (or any FX topic) */
htsn_error htsn_fxmqtt_send_c2c(htsn_fxmqtt *f, const char *topic, const char *payload) {
    return htsn_fxmqtt_field_publish_ex(f, topic, payload);
}
