#include "mqtt/mqtt_client.h"

#include "mvc/event_bus.h"

#include "common/str_util.h"

#include "common/log.h"

#include <stdlib.h>
#include <string.h>

#include <mosquitto.h>

struct htsn_mqtt_client {
    struct mosquitto *client;
    htsn_event_bus *bus;
    htsn_mqtt_message_cb msg_cb;
    void *msg_ud;
    char host[128];
    int port;
    bool connected;
};

static void on_connect(struct mosquitto *mq, void *userdata, int rc) {
    htsn_mqtt_client *c = (htsn_mqtt_client *)userdata;
    (void)mq;
    if (rc == 0) {
        c->connected = true;
        htsn_log(HTSN_LOG_INFO, "mqtt connected to %s:%d", c->host, c->port);
        htsn_event_bus_publish(c->bus, "mqtt.connected", NULL);
    } else {
        c->connected = false;
        htsn_log(HTSN_LOG_WARN, "mqtt connect refused rc=%d", rc);
    }
}

static void on_message(struct mosquitto *mq, void *userdata, const struct mosquitto_message *msg) {
    htsn_mqtt_client *c = (htsn_mqtt_client *)userdata;
    (void)mq;
    const char *topic = msg->topic ? msg->topic : "";
    const char *payload = msg->payload ? (const char *)msg->payload : "";
    size_t len = msg->payloadlen;

    if (c->msg_cb) {
        char *copy = malloc(len + 1);
        if (copy) {
            memcpy(copy, payload, len);
            copy[len] = '\0';
            c->msg_cb(topic, copy, len, c->msg_ud);
            free(copy);
        }
    }
    htsn_event_bus_publish(c->bus, "mqtt.message", (void *)topic);
}

static void on_disconnect(struct mosquitto *mq, void *userdata, int rc) {
    htsn_mqtt_client *c = (htsn_mqtt_client *)userdata;
    (void)mq;
    c->connected = false;
    htsn_log(HTSN_LOG_INFO, "mqtt disconnected rc=%d", rc);
    htsn_event_bus_publish(c->bus, "mqtt.disconnected", NULL);
}

htsn_mqtt_client *htsn_mqtt_client_create(htsn_event_bus *bus) {
    htsn_mqtt_client *c = calloc(1, sizeof(htsn_mqtt_client));
    if (!c) return NULL;
    c->bus = bus;
    return c;
}

void htsn_mqtt_client_destroy(htsn_mqtt_client *c) {
    if (!c) return;
    if (c->client) {
        htsn_mqtt_client_loop_stop(c);
        mosquitto_destroy(c->client);
    }
    free(c);
}

htsn_error htsn_mqtt_client_connect(htsn_mqtt_client *c, const char *host, int port,
                                    const char *client_id, const char *username, const char *password) {
    if (!c || !host || port <= 0) return HTSN_ERR_INVALID_ARG;
    if (c->client) return HTSN_ERR_BUSY;

    mosquitto_lib_init();
    c->client = mosquitto_new(client_id ? client_id : "htsn-configurator", true, c);
    if (!c->client) return HTSN_ERR_NO_MEMORY;

    if (username) mosquitto_username_pw_set(c->client, username, password);
    mosquitto_connect_callback_set(c->client, on_connect);
    mosquitto_message_callback_set(c->client, on_message);
    mosquitto_disconnect_callback_set(c->client, on_disconnect);

    htsn_strlcpy(c->host, host, sizeof(c->host));
    c->port = port;

    if (mosquitto_connect_async(c->client, host, port, 30) != MOSQ_ERR_SUCCESS) {
        htsn_log(HTSN_LOG_ERROR, "mqtt connect_async failed");
        return HTSN_ERR_NET;
    }
    return HTSN_OK;
}

void htsn_mqtt_client_disconnect(htsn_mqtt_client *c) {
    if (c && c->client) mosquitto_disconnect(c->client);
}

htsn_error htsn_mqtt_client_subscribe(htsn_mqtt_client *c, const char *topic) {
    if (!c || !c->client || !topic) return HTSN_ERR_INVALID_ARG;
    return mosquitto_subscribe(c->client, NULL, topic, 0) == MOSQ_ERR_SUCCESS
        ? HTSN_OK : HTSN_ERR_NET;
}

htsn_error htsn_mqtt_client_publish(htsn_mqtt_client *c, const char *topic, const char *payload) {
    if (!c || !c->client || !topic) return HTSN_ERR_INVALID_ARG;
    return mosquitto_publish(c->client, NULL, topic, (int)strlen(payload),
                            payload, 0, false) == MOSQ_ERR_SUCCESS
        ? HTSN_OK : HTSN_ERR_NET;
}

void htsn_mqtt_client_set_message_cb(htsn_mqtt_client *c, htsn_mqtt_message_cb cb, void *ud) {
    if (!c) return;
    c->msg_cb = cb;
    c->msg_ud = ud;
}

void htsn_mqtt_client_loop_start(htsn_mqtt_client *c) {
    if (c && c->client) mosquitto_loop_start(c->client);
}

void htsn_mqtt_client_loop_stop(htsn_mqtt_client *c) {
    if (c && c->client) mosquitto_loop_stop(c->client, true);
}
