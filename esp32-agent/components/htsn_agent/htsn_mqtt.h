#ifndef HTSN_MQTT_H
#define HTSN_MQTT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct htsn_mqtt htsn_mqtt;

/* on_command (topic, payload) is called with a normalized topic and payload
   whenever a command arrives. */
typedef void (*htsn_cmd_cb)(const char *topic, const char *payload, void *ud);

typedef void (*htsn_connected_cb)(const char *client_id, void *ud);

htsn_mqtt *htsn_mqtt_create(const char *host, int port, const char *client_id,
                             htsn_cmd_cb cb, htsn_connected_cb conn_cb, void *ud);
/* Extended create with optional broker auth + TLS:
 *   user/pass   -> MQTT username/password (may be NULL for none)
 *   tls         -> use TLS over port `port` (esp_mqtt client) when non-zero
 *   tls_ca_pem  -> CA certificate / bundle to verify the broker (may be NULL)
 *   insecure    -> do not verify the broker certificate (dev only)
 * The plain htsn_mqtt_create() calls this with NULL/no-TLS. */
htsn_mqtt *htsn_mqtt_create_auth(const char *host, int port, const char *client_id,
                                 const char *user, const char *pass,
                                 bool tls, const char *tls_ca_pem, bool insecure,
                                 htsn_cmd_cb cb, htsn_connected_cb conn_cb, void *ud);
void htsn_mqtt_start(htsn_mqtt *m);
void htsn_mqtt_publish(htsn_mqtt *m, const char *topic, const char *payload);
void htsn_mqtt_publish_qos(htsn_mqtt *m, const char *topic, const char *payload, int qos);
void htsn_mqtt_set_device_id(htsn_mqtt *m, const char *id);

#endif
