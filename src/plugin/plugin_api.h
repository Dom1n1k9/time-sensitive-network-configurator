#ifndef HTSN_PLUGIN_API_H
#define HTSN_PLUGIN_API_H

#include "common/common.h"
#include "device/device.h"

#define HTSN_PLUGIN_API_VERSION 1

typedef struct htsn_plugin htsn_plugin;

struct htsn_plugin {
    int api_version;
    char name[HTSN_MAX_STR];
    void *handle;
    void *userdata;
    htsn_error (*probe)(htsn_plugin *self, const char *discovery_data);
    htsn_error (*discover)(htsn_plugin *self, htsn_device *out, int max, int *count);
    htsn_error (*shutdown)(htsn_plugin *self);
};

typedef htsn_plugin *(*htsn_plugin_create_fn)(void);
typedef void (*htsn_plugin_destroy_fn)(htsn_plugin *);

#endif
