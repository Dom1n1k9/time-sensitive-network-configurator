#ifndef WTSN_DISPLAY_H
#define WTSN_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "wtsn_mqtt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SSD1306 128x64 I2C OLED on the actor board (esp32-02) + 4 button inputs.
 *
 * Wiring (matches the module with SCL/SDA/VCC/GND + K1..K4):
 *   VCC  -> 3V3
 *   GND  -> GND
 *   SCL  -> GPIO22  (same I2C bus as the sensor add-on: SDA21/SCL22)
 *   SDA  -> GPIO21
 *   K1..K4 -> GPIOs (see WTSN_BTN_* below), button other side to GND
 *
 * The display renders node status / live telemetry and each button publishes
 * a labelled event on tsn/sensors/<id> plus a short "button.*" ack so the
 * GUI monitor shows what happened. Buttons are also aggregated into the 2 s
 * telemetry tick (btn1..btn4 sensors) so the GUI Sensors page shows them.
 */

/* ---- pin map (defaults; override with -D at build if needed) ---- */
#ifndef WTSN_SSD1306_SDA
#define WTSN_SSD1306_SDA GPIO_NUM_21
#endif
#ifndef WTSN_SSD1306_SCL
#define WTSN_SSD1306_SCL GPIO_NUM_22
#endif
#ifndef WTSN_BTN1_GPIO
#define WTSN_BTN1_GPIO GPIO_NUM_32
#endif
#ifndef WTSN_BTN2_GPIO
#define WTSN_BTN2_GPIO GPIO_NUM_33
#endif
#ifndef WTSN_BTN3_GPIO
#define WTSN_BTN3_GPIO GPIO_NUM_34
#endif
#ifndef WTSN_BTN4_GPIO
#define WTSN_BTN4_GPIO GPIO_NUM_35
#endif

/* ---- public API ---- */

/* Init the OLED (if present) and the four buttons. safe to call always;
 * if no SSD1306 answers on I2C the display part simply stays blank. */
void wtsn_display_init(const char *device_id, wtsn_mqtt *mq);

/* Set arbitrary 2-line status (label + value). */
void wtsn_display_status(const char *line1, const char *line2);

/* Per-period update: refresh the rendered text + aggregate button state. */
void wtsn_display_tick(void);

/* Feed a received MQTT message (topic + payload) to the display so it can
 * parse esp32-01's telemetry (temp/hum/press/light/pir) for the HUD. */
void wtsn_display_on_telemetry(const char *topic, const char *payload);

/* Set the OLED to the 4-corner sensor HUD mode (temp/hum/press/motion). */
void wtsn_display_set_hud(bool enable);

/* True when the HUD layout is active (vs. the 2-line status text). */
bool wtsn_display_hud(void);

/* Switch panel controller: false = SSD1306 (128 segs), true = SH1106
 * (132 segs, 2 invisible -> 2-col offset). Re-inits + re-renders. Returns
 * true when the OLED is present. */
bool wtsn_display_set_controller(bool sh1106);

/* True when the SH1106 controller mode is active. */
bool wtsn_display_controller_sh1106(void);

/* Fill the whole panel with a test pattern (0xFF lit / 0x00 off / 0xAA stripes)
 * and flush. Pure I2C transport check for debugging. Also freezes the frame. */
void wtsn_display_fill(uint8_t pattern);

/* Freeze (hold the current frame) or resume normal rendering. */
void wtsn_display_freeze(bool on);

/* Decisive mapping test: re-init + fill 4 solid quadrants (TL+BR lit) + freeze. */
void wtsn_display_quad(void);

/* Combined row+column mapping test: 4 horizontal bars, each left-half lit. */
void wtsn_display_bars(void);

/* Render the current content and log the framebuffer as an ASCII bitmap to
 * the serial console — for inspecting the pixels we compute. */
void wtsn_display_dump(void);

/* Read the panel's own GDDRAM back over I2C and log it as an ASCII bitmap.
 * Compare against wtsn_display_dump() to see if our data actually landed. */
void wtsn_display_dump_panel(void);

/* Fill with 0xFF, flush, then read the GDDRAM back over I2C and log it —
 * definitive check for whether the panel stores our data. */
void wtsn_display_readback(void);

/* Register a callback invoked (in the display tick task) whenever a button is
 * pressed. main.c maps K1..K4 to concrete actions (relay toggle, status,
 * page, identify). */
typedef void (*wtsn_display_btn_cb)(int btn, void *ud);
void wtsn_display_set_btn_cb(wtsn_display_btn_cb cb, void *ud);

/* True when an SSD1306 was actually found on the bus. */
bool wtsn_display_present(void);

/* Last pressed button (1..4, 0 = none) since the last tick. */
int wtsn_display_btn_last(void);

/* Accessors so telemetry callers can report btn state within the sensor JSON. */
bool wtsn_display_btn(int n);            /* 1..4, staggered latch */
void wtsn_display_buttons(int *k1, int *k2, int *k3, int *k4);

#ifdef __cplusplus
}
#endif

#endif /* WTSN_DISPLAY_H */
