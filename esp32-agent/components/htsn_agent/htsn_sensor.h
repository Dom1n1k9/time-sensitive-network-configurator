#ifndef HTSN_SENSOR_H
#define HTSN_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

#include "htsn_mqtt.h"
#include "htsn_wifimotion.h"

/* Sensor types matching the webgui schema (type column). */
enum {
    HTSN_SENSOR_TEMP   = 0,
    HTSN_SENSOR_PRESS  = 1,
    HTSN_SENSOR_IMU    = 2,
    HTSN_SENSOR_DIST   = 3,
    HTSN_SENSOR_GPIO   = 4,
};

/* Timer switch (actor) modes:
 *  0 off, 1 manual-on, 2 always-on, 3 timed-once, 4 interval,
 *  5 delayed-start, 6 auto/cyclic, 7 momentary trigger pulse (~300 ms,
 *  for edge-triggered relay/timer modules such as the Z-3807-M) */
#define TIMER_SWITCH_MODES 8

/* Init sensors + actor on this node. device_id is used on the MQTT payloads. */
void htsn_sensor_init(const char *device_id, htsn_mqtt *mq);

/* True once the sensor add-on board (BME280 on I2C) was detected on this node.
 * Used to auto-assign roles: the board with sensors = esp32-01, the other = relay. */
bool htsn_sensor_present(void);

/* Quick I2C probe (no full init) that checks whether a BME280 sits on the bus.
 * Called early (before MQTT) to auto-pick the device role. */
bool htsn_sensor_probe(void);

/* Periodic scan: read analog light, PIR motion and BME280, publish telemetry. */
void htsn_sensor_tick(void);

/* Actor: set the output switch. mode 0-6, returns previous mode. */
int htsn_sensor_actor_set(int mode);
int htsn_sensor_actor_get(void);
void htsn_sensor_actor_set_pin(void);

/* Value accessors for the micro:bit display panel. NULL-safe; 0 on missing data. */
int  htsn_sensor_light(void);
int  htsn_sensor_motion(void);
void htsn_sensor_last(float *temp_c, float *press_hpa, float *hum_pct,
                      int *light, int *pir, int *actor);

/* Motion buzzer (piezo) on GPIO25 - beeps when the PIR trips. */
void htsn_sensor_buzzer_init(void);

/* Public beep API (used by the micro:bit command pad for "identify"). */
void htsn_sensor_buzzer_beep(uint16_t freq_hz, uint32_t dur_ms);

#endif
