#ifndef HTSN_DISCOVERY_H
#define HTSN_DISCOVERY_H

#include "common/common.h"
#include "device/device.h"

typedef enum {
    HTSN_DISCOVERY_MQTT = 0,
    HTSN_DISCOVERY_PLUGIN,
    HTSN_DISCOVERY_MANUAL
} htsn_discovery_source;

typedef struct htsn_discoverer htsn_discoverer;

typedef htsn_error (*htsn_discovery_run_fn)(htsn_discoverer *d, htsn_device *out, int max, int *count);
typedef void (*htsn_discovery_destroy_fn)(htsn_discoverer *d);

struct htsn_discoverer {
    htsn_discovery_source source;
    char name[HTSN_MAX_STR];
    void *data;
    htsn_discovery_run_fn run;
    htsn_discovery_destroy_fn destroy;
    void (*on_device)(htsn_device *dev, void *ud);
    void *userdata;
};

typedef htsn_discoverer *(*htsn_discovery_create_fn)(htsn_discovery_source src);

htsn_discoverer *htsn_discovery_create(htsn_discovery_source src, const char *name,
                                       htsn_discovery_run_fn run,
                                       htsn_discovery_destroy_fn destroy, void *data);
void htsn_discovery_destroy(htsn_discoverer *d);

#endif
