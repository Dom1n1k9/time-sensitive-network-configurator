#ifndef HTSN_DEVICE_MANAGER_H
#define HTSN_DEVICE_MANAGER_H

#include "common/common.h"
#include "db/db.h"
#include "device/device.h"
#include "mvc/event_bus.h"
#include "plugin/plugin_manager.h"

typedef struct htsn_device_manager htsn_device_manager;

htsn_device_manager *htsn_device_manager_create(htsn_db *db, htsn_event_bus *bus,
                                                 htsn_plugin_manager *plugins);
void htsn_device_manager_destroy(htsn_device_manager *m);

htsn_error htsn_device_manager_upsert(htsn_device_manager *m, const htsn_device *dev);
htsn_error htsn_device_manager_remove(htsn_device_manager *m, const char *id);
const htsn_device *htsn_device_manager_get(htsn_device_manager *m, const char *id);
size_t htsn_device_manager_count(htsn_device_manager *m);
void htsn_device_manager_for_each(htsn_device_manager *m,
                                  void (*cb)(const htsn_device *dev, void *ud), void *ud);

void htsn_device_manager_mark_offline_after(htsn_device_manager *m, time_t threshold);
void htsn_device_manager_restore(htsn_device_manager *m);
htsn_error htsn_device_manager_discover_once(htsn_device_manager *m);
htsn_error htsn_device_manager_record_heartbeat(htsn_device_manager *m, const char *id);
htsn_error htsn_device_manager_set_domain(htsn_device_manager *m, const char *id,
                                        const char *domain);
htsn_error htsn_device_manager_set_health(htsn_device_manager *m, const char *id,
                                        htsn_device_status status);

#endif
