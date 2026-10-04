#ifndef HTSN_QOS_MANAGER_H
#define HTSN_QOS_MANAGER_H

#include "common/common.h"
#include "db/db.h"
#include "db/db_qos.h"
#include "mvc/event_bus.h"
#include "qos/qos.h"

#define HTSN_QOS_MANAGER_MODEL "qos"

typedef struct htsn_qos_manager htsn_qos_manager;

htsn_qos_manager *htsn_qos_manager_create(htsn_db *db, htsn_event_bus *bus);
void htsn_qos_manager_destroy(htsn_qos_manager *m);

htsn_error htsn_qos_manager_configure(htsn_qos_manager *m, const char *device_id,
                                            const htsn_qos_config_model *cfg);
htsn_error htsn_qos_manager_load_for_device(htsn_qos_manager *m, const char *device_id, htsn_qos_config *out);
htsn_error htsn_qos_manager_remove(htsn_qos_manager *m, const char *device_id);

#endif
