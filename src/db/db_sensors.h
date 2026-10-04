#ifndef HTSN_DB_SENSORS_H
#define HTSN_DB_SENSORS_H

#include "db/db.h"
#include "sensors/sensor.h"

htsn_error htsn_db_sensor_upsert(htsn_db *db, const htsn_sensor *s);
htsn_error htsn_db_sensor_delete(htsn_db *db, const char *device_id, const char *sensor_id);
typedef void (*htsn_db_sensor_cb)(const htsn_sensor *s, void *userdata);
void htsn_db_sensor_for_each(htsn_db *db, htsn_db_sensor_cb cb, void *userdata);

#endif
