#ifndef HTSN_SENSOR_MANAGER_H
#define HTSN_SENSOR_MANAGER_H

#include "common/common.h"
#include "db/db.h"
#include "db/db_sensors.h"
#include "mvc/event_bus.h"
#include "sensors/sensor.h"

#define HTSN_SENSOR_MANAGER_MODEL "sensor"

typedef struct htsn_sensor_manager htsn_sensor_manager;

htsn_sensor_manager *htsn_sensor_manager_create(htsn_db *db, htsn_event_bus *bus);
void htsn_sensor_manager_destroy(htsn_sensor_manager *m);

htsn_error htsn_sensor_manager_upsert(htsn_sensor_manager *m, const htsn_sensor *s);
htsn_error htsn_sensor_manager_remove(htsn_sensor_manager *m, const char *dev, const char *sid);
void htsn_sensor_manager_for_each(htsn_sensor_manager *m,
                                  void (*cb)(const htsn_sensor *s, void *ud), void *ud);
size_t htsn_sensor_manager_count(htsn_sensor_manager *m);
htsn_error htsn_sensor_manager_auto_detect(htsn_sensor_manager *m, const char *device_id);

#endif
