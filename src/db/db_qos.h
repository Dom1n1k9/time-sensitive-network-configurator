#ifndef HTSN_DB_QOS_H
#define HTSN_DB_QOS_H

#include "db/db.h"

typedef struct {
    char device_id[HTSN_MAX_STR];
    int priority;
    int traffic_class;
    int bandwidth_kbps;
    int latency_ms;
    int preemption;
} htsn_qos_config;

htsn_error htsn_db_qos_save(htsn_db *db, const htsn_qos_config *cfg);
htsn_error htsn_db_qos_load(htsn_db *db, const char *device_id, htsn_qos_config *out);
htsn_error htsn_db_qos_delete(htsn_db *db, const char *device_id);

#endif
