/* Thin porting layer so the (ESP32-derived) actuator code compiles on the
 * STM32 without sprinkling vendor calls everywhere. Implemented in Board/. */
#ifndef WTSN_PORT_H
#define WTSN_PORT_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* Monotonic microsecond clock (DWT cycle counter). Same role as the ESP's
 * esp_timer_get_time(). */
uint64_t wtsn_now_us(void);

/* Busy-wait microsecond delay (DWT spin). Used for the HC-SR04 TRIG pulse. */
void wtsn_delay_us(uint32_t us);

/* Full system reset (same role as esp_restart()). */
void wtsn_reset(void);

/* Minimal logging (console/UART). Tag + format + optional args. */
#define LOGI(...) printf("[I] " __VA_ARGS__)
#define LOGW(...) printf("[W] " __VA_ARGS__)
#define LOGE(...) printf("[E] " __VA_ARGS__)

#endif /* WTSN_PORT_H */
