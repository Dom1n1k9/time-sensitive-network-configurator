/* Two fixed PWM outputs (servo SG90 + buzzer), programmed via a small CMSIS
 * register shim. See pwm.c for why this avoids the Zephyr PWM driver. */
#ifndef WTSN_PWM_H
#define WTSN_PWM_H

#include <stdint.h>

/* Program the GPIO alternate-function mux + TIM1 (servo, 50 Hz) + TIM3
 * (buzzer, 1 kHz). Call once after the clocks are up. */
void wtsn_pwm_init(void);

/* Servo duty in microseconds (1000..2000 -> 0..180 deg). */
void wtsn_pwm_servo_us(uint16_t us);

/* Buzzer duty 0..100 (%). 0 = off. */
void wtsn_pwm_buzzer_duty(uint8_t pct);

#endif /* WTSN_PWM_H */
