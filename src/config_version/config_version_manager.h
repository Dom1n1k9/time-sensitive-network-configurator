#ifndef HTSN_CONFIG_VERSION_MANAGER_H
#define HTSN_CONFIG_VERSION_MANAGER_H

#include "common/common.h"
#include "db/db_config_versions.h"
#include "mvc/event_bus.h"

#define HTSN_CONFIG_VERSION_MODEL "config_version"

typedef struct htsn_config_version_manager htsn_config_version_manager;

htsn_config_version_manager *htsn_cfg_ver_manager_create(htsn_db *db, htsn_event_bus *bus);
void htsn_cfg_ver_manager_destroy(htsn_config_version_manager *m);

/* snapshot current config for a device (or global) and store it as a version */
htsn_error htsn_cfg_ver_snapshot(htsn_config_version_manager *m, const char *name,
                                 const char *device_id);
htsn_error htsn_cfg_ver_rollback(htsn_config_version_manager *m, int id);
htsn_error htsn_cfg_ver_diff(htsn_config_version_manager *m, int id_a, int id_b,
                             char *out, size_t out_size);
int htsn_cfg_ver_count(htsn_config_version_manager *m);
void htsn_cfg_ver_for_each(htsn_config_version_manager *m, htsn_db_config_version_cb cb,
                           void *ud);

#endif
