#ifndef HTSN_DEVICE_H
#define HTSN_DEVICE_H

#include "common/common.h"

#include <time.h>

#define HTSN_TSN_FEATURES_MAX 8

typedef enum {
    HTSN_DEVICE_ONLINE = 0,
    HTSN_DEVICE_OFFLINE,
    HTSN_DEVICE_ERROR
} htsn_device_status;

typedef enum {
    HTSN_DEVICE_KIND_UNKNOWN = 0,
    HTSN_DEVICE_KIND_ESP32,
    HTSN_DEVICE_KIND_RASPBERRYPI,
    HTSN_DEVICE_KIND_STM32
} htsn_device_kind;

typedef struct {
    char id[HTSN_MAX_STR];
    char ip[64];
    char mac[32];
    char firmware[32];
    time_t last_seen;
    htsn_device_status status;
    htsn_device_kind kind;
    char tsn_features[HTSN_TSN_FEATURES_MAX][64];
    size_t tsn_features_count;
    char name[HTSN_MAX_STR];
    char domain[HTSN_MAX_STR];
    time_t heartbeat_at;
} htsn_device;

const char *htsn_device_status_str(htsn_device_status s);
const char *htsn_device_kind_str(htsn_device_kind k);
htsn_device_kind htsn_device_kind_parse(const char *s);
void htsn_device_add_tsn_feature(htsn_device *d, const char *feature);

#endif
