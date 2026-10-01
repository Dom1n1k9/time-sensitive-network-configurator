/* HC-SR04 panning sonar + SG90 servo, ported from esp32-02 (wtsn_sonar.c).
 * Same logic; transport is the TSN UDP link (wtsn_net) instead of MQTT. */
#ifndef WTSN_SONAR_H
#define WTSN_SONAR_H

#include <stdint.h>

void wtsn_sonar_init(const char *device_id);
void wtsn_sonar_trigger(void);
const int16_t *wtsn_sonar_map(int *n);
void wtsn_sonar_set_angle(int deg);
int  wtsn_sonar_active_angle(void);

#endif /* WTSN_SONAR_H */
