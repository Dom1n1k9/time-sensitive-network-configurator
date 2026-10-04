/* SSD1306 OLED + 4 buttons, ported from esp32-02 (htsn_display.c). */
#ifndef HTSN_DISPLAY_H
#define HTSN_DISPLAY_H

#include <stdbool.h>

void htsn_display_init(const char *device_id);
void htsn_display_tick(void);          /* call ~10 Hz: debounce buttons + refresh */
void htsn_display_status(const char *l1, const char *l2);
void htsn_display_set_hud(bool on);
void htsn_display_on_telemetry(const char *json);   /* HUD values from the CNC */
bool htsn_display_present(void);

void htsn_display_buttons(int *k1, int *k2, int *k3, int *k4);

/* Button press (rising, debounced) callback — registered by the app. */
typedef void (*htsn_display_btn_cb)(int n, void *ud);
void htsn_display_set_btn_cb(htsn_display_btn_cb cb, void *ud);

#endif /* HTSN_DISPLAY_H */
