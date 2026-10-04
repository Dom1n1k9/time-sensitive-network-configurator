#ifndef HTSN_AGENT_H
#define HTSN_AGENT_H

#include "common/common.h"
#include "agent/platform/agent_platform.h"
#include "mqtt/mqtt_client.h"

typedef struct {
    char device_id[HTSN_MAX_STR];
    char platform_str[HTSN_MAX_STR];
    agent_platform platform;
    agent_platform_ops ops;
    void *state;
    char mqtt_host[128];
    int mqtt_port;
    htsn_mqtt_client *mqtt;
    /* internal handle to the command loop context */
    void *ctx;
} htsn_agent;

htsn_agent *htsn_agent_create(const char *device_id, const char *platform_str,
                              const char *mqtt_host, int mqtt_port);
void htsn_agent_destroy(htsn_agent *a);
htsn_error htsn_agent_start(htsn_agent *a);
htsn_error htsn_agent_handle_command(htsn_agent *a, const char *command, const char *payload);
int htsn_agent_run_loop(htsn_agent *a);

#endif
