#ifndef HTSN_DB_DEVICES_H
#define HTSN_DB_DEVICES_H

#include "db/db.h"
#include "device/device.h"

htsn_error htsn_db_device_upsert(htsn_db *db, const htsn_device *dev);
htsn_error htsn_db_device_delete(htsn_db *db, const char *id);
htsn_error htsn_db_device_get(htsn_db *db, const char *id, htsn_device *out);
typedef void (*htsn_db_device_cb)(const htsn_device *dev, void *userdata);
void htsn_db_device_for_each(htsn_db *db, htsn_db_device_cb cb, void *userdata);
void htsn_db_device_for_each_in_domain(htsn_db *db, const char *domain,
                                       htsn_db_device_cb cb, void *userdata);
htsn_error htsn_db_device_set_status(htsn_db *db, const char *id, htsn_device_status status);
htsn_error htsn_db_device_touch(htsn_db *db, const char *id, time_t seen);

#endif
