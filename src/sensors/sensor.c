#include "sensors/sensor.h"

#include <string.h>

const char *htsn_sensor_type_str(htsn_sensor_type t) {
    switch (t) {
    case HTSN_SENSOR_TEMPERATURE: return "temperature";
    case HTSN_SENSOR_PRESSURE: return "pressure";
    case HTSN_SENSOR_IMU: return "imu";
    case HTSN_SENSOR_DISTANCE: return "distance";
    case HTSN_SENSOR_GPIO: return "gpio";
    default: return "unknown";
    }
}

htsn_sensor_type htsn_sensor_type_parse(const char *s) {
    if (!s) return HTSN_SENSOR_TEMPERATURE;
    if (strcmp(s, "temperature") == 0) return HTSN_SENSOR_TEMPERATURE;
    if (strcmp(s, "pressure") == 0) return HTSN_SENSOR_PRESSURE;
    if (strcmp(s, "imu") == 0) return HTSN_SENSOR_IMU;
    if (strcmp(s, "distance") == 0) return HTSN_SENSOR_DISTANCE;
    if (strcmp(s, "gpio") == 0) return HTSN_SENSOR_GPIO;
    return HTSN_SENSOR_TEMPERATURE;
}
