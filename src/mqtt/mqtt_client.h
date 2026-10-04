#ifndef HTSN_MQTT_CLIENT_H
#define HTSN_MQTT_CLIENT_H

#include "common/common.h"
#include "device/device.h"
#include "mvc/event_bus.h"

typedef struct htsn_mqtt_client htsn_mqtt_client;

typedef void (*htsn_mqtt_message_cb)(const char *topic, const char *payload, size_t len, void *ud);

htsn_mqtt_client *htsn_mqtt_client_create(htsn_event_bus *bus);
void htsn_mqtt_client_destroy(htsn_mqtt_client *c);

htsn_error htsn_mqtt_client_connect(htsn_mqtt_client *c, const char *host, int port,
                                    const char *client_id, const char *username, const char *password);
void htsn_mqtt_client_disconnect(htsn_mqtt_client *c);
htsn_error htsn_mqtt_client_subscribe(htsn_mqtt_client *c, const char *topic);
htsn_error htsn_mqtt_client_publish(htsn_mqtt_client *c, const char *topic, const char *payload);
void htsn_mqtt_client_set_message_cb(htsn_mqtt_client *c, htsn_mqtt_message_cb cb, void *ud);
void htsn_mqtt_client_loop_start(htsn_mqtt_client *c);
void htsn_mqtt_client_loop_stop(htsn_mqtt_client *c);

#endif
