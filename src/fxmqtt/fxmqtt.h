#ifndef HTSN_FXMQTT_H
#define HTSN_FXMQTT_H

#include "common/common.h"
#include "mqtt/mqtt_client.h"

#define HTSN_FXMQTT_TOPIC_FIELD "tsn/fx/field"
#define HTSN_FXMQTT_TOPIC_DATA "tsn/fx/data"
#define HTSN_FXMQTT_TOPIC_NODE "tsn/fx/node"

typedef enum {
    HTSN_FXMQTT_SERVER_NONE = 0,
    HTSN_FXMQTT_SERVER_PC,     /* server = configurator (this PC) */
    HTSN_FXMQTT_SERVER_NODE     /* server = selected device node */
} htsn_fxmqtt_server_type;

typedef struct {
    char device_id[HTSN_MAX_STR];
    htsn_fxmqtt_server_type server_type;
    char broker_host[HTSN_MAX_STR];
    int broker_port;
    bool started;
    htsn_mqtt_client *mqtt;   /* broker channel used for publish */
} htsn_fxmqtt;

htsn_fxmqtt *htsn_fxmqtt_create(void);
void htsn_fxmqtt_destroy(htsn_fxmqtt *f);
htsn_error htsn_fxmqtt_configure(htsn_fxmqtt *f, const htsn_fxmqtt *cfg);
htsn_error htsn_fxmqtt_start(htsn_fxmqtt *f, htsn_mqtt_client *mqtt);
htsn_error htsn_fxmqtt_field_publish(htsn_fxmqtt *f, const char *payload);
htsn_error htsn_fxmqtt_send_c2c(htsn_fxmqtt *f, const char *topic, const char *payload);

#endif
