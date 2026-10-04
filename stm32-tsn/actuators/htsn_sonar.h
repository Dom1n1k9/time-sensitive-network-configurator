/* HC-SR04 panning sonar + SG90 servo, ported from esp32-02 (htsn_sonar.c).
 * Same logic; transport is the TSN UDP link (htsn_net) instead of MQTT. */
#ifndef HTSN_SONAR_H
#define HTSN_SONAR_H

#include <stdint.h>

void htsn_sonar_init(const char *device_id);
void htsn_sonar_trigger(void);
const int16_t *htsn_sonar_map(int *n);
void htsn_sonar_set_angle(int deg);
int  htsn_sonar_active_angle(void);

#endif /* HTSN_SONAR_H */
