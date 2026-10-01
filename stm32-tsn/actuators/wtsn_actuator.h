/* Relay + buzzer actuator (ported from esp32-02 wtsn_sensor.c actor path). */
#ifndef WTSN_ACTUATOR_H
#define WTSN_ACTUATOR_H

void wtsn_actuator_init(const char *device_id);
void wtsn_actuator_relay(int on);      /* 0 = off, 1 = energised */
void wtsn_actuator_beep(int ms);       /* beep for ~ms */
int  wtsn_actuator_relay_state(void);

#endif /* WTSN_ACTUATOR_H */
