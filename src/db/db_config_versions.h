#ifndef HTSN_DB_CONFIG_VERSIONS_H
#define HTSN_DB_CONFIG_VERSIONS_H

#include "db/db.h"

#include <time.h>

typedef struct {
    int id;
    char name[HTSN_MAX_STR];
    char device_id[HTSN_MAX_STR];
    time_t created_at;
    char payload[4096];
} htsn_config_version;

htsn_error htsn_db_config_version_add(htsn_db *db, const char *name,
                                      const char *device_id, const char *payload);
htsn_error htsn_db_config_version_get(htsn_db *db, int id, htsn_config_version *out);
typedef void (*htsn_db_config_version_cb)(const htsn_config_version *v, void *userdata);
void htsn_db_config_version_for_each(htsn_db *db, htsn_db_config_version_cb cb, void *userdata);
int htsn_db_config_version_count(htsn_db *db);

#endif
