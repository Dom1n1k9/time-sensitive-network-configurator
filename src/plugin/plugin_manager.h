#ifndef HTSN_PLUGIN_MANAGER_H
#define HTSN_PLUGIN_MANAGER_H

#include "common/common.h"
#include "plugin/plugin_api.h"

typedef struct htsn_plugin_manager htsn_plugin_manager;

htsn_plugin_manager *htsn_plugin_manager_create(void);
void htsn_plugin_manager_destroy(htsn_plugin_manager *m);
htsn_error htsn_plugin_manager_load(htsn_plugin_manager *m, const char *path);
size_t htsn_plugin_manager_count(htsn_plugin_manager *m);
htsn_plugin *htsn_plugin_manager_get(htsn_plugin_manager *m, size_t index);

#endif
