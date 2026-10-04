/* SSD1306 128x64 (I2C) display + 4 debounced buttons, ported from esp32-02
 * (htsn_display.c). Zephyr: I2C2 via the device model, buttons are DT GPIOs
 * (active-low handled by the GPIO spec), ticks run in the display thread.
 */
#include "htsn_display.h"

#include "htsn_config.h"
#include "htsn_port.h"
#include "htsn_net.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define HTSN_ACT DT_NODELABEL(htsn_actuators)
static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c2));

/* ---- SSD1306 register commands ---- */
#define SSD1306_ADDR      0x3C
#define SSD1306_CTRL_CMD  0x00
#define SSD1306_CTRL_DATA 0x40
#define C_DISP_OFF        0xAE
#define C_MEM_MODE        0x20
#define C_MEM_HORZ        0x00
#define C_COL_ADDR        0x21
#define C_PAGE_ADDR       0x22
#define C_START_LINE      0x40
#define C_CHARGE_PUMP     0x8D
#define C_CP_ON           0x14
#define C_MUX_RATIO       0xA8
#define C_MUX_64          0x3F
#define C_DISP_OFFSET     0xD3
#define C_SEG_REMAP       0xA1
#define C_COM_SCAN_DEC    0xC8
#define C_COMPIN          0xDA
#define C_COMPIN_64       0x12
#define C_CONTRAST        0x81
#define C_CONTRAST_VAL    0x7F
#define C_VCOMH           0xDB
#define C_VCOMH_VAL       0x30
#define C_CLOCKDIV        0xD5
#define C_CLOCKDIV_VAL    0x80
#define C_PRECHARGE       0xD9
#define C_PRECHARGE_VAL   0x22
#define C_SET_SCROLL      0x2E

#define SSD1306_BUF_SZ 1024

static bool    g_present = false;
static uint8_t g_addr    = SSD1306_ADDR;
static bool    g_sh1106  = false;
static uint8_t g_fb[SSD1306_BUF_SZ];

static char g_line1[24] = "htsn-tsn";
static char g_line2[24] = "linking...";

static bool   g_hud = true;
static float  g_hum = 0, g_press = 0, g_temp = 0;
static int    g_light = 0;

static const int64_t RENDER_INTERVAL_US = 10000000LL;   /* 10 s */
static uint64_t g_last_render_us = 0;
static uint8_t  g_fb_last[SSD1306_BUF_SZ];
static bool     g_fb_last_valid = false;

static htsn_display_btn_cb g_btn_cb = NULL;
static void *g_btn_ud = NULL;

/* ---- buttons (active-low; the DT GPIO spec reports 1 when pressed) ---- */
#define BTN_COUNT 4
static const struct gpio_dt_spec g_btn[BTN_COUNT] = {
	GPIO_DT_SPEC_GET(HTSN_ACT, btn1_gpios), GPIO_DT_SPEC_GET(HTSN_ACT, btn2_gpios),
	GPIO_DT_SPEC_GET(HTSN_ACT, btn3_gpios), GPIO_DT_SPEC_GET(HTSN_ACT, btn4_gpios),
};
static const char *g_btn_label[BTN_COUNT] = { "K1", "K2", "K3", "K4" };
static volatile uint32_t g_btn_levels = 0;
static int      g_btn_raw[BTN_COUNT];
static uint64_t g_btn_change_us[BTN_COUNT];
static const uint64_t BTN_DEBOUNCE_US = 30000;   /* 30 ms */

/* ---- I2C ---- */
static bool i2c_xfer(const uint8_t *buf, uint16_t len)
{
	return i2c_write_dt(i2c, buf, len) == 0;
}
static bool i2c_cmd(uint8_t cmd)
{
	uint8_t b[2] = { SSD1306_CTRL_CMD, cmd };
	return i2c_xfer(b, 2);
}
static bool ssd1306_present(void)
{
	if (i2c_probe(i2c, 0x3C) == 0) { g_addr = 0x3C; return true; }
	if (i2c_probe(i2c, 0x3D) == 0) { g_addr = 0x3D; return true; }
	return false;
}

/* Flush the whole framebuffer in HORIZONTAL addressing (linear 1024-byte GDDRAM). */
static void ssd1306_flush(void)
{
	int col_off = g_sh1106 ? 2 : 0;
	uint8_t b0[8 + 128];
	b0[0] = SSD1306_CTRL_CMD; b0[1] = C_PAGE_ADDR; b0[2] = 0x00; b0[3] = 0x07;
	b0[4] = C_COL_ADDR;       b0[5] = (uint8_t)col_off; b0[6] = (uint8_t)(col_off + 127);
	b0[7] = SSD1306_CTRL_DATA;
	memcpy(&b0[8], g_fb, 128);
	i2c_xfer(b0, sizeof(b0));
	for (int page = 1; page < 8; page++) {
		uint8_t b[129];
		b[0] = SSD1306_CTRL_DATA;
		memcpy(&b[1], &g_fb[page * 128], 128);
		i2c_xfer(b, sizeof(b));
	}
}

static void ssd1306_init_seq(void)
{
	i2c_cmd(C_DISP_OFF);
	i2c_cmd(C_MUX_RATIO); i2c_cmd(C_MUX_64);
	i2c_cmd(C_DISP_OFFSET); i2c_cmd(0x00);
	i2c_cmd(C_START_LINE | 0x00);
	i2c_cmd(C_COM_SCAN_DEC);
	i2c_cmd(C_SEG_REMAP);
	i2c_cmd(C_COMPIN); i2c_cmd(C_COMPIN_64);
	i2c_cmd(C_CONTRAST); i2c_cmd(C_CONTRAST_VAL);
	i2c_cmd(C_CLOCKDIV); i2c_cmd(C_CLOCKDIV_VAL);
	i2c_cmd(C_PRECHARGE); i2c_cmd(C_PRECHARGE_VAL);
	i2c_cmd(C_VCOMH); i2c_cmd(C_VCOMH_VAL);
	i2c_cmd(C_CHARGE_PUMP); i2c_cmd(C_CP_ON);
	i2c_cmd(C_MEM_MODE); i2c_cmd(C_MEM_HORZ);
	i2c_cmd(0xA4);   /* display from RAM */
	i2c_cmd(0xA6);   /* normal (not inverted) */
	i2c_cmd(C_SET_SCROLL);
	i2c_cmd(0xAF);   /* display on */
}

/* ---- tiny 5x7 font (ASCII 0x20..0x7E), bit0 = top row ---- */
static const uint8_t FONT5[96][5] = {
	{0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},
	{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
	{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
	{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
	{0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},
	{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
	{0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},
	{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
	{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
	{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
	{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
	{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
	{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
	{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
	{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
	{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
	{0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},
	{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
	{0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},
	{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
	{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
	{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
	{0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},
	{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
	{0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},
	{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
	{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
	{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
	{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
	{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x41},
	{0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x41,0x7F},
	{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
	{0x00,0x03,0x05,0x09,0x00},{0x20,0x54,0x54,0x54,0x78},
	{0x7F,0x28,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x28},
	{0x38,0x44,0x44,0x28,0x7F},{0x38,0x54,0x54,0x54,0x18},
	{0x00,0x08,0x7E,0x09,0x02},{0x0C,0x52,0x52,0x52,0x3E},
	{0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},
	{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
	{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x78,0x04,0x78},
	{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
	{0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x08,0x7C},
	{0x7C,0x04,0x04,0x04,0x00},{0x48,0x54,0x54,0x54,0x20},
	{0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},
	{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x38,0x40,0x3C},
	{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
	{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
	{0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},
	{0x02,0x01,0x02,0x04,0x02},{0xFF,0xFF,0xFF,0xFF,0xFF},
};

static void fb_clear(void) { memset(g_fb, 0, sizeof(g_fb)); }
static void fb_set_px(int x, int y)
{
	if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
	g_fb[(y >> 3) * 128 + x] |= (uint8_t)(1 << (y & 7));
}
static void draw_text_px(int x, int y, const char *text)
{
	if (!text) return;
	for (const char *p = text; *p; p++) {
		unsigned char ch = (unsigned char)*p;
		const uint8_t *glyph;
		if (ch == 0xB0) {
			static const uint8_t deg[5] = {0x06, 0x09, 0x09, 0x09, 0x06};
			glyph = deg;
		} else {
			if (ch < 0x20 || ch > 0x7E) ch = ' ';
			glyph = FONT5[ch - 0x20];
		}
		for (int c = 0; c < 5; c++)
			for (int b = 0; b < 7; b++)
				if (glyph[c] & (1 << b)) fb_set_px(x + c, y + b);
		x += 6;
		if (x + 5 > 127) break;
	}
}
static void fb_text(int col, int row, const char *text) { draw_text_px(col * 6, row * 8, text); }
static void fb_text_centered(int row, const char *text)
{
	int x = (128 - (int)strlen(text) * 6) / 2;
	if (x < 0) x = 0;
	draw_text_px(x, row * 8, text);
}

static void hud_render(void)
{
	char row[24];
	fb_clear();
	if (g_temp > -100) snprintf(row, sizeof(row), "T: %.1f \xB0C", g_temp);
	else snprintf(row, sizeof(row), "T: --");
	fb_text_centered(1, row);
	if (g_hum > 0) snprintf(row, sizeof(row), "H: %.0f %%", g_hum);
	else snprintf(row, sizeof(row), "H: --");
	fb_text_centered(3, row);
	if (g_press > 0) snprintf(row, sizeof(row), "P: %.0f hPa", g_press);
	else snprintf(row, sizeof(row), "P: --");
	fb_text_centered(5, row);
	if (g_light > 0) snprintf(row, sizeof(row), "L: %d lx", g_light);
	else snprintf(row, sizeof(row), "L: --");
	fb_text_centered(7, row);
}

/* ---- buttons ---- */
static int btn_read(int i) { return gpio_pin_get_dt(&g_btn[i]); }   /* 1 = pressed */

static void buttons_init(void)
{
	uint64_t now = htsn_now_us();
	for (int i = 0; i < BTN_COUNT; i++) {
		if (device_is_ready(g_btn[i].port)) {
			gpio_pin_configure_dt(&g_btn[i], GPIO_INPUT);
		}
		g_btn_raw[i] = btn_read(i);
		g_btn_change_us[i] = now;
	}
}

static void buttons_publish(void)
{
	int k1 = (g_btn_levels >> 0) & 1u, k2 = (g_btn_levels >> 1) & 1u;
	int k3 = (g_btn_levels >> 2) & 1u, k4 = (g_btn_levels >> 3) & 1u;
	htsn_net_publish_buttons(k1, k2, k3, k4);
}

static void buttons_tick(void)
{
	uint64_t now = htsn_now_us();
	for (int i = 0; i < BTN_COUNT; i++) {
		int raw = btn_read(i);
		if (raw != g_btn_raw[i]) { g_btn_raw[i] = raw; g_btn_change_us[i] = now; continue; }
		if (now - g_btn_change_us[i] < BTN_DEBOUNCE_US) continue;
		int committed = (g_btn_levels >> i) & 1u;
		if (committed == raw) continue;
		if (raw) g_btn_levels |= (1u << i); else g_btn_levels &= ~(1u << i);
		g_last_render_us = 0;   /* force a panel refresh for feedback */
		if (committed == 0 && raw == 1) {
			LOGI("button %d (%s) pressed\n", i + 1, g_btn_label[i]);
			if (g_btn_cb) g_btn_cb(i + 1, g_btn_ud);
		}
	}
}

/* ---- public API ---- */
void htsn_display_set_btn_cb(htsn_display_btn_cb cb, void *ud) { g_btn_cb = cb; g_btn_ud = ud; }

void htsn_display_buttons(int *k1, int *k2, int *k3, int *k4)
{
	if (k1) *k1 = (g_btn_levels >> 0) & 1u;
	if (k2) *k2 = (g_btn_levels >> 1) & 1u;
	if (k3) *k3 = (g_btn_levels >> 2) & 1u;
	if (k4) *k4 = (g_btn_levels >> 3) & 1u;
}

void htsn_display_status(const char *l1, const char *l2)
{
	if (l1) { strncpy(g_line1, l1, sizeof(g_line1) - 1); g_line1[sizeof(g_line1) - 1] = 0; }
	if (l2) { strncpy(g_line2, l2, sizeof(g_line2) - 1); g_line2[sizeof(g_line2) - 1] = 0; }
}
void htsn_display_set_hud(bool on) { g_hud = on; }
bool htsn_display_present(void) { return g_present; }

/* Minimal "key":number extractor (no JSON lib on the target). */
static float json_num(const char *json, const char *key, float def)
{
	char pat[24];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *k = strstr(json, pat);
	if (!k) return def;
	k = strchr(k, ':');
	return k ? (float)atof(k + 1) : def;
}

/* The CNC forwards esp32-01 sensor telemetry to the endpoint; the HUD shows it. */
void htsn_display_on_telemetry(const char *json)
{
	if (!json) return;
	g_temp  = json_num(json, "temp", -999.0f);
	g_hum   = json_num(json, "humidity", 0.0f);
	g_press = json_num(json, "pressure", 0.0f);
	g_light = (int)json_num(json, "light", 0);
	g_last_render_us = 0;   /* force a panel refresh */
}

void htsn_display_init(const char *device_id)
{
	(void)device_id;
	buttons_init();
	k_msleep(400);   /* let the panel power on */
	for (int attempt = 0; attempt < 2 && !g_present; attempt++) {
		if (attempt) k_msleep(150);
		if (ssd1306_present()) {
			ssd1306_init_seq();
			k_msleep(20);
			hud_render();
			ssd1306_flush();
			memcpy(g_fb_last, g_fb, SSD1306_BUF_SZ);
			g_fb_last_valid = true;
			g_present = true;
			g_last_render_us = htsn_now_us() - RENDER_INTERVAL_US;
			LOGI("SSD1306 found at 0x%02X (I2C2)\n", g_addr);
		}
	}
	if (!g_present) LOGW("no SSD1306 on I2C2 — display off, buttons still active\n");
}

void htsn_display_tick(void)
{
	buttons_tick();
	if ((htsn_now_us() % 1000000) < 100000) buttons_publish();   /* ~2 s cadence */

	if (!g_present) return;
	if (g_hud) hud_render();
	else {
		fb_clear();
		fb_text(0, 0, g_line1);
		fb_text(0, 1, g_line2);
		char tmp[24];
		snprintf(tmp, sizeof(tmp), "1:%d 2:%d 3:%d 4:%d",
		         (g_btn_levels >> 0) & 1u, (g_btn_levels >> 1) & 1u,
		         (g_btn_levels >> 2) & 1u, (g_btn_levels >> 3) & 1u);
		fb_text(0, 6, tmp);
		fb_text(0, 7, "stm32-tsn");
	}

	uint64_t now = htsn_now_us();
	if (now - g_last_render_us < RENDER_INTERVAL_US) return;
	if (g_fb_last_valid && memcmp(g_fb, g_fb_last, SSD1306_BUF_SZ) == 0) {
		g_last_render_us = now;
		return;
	}
	ssd1306_flush();
	memcpy(g_fb_last, g_fb, SSD1306_BUF_SZ);
	g_fb_last_valid = true;
	g_last_render_us = now;
}
