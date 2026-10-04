#ifndef HTSN_SENSOR_H
#define HTSN_SENSOR_H

#include "common/common.h"

#include <time.h>

typedef enum {
    HTSN_SENSOR_TEMPERATURE = 0,
    HTSN_SENSOR_PRESSURE,
    HTSN_SENSOR_IMU,
    HTSN_SENSOR_DISTANCE,
    HTSN_SENSOR_GPIO
} htsn_sensor_type;

typedef struct {
    char device_id[HTSN_MAX_STR];
    char sensor_id[HTSN_MAX_STR];
    htsn_sensor_type type;
    char name[HTSN_MAX_STR];
    double value;
    char unit[32];
    bool healthy;
    time_t last_update;
} htsn_sensor;

const char *htsn_sensor_type_str(htsn_sensor_type t);
htsn_sensor_type htsn_sensor_type_parse(const char *s);

#endif
