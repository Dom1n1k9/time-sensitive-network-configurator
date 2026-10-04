/* htsn-tsn pin map — Nucleo-F767ZI.
 *
 * SUPERSEDED: the physical wiring now lives in the Zephyr device-tree overlay
 *   boards/nucleo_f767zi.overlay  (node `htsn-actuators`)
 * which is the single source of truth (the C reads pins via GPIO_DT_SPEC_GET).
 * This file is kept only as a human-readable index of what is wired where.
 *
 *   servo (SG90, 50 Hz PWM)   PA8  = TIM1_CH1   (Board/pwm.c)
 *   sonar HC-SR04             PB3  TRIG (out)   PB4 ECHO (in)
 *   buzzer (PWM beep)         PB1  = TIM3_CH4   (Board/pwm.c)
 *   relay (digital out)       PB0
 *   OLED SSD1306 (I2C2)       PB10 SCL  PB11 SDA
 *   buttons (active-low)      PC13  PC14  PC15  PD0
 *   heartbeat LED (LD1)       PC8
 *
 * RESERVED (on-board LAN8742A PHY): PA1/PA2/PA7/PC1/PC4/PC5 — do not reuse.
 */
#ifndef HTSN_PINS_H
#define HTSN_PINS_H
#endif /* HTSN_PINS_H */
