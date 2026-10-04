/* Relay + buzzer actuator (ported from esp32-02 htsn_sensor.c actor path). */
#ifndef HTSN_ACTUATOR_H
#define HTSN_ACTUATOR_H

void htsn_actuator_init(const char *device_id);
void htsn_actuator_relay(int on);      /* 0 = off, 1 = energised */
void htsn_actuator_beep(int ms);       /* beep for ~ms */
int  htsn_actuator_relay_state(void);

#endif /* HTSN_ACTUATOR_H */
