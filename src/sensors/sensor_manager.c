#include "sensors/sensor_manager.h"

#include "db/db_sensors.h"

#include "common/str_util.h"

#include "common/log.h"
#include "mvc/model.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SENSORS 1024

struct htsn_sensor_manager {
    htsn_db *db;
    htsn_event_bus *bus;
    htsn_model model;
    htsn_sensor sensors[MAX_SENSORS];
    size_t count;
};

static void collect(const htsn_sensor *s, void *ud) {
    htsn_sensor_manager *m = (htsn_sensor_manager *)ud;
    if (m->count < MAX_SENSORS) m->sensors[m->count++] = *s;
}

htsn_sensor_manager *htsn_sensor_manager_create(htsn_db *db, htsn_event_bus *bus) {
    if (!db || !bus) return NULL;
    htsn_sensor_manager *m = calloc(1, sizeof(htsn_sensor_manager));
    if (!m) return NULL;
    m->db = db;
    m->bus = bus;
    htsn_model_init(&m->model, HTSN_SENSOR_MANAGER_MODEL, bus);
    htsn_db_sensor_for_each(db, collect, m);
    return m;
}

void htsn_sensor_manager_destroy(htsn_sensor_manager *m) {
    free(m);
}

size_t htsn_sensor_manager_count(htsn_sensor_manager *m) {
    return m ? m->count : 0;
}

void htsn_sensor_manager_for_each(htsn_sensor_manager *m,
                                  void (*cb)(const htsn_sensor *s, void *ud), void *ud) {
    if (!m || !cb) return;
    for (size_t i = 0; i < m->count; i++) cb(&m->sensors[i], ud);
}

htsn_error htsn_sensor_manager_upsert(htsn_sensor_manager *m, const htsn_sensor *s) {
    if (!m || !s) return HTSN_ERR_INVALID_ARG;
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->sensors[i].device_id, s->device_id) == 0 &&
            strcmp(m->sensors[i].sensor_id, s->sensor_id) == 0) {
            m->sensors[i] = *s;
            htsn_db_sensor_upsert(m->db, s);
            htsn_model_notify(&m->model, "changed");
            return HTSN_OK;
        }
    }
    if (m->count >= MAX_SENSORS) return HTSN_ERR_BUSY;
    m->sensors[m->count++] = *s;
    htsn_db_sensor_upsert(m->db, s);
    htsn_model_notify(&m->model, "changed");
    return HTSN_OK;
}

htsn_error htsn_sensor_manager_remove(htsn_sensor_manager *m, const char *dev, const char *sid) {
    if (!m || !dev || !sid) return HTSN_ERR_INVALID_ARG;
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->sensors[i].device_id, dev) == 0 &&
            strcmp(m->sensors[i].sensor_id, sid) == 0) {
            memmove(&m->sensors[i], &m->sensors[i+1], (m->count - i - 1) * sizeof(htsn_sensor));
            m->count--;
            htsn_db_sensor_delete(m->db, dev, sid);
            htsn_model_notify(&m->model, "changed");
            return HTSN_OK;
        }
    }
    return HTSN_ERR_NOT_FOUND;
}

static void make_sensor(const char *device_id, const char *sid, htsn_sensor_type type,
                        const char *name, const char *unit, htsn_sensor *out) {
    memset(out, 0, sizeof(*out));
    htsn_strlcpy(out->device_id, device_id, sizeof(out->device_id));
    htsn_strlcpy(out->sensor_id, sid, sizeof(out->sensor_id));
    out->type = type;
    htsn_strlcpy(out->name, name, sizeof(out->name));
    htsn_strlcpy(out->unit, unit, sizeof(out->unit));
    out->value = 0.0;
    out->healthy = true;
    out->last_update = time(NULL);
}

htsn_error htsn_sensor_manager_auto_detect(htsn_sensor_manager *m, const char *device_id) {
    if (!m || !device_id) return HTSN_ERR_INVALID_ARG;
    htsn_sensor s;
    make_sensor(device_id, "temp1", HTSN_SENSOR_TEMPERATURE, "Ambient Temperature", "degC", &s);
    htsn_sensor_manager_upsert(m, &s);
    make_sensor(device_id, "press1", HTSN_SENSOR_PRESSURE, "Ambient Pressure", "hPa", &s);
    htsn_sensor_manager_upsert(m, &s);
    make_sensor(device_id, "imu1", HTSN_SENSOR_IMU, "6-axis IMU", "g", &s);
    htsn_sensor_manager_upsert(m, &s);
    make_sensor(device_id, "dist1", HTSN_SENSOR_DISTANCE, "Ultrasonic Distance", "cm", &s);
    htsn_sensor_manager_upsert(m, &s);
    make_sensor(device_id, "gpio1", HTSN_SENSOR_GPIO, "GPIO Input", "", &s);
    htsn_sensor_manager_upsert(m, &s);
    htsn_log(HTSN_LOG_INFO, "auto-detected sensors for %s", device_id);
    return HTSN_OK;
}
