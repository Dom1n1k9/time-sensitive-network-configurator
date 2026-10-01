/* SSD1306 OLED + 4 buttons, ported from esp32-02 (wtsn_display.c). */
#ifndef WTSN_DISPLAY_H
#define WTSN_DISPLAY_H

#include <stdbool.h>

void wtsn_display_init(const char *device_id);
void wtsn_display_tick(void);          /* call ~10 Hz: debounce buttons + refresh */
void wtsn_display_status(const char *l1, const char *l2);
void wtsn_display_set_hud(bool on);
void wtsn_display_on_telemetry(const char *json);   /* HUD values from the CNC */
bool wtsn_display_present(void);

void wtsn_display_buttons(int *k1, int *k2, int *k3, int *k4);

/* Button press (rising, debounced) callback — registered by the app. */
typedef void (*wtsn_display_btn_cb)(int n, void *ud);
void wtsn_display_set_btn_cb(wtsn_display_btn_cb cb, void *ud);

#endif /* WTSN_DISPLAY_H */
