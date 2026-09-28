/* Actor-board display + buttons.
 *
 * A small, self-contained SSD1306 128x64 (I2C) driver and four debounced
 * push-buttons. Used on the actor board (esp32-02) to render the node status
 * and let physical buttons publish labelled MQTT events.
 *
 * No external component is required: the driver talks to the same I2C master
 * the sensor add-on already initialises (SDA=21, SCL=22, 100 kHz) and only
 * renders if an SSD1306 actually answers.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

#include "wtsn_display.h"
#include "wtsn_mqtt.h"
#include "wtsn_json.h"

static const char *TAG = "display";

/* ---- SSD1306 register commands ---- */
#define SSD1306_ADDR   0x3C
#define SSD1306_CTRL_CMD 0x00
#define SSD1306_CTRL_DATA 0x40
#define SSD1306_CMD_DISPLAY_OFF     0xAE
#define SSD1306_CMD_DISPLAY_ON      0xAF
#define SSD1306_CMD_SET_MEM_MODE    0x20
#define SSD1306_CMD_MEM_HORZ        0x00
#define SSD1306_CMD_MEM_PAGE        0x02
#define SSD1306_CMD_SET_COL_ADDR    0x21
#define SSD1306_CMD_SET_PAGE_ADDR   0x22
#define SSD1306_CMD_SET_START_LINE  0x40
#define SSD1306_CMD_CHARGE_PUMP     0x8D
#define SSD1306_CMD_CP_ON           0x14
#define SSD1306_CMD_SET_MUX_RATIO   0xA8
#define SSD1306_CMD_MUX_64          0x3F
#define SSD1306_CMD_DISP_OFFSET     0xD3
#define SSD1306_CMD_SEG_REMAP       0xA1
#define SSD1306_CMD_SET_COM_SCAN_DEC 0xC8
#define SSD1306_CMD_SET_COMPIN      0xDA
#define SSD1306_CMD_COMPIN_64       0x12
#define SSD1306_CMD_SET_CONTRAST    0x81
#define SSD1306_CMD_CONTRAST        0x7F
#define SSD1306_CMD_SET_VCOMH       0xDB
#define SSD1306_CMD_VCOMH_1_150     0x30
#define SSD1306_CMD_CLOCKDIV        0xD5
#define SSD1306_CMD_CLOCKDIV_VAL    0x80
#define SSD1306_CMD_PRECHARGE       0xD9
#define SSD1306_CMD_PRECHARGE_VAL   0x22
#define SSD1306_CMD_SET_SCROLL      0x2E   /* deactivate scroll */

/* 128 * 64 / 8 = 1024 bytes of page-mapped display RAM */
#define SSD1306_BUF_SZ 1024

static bool g_present = false;
static uint8_t g_i2c_found = 0;   /* actual SSD1306 address ACKed on the bus */
static bool g_sh1106 = false;     /* SH1106 panel (132 segs, 2 invisible) vs SSD1306 */
static bool g_freeze = false;     /* hold the current frame (test patterns) */
static int64_t g_init_us = 0;     /* when we first looked for the panel */
static int64_t g_found_us = 0;    /* when the panel first answered */
static uint8_t g_fb[SSD1306_BUF_SZ];

static char g_dev_id[16] = "esp32-02";
static wtsn_mqtt *g_mq = NULL;

static char g_line1[24] = "WTSN node";
static char g_line2[24] = "connecting...";

static int64_t g_last_pub_us = 0;

/* Info-panel refresh policy: the display is a low-frequency status panel, not
 * a live scope. Re-render at most every RENDER_INTERVAL_US, and only push a
 * flush when the framebuffer actually changed — otherwise the panel flickers
 * on every tick. A button press forces an immediate refresh for feedback. */
static int64_t g_last_render_us = 0;
static const int64_t RENDER_INTERVAL_US = 10000000LL;   /* 10 s */
static uint8_t g_fb_last[SSD1306_BUF_SZ];               /* last pushed frame */
static bool g_fb_last_valid = false;

/* ---- telemetry HUD (values from esp32-01 over tsn/sensors) ---- */
static bool g_hud = true;                      /* 4-corner HUD vs 2-line text */
static float g_hum = 0, g_press = 0, g_temp = 0;
static int   g_pir = 0, g_light = 0;
static int64_t g_last_telem_us = -1;           /* when we last saw telemetry */

/* ---- button action callback ---- */
static wtsn_display_btn_cb g_btn_cb = NULL;
static void *g_btn_ud = NULL;

/* ---- buttons ---- */
#define BTN_COUNT 4
static const gpio_num_t g_btn_gpio[BTN_COUNT] = {
    WTSN_BTN1_GPIO, WTSN_BTN2_GPIO, WTSN_BTN3_GPIO, WTSN_BTN4_GPIO,
};
/* Active-low (pressed = GND). */
static volatile uint32_t g_btn_levels = 0;      /* debounced 0/1 per button */
static volatile uint32_t g_btn_rising = 0;      /* level rise seen since last tick */
static int g_prev_btn[BTN_COUNT];
static int g_btn_raw[BTN_COUNT];                /* last observed raw level (1=pressed) */
static int64_t g_btn_change_us[BTN_COUNT];      /* when the raw level last changed */
static const int64_t BTN_DEBOUNCE_US = 30000;   /* 30 ms stable before we commit */
static bool g_btn_inited = false;               /* buttons_tick must not run before buttons_init */
static char g_btn_label[BTN_COUNT][6] = {"K1", "K2", "K3", "K4"};
static bool g_btn_enabled = true;               /* disabled if the OLED is present for K3/K4 without pullup */

/* ===================================================================== */
/* SDD1306 helpers (only used when g_present)                            */
/* ===================================================================== */

static inline uint8_t ssd1306_addr(void) { return g_i2c_found ? g_i2c_found : SSD1306_ADDR; }

static bool i2c_write_cmd(uint8_t cmd) {
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (ssd1306_addr() << 1) | I2C_MASTER_WRITE, 1);
    i2c_master_write_byte(c, SSD1306_CTRL_CMD, 1);
    i2c_master_write_byte(c, cmd, 1);
    i2c_master_stop(c);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, c, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(c);
    return (err == ESP_OK);
}

/* Write the whole framebuffer using HORIZONTAL addressing mode (0x20 0x00).
 *
 * In horizontal mode the X/Y pointer auto-advances the column and wraps to the
 * next page, so one linear 1024-byte stream fills the entire panel. We set the
 * page range (0x22) and column range (0x21) once, then stream the framebuffer
 * in 128-byte I2C chunks (the pointer keeps advancing across transactions).
 *
 * This is the addressing the panel reliably honours. The previous per-page
 * approach used the 0xB0+page command, which is only valid on SH1106 clones —
 * on a real SSD1306 the page command is 0x80+page, so 0xB0 was ignored and
 * every page of data landed in the wrong place, turning any column-varying
 * image into fuzz while uniform fills (0xFF / 0xAA) still looked fine.
 * 128-byte chunks also stay well under the I2C transfer timeout. */
static void ssd1306_flush(void) {
    int col_off = g_sh1106 ? 2 : 0;   /* SH1106 has 132 segs, first 2 invisible */
    static int errs = 0;

    /* Set page + column ranges (this also positions the pointer at page 0,
     * column col_off), then stream the first 128 bytes (page 0). */
    i2c_cmd_handle_t c0 = i2c_cmd_link_create();
    i2c_master_start(c0);
    i2c_master_write_byte(c0, (ssd1306_addr() << 1) | I2C_MASTER_WRITE, 1);
    i2c_master_write_byte(c0, SSD1306_CTRL_CMD, 1);
    i2c_master_write_byte(c0, 0x22, 1);                    /* page address range */
    i2c_master_write_byte(c0, 0x00, 1);                    /*   page start */
    i2c_master_write_byte(c0, 0x07, 1);                    /*   page end   */
    i2c_master_write_byte(c0, 0x21, 1);                    /* column address range */
    i2c_master_write_byte(c0, (uint8_t)col_off, 1);        /*   column start */
    i2c_master_write_byte(c0, (uint8_t)(col_off + 127), 1);/*   column end   */
    i2c_master_write_byte(c0, SSD1306_CTRL_DATA, 1);       /* data mode */
    i2c_master_write(c0, g_fb, 128, 1);                    /* page 0 */
    i2c_master_stop(c0);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, c0, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(c0);
    if (err != ESP_OK) {
        errs++;
        ESP_LOGE(TAG, "flush range+page0 FAILED err=0x%x", err);
    }

    /* Pages 1..7: data only — the pointer auto-advances from the last byte. */
    for (int page = 1; page < 8; page++) {
        i2c_cmd_handle_t c = i2c_cmd_link_create();
        i2c_master_start(c);
        i2c_master_write_byte(c, (ssd1306_addr() << 1) | I2C_MASTER_WRITE, 1);
        i2c_master_write_byte(c, SSD1306_CTRL_DATA, 1);
        i2c_master_write(c, &g_fb[page * 128], 128, 1);
        i2c_master_stop(c);
        err = i2c_master_cmd_begin(I2C_NUM_0, c, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(c);
        if (err != ESP_OK) {
            errs++;
            if (errs == 1 || (errs % 25) == 0)
                ESP_LOGE(TAG, "flush page %d FAILED err=0x%x (cum %d)", page, err, errs);
        }
    }
}

static int ssd1306_init_seq(void) {
    int ok = 0;
#define W(cmd) do { if (i2c_write_cmd(cmd)) ok++; \
                    else ESP_LOGE(TAG, "init 0x%02X FAILED", (cmd)); } while (0)
    W(SSD1306_CMD_DISPLAY_OFF);
    W(SSD1306_CMD_SET_MUX_RATIO); W(SSD1306_CMD_MUX_64);
    W(SSD1306_CMD_DISP_OFFSET);   W(0x00);
    W(SSD1306_CMD_SET_START_LINE | 0x00);
    W(SSD1306_CMD_SET_COM_SCAN_DEC);
    W(SSD1306_CMD_SEG_REMAP);
    W(SSD1306_CMD_SET_COMPIN);    W(SSD1306_CMD_COMPIN_64);
    W(SSD1306_CMD_SET_CONTRAST);  W(SSD1306_CMD_CONTRAST);
    W(SSD1306_CMD_CLOCKDIV);      W(SSD1306_CMD_CLOCKDIV_VAL);
    W(SSD1306_CMD_PRECHARGE);     W(SSD1306_CMD_PRECHARGE_VAL);
    W(SSD1306_CMD_SET_VCOMH);     W(SSD1306_CMD_VCOMH_1_150);
    W(SSD1306_CMD_CHARGE_PUMP);   W(SSD1306_CMD_CP_ON);
    /* HORIZONTAL addressing mode (0x20 0x00) — matches ssd1306_flush(), which
     * streams the framebuffer linearly. 0xA4 = display from RAM (not
     * "entire-on"), 0xA6 = normal (non-inverted) — both per the reference
     * init so the panel shows our GDDRAM content correctly. */
    W(SSD1306_CMD_SET_MEM_MODE);  W(SSD1306_CMD_MEM_HORZ);
    W(0xA4);                      /* entire-display off, resume to RAM */
    W(0xA6);                      /* normal display (not inverted) */
    W(SSD1306_CMD_SET_SCROLL);
    W(SSD1306_CMD_DISPLAY_ON);
#undef W
    return ok;
}

/* Check an address ACKs on the I2C bus: start + address-byte(with ACK check)
 * + stop. This is the reliable way to probe with the legacy I2C driver; the
 * zero-length i2c_master_write_to_device() is not dependable here. */
static bool i2c_addr_acks(uint8_t addr) {
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (uint8_t)((addr << 1) | I2C_MASTER_WRITE), 1);
    i2c_master_stop(c);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, c, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(c);
    return (err == ESP_OK);
}

/* Check the SSD1306 answers NAK-free at its address on the I2C bus.
 * Some clones answer at 0x3D instead of the usual 0x3C, so probe both. */
static bool ssd1306_present(void) {
    if (i2c_addr_acks(0x3C)) {
        g_i2c_found = 0x3C;
        return true;
    }
    if (i2c_addr_acks(0x3D)) {
        g_i2c_found = 0x3D;
        return true;
    }
    return false;
}

/* Log every address that ACKs on the display I2C bus, to help debug why the
 * OLED is not detected. */
static void ssd1306_scan_bus(void) {
    char buf[64] = "";
    int n = 0;
    for (int a = 0x04; a < 0x78; a++) {
        if (i2c_addr_acks((uint8_t)a)) {
            n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%s0x%02X",
                          (n ? "," : ""), a);
        }
    }
    ESP_LOGI(TAG, "I2C scan on SDA%d SCL%d: %s", (int)WTSN_SSD1306_SDA,
             (int)WTSN_SSD1306_SCL, n ? buf : "(no devices)");
}

/* ---------------------------------------------------------------- */
/* Tiny 5x7 font (ASCII 0x20..0x7E)                                 */
/* ---------------------------------------------------------------- */
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

static void fb_set_px(int x, int y) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    g_fb[(y >> 3) * 128 + x] |= (uint8_t)(1 << (y & 7));
}

static void draw_text_px(int x, int y, const char *text) {
    if (!text) return;
    for (const char *p = text; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        const uint8_t *glyph;
        if (ch == 0xB0) {                 /* degree sign (not in the ASCII font) */
            static const uint8_t deg[5] = {0x06, 0x09, 0x09, 0x09, 0x06};
            glyph = deg;
        } else {
            if (ch < 0x20 || ch > 0x7E) ch = ' ';
            glyph = FONT5[ch - 0x20];
        }
        for (int c = 0; c < 5; c++) {
            uint8_t line = glyph[c];
            /* FONT5 stores each glyph column with bit 0 = top row, so bit b
             * maps straight down to row y+b. (The old y+6-b flipped every
             * character vertically, which is why text looked like noise.) */
            for (int b = 0; b < 7; b++) {
                if (line & (1 << b)) fb_set_px(x + c, y + b);
            }
        }
        x += 6;
        if (x + 5 > 127) break;
    }
}

static void fb_text(int col, int row, const char *text) {
    draw_text_px(col * 6, row * 8, text);
}

/* Draw a string horizontally centred on the 128 px panel at the given row. */
static void fb_text_centered(int row, const char *text) {
    int x = (128 - (int)strlen(text) * 6) / 2;
    if (x < 0) x = 0;
    draw_text_px(x, row * 8, text);
}

/* ===================================================================== */
/* Public API                                                             */
/* ===================================================================== */

static bool display_probe_init(void);   /* probe + init, returns true when found */
static void hud_render(void);           /* render the 4-corner sensor HUD */

bool wtsn_display_present(void) { return g_present; }

void wtsn_display_status(const char *line1, const char *line2) {
    if (line1) { strncpy(g_line1, line1, sizeof(g_line1) - 1); g_line1[sizeof(g_line1) - 1] = 0; }
    if (line2) { strncpy(g_line2, line2, sizeof(g_line2) - 1); g_line2[sizeof(g_line2) - 1] = 0; }
}

/* Publish button state + press events as sensors so the GUI sees them. */
static void buttons_publish(void) {
    if (!g_mq) return;
    int k1, k2, k3, k4;
    wtsn_display_buttons(&k1, &k2, &k3, &k4);
    char buf[300];
    size_t m = (size_t)snprintf(buf, sizeof(buf) - 1,
             "{\"id\":\"%s\",\"ts\":%lld,\"sensors\":[",
             g_dev_id, (long long)time(NULL));
    char sub[96];
    snprintf(sub, sizeof(sub),
             "{\"sensor_id\":\"btn1\",\"type\":4,\"value\":%d,\"unit\":\"\",\"healthy\":1}",
             k1);
    m += (size_t)snprintf(buf + m, sizeof(buf) - m, "%s", sub);
    snprintf(sub, sizeof(sub),
             "{\"sensor_id\":\"btn2\",\"type\":4,\"value\":%d,\"unit\":\"\",\"healthy\":1}",
             k2);
    m += (size_t)snprintf(buf + m, sizeof(buf) - m, ",%s", sub);
    snprintf(sub, sizeof(sub),
             "{\"sensor_id\":\"btn3\",\"type\":4,\"value\":%d,\"unit\":\"\",\"healthy\":1}",
             k3);
    m += (size_t)snprintf(buf + m, sizeof(buf) - m, ",%s", sub);
    snprintf(sub, sizeof(sub),
             "{\"sensor_id\":\"btn4\",\"type\":4,\"value\":%d,\"unit\":\"\",\"healthy\":1}",
             k4);
    m += (size_t)snprintf(buf + m, sizeof(buf) - m, ",%s]}", sub);
    (void)m;
    wtsn_mqtt_publish(g_mq, "tsn/sensors", buf);
}

static void buttons_init(void) {
    /* 34/35 have no internal pull-up on classic ESP32: use external 10k or
     * rely on the module's own pull. We configure pull-up where available and
     * debounce so a floating line can't register as a press. */
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < BTN_COUNT; i++) {
        gpio_config_t io = {0};
        io.pin_bit_mask = (1ULL << g_btn_gpio[i]);
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_config(&io);
        g_prev_btn[i] = -1;
        /* Seed the raw state from the current level so boot isn't seen as a
         * press, and start the debounce window now. */
        g_btn_raw[i] = (gpio_get_level(g_btn_gpio[i]) == 0) ? 1 : 0;
        g_btn_change_us[i] = now;
    }
    g_btn_inited = true;
}

static void buttons_tick(void) {
    if (!g_btn_inited) return;   /* GPIOs not configured yet — don't sample */
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < BTN_COUNT; i++) {
        /* active low: pressed = 0 */
        int raw = (gpio_get_level(g_btn_gpio[i]) == 0) ? 1 : 0;
        if (raw != g_btn_raw[i]) {
            /* Level just changed: restart the debounce window, don't commit
             * yet. This is what stops a floating input (K3/K4 on the
             * input-only GPIOs 34/35) from flickering the HUD row or
             * auto-toggling the display mode. */
            g_btn_raw[i] = raw;
            g_btn_change_us[i] = now;
            continue;
        }
        if (now - g_btn_change_us[i] < BTN_DEBOUNCE_US) continue;
        int committed = (g_btn_levels >> i) & 1u;
        if (committed == raw) continue;   /* already in the committed state */
        /* Commit the new stable level. */
        if (raw) g_btn_levels |= (1u << i);
        else g_btn_levels &= ~(1u << i);
        g_prev_btn[i] = raw;
        g_last_render_us = 0;   /* force an immediate panel refresh for feedback */
        if (committed == 0 && raw == 1) {
            g_btn_rising |= (1u << i);
            ESP_LOGI(TAG, "button %d (%s) pressed", i + 1, g_btn_label[i]);
            char topic[64], payload[96];
            /* labelled event so the monitor shows which physical button */
            snprintf(topic, sizeof(topic), "tsn/button/%s/%s", g_dev_id, g_btn_label[i]);
            snprintf(payload, sizeof(payload), "{\"id\":\"%s\",\"button\":\"%s\",\"n\":%d}",
                     g_dev_id, g_btn_label[i], i + 1);
            if (g_mq) wtsn_mqtt_publish(g_mq, topic, payload);
            /* also ack so a GUI ping handler can show it */
            snprintf(topic, sizeof(topic), "tsn/ack/%s", g_dev_id);
            snprintf(payload, sizeof(payload), "{\"id\":\"%s\",\"ok\":true,\"button\":\"%s\"}",
                     g_dev_id, g_btn_label[i]);
            if (g_mq) wtsn_mqtt_publish(g_mq, topic, payload);
            /* hand off to the action callback (registered by main.c) */
            if (g_btn_cb) g_btn_cb(i + 1, g_btn_ud);
        }
    }
}

int wtsn_display_btn_last(void) {
    uint32_t r = g_btn_rising;
    g_btn_rising = 0;
    for (int i = 0; i < BTN_COUNT; i++) if (r & (1u << i)) return i + 1;
    return 0;
}

bool wtsn_display_btn(int n) {
    if (n < 1 || n > BTN_COUNT) return false;
    return (g_btn_levels >> (n - 1)) & 1u;
}

void wtsn_display_buttons(int *k1, int *k2, int *k3, int *k4) {
    if (k1) *k1 = (g_btn_levels >> 0) & 1u;
    if (k2) *k2 = (g_btn_levels >> 1) & 1u;
    if (k3) *k3 = (g_btn_levels >> 2) & 1u;
    if (k4) *k4 = (g_btn_levels >> 3) & 1u;
}

void wtsn_display_init(const char *device_id, wtsn_mqtt *mq) {
    if (device_id) snprintf(g_dev_id, sizeof(g_dev_id), "%s", device_id);
    g_mq = mq;
    g_hum = g_press = g_temp = 0;
    g_pir = g_light = 0;

    buttons_init();

    /* Startup delay: give the OLED module time to power on and finish its
     * internal reset before the first I2C contact. Clones ACK their address
     * as soon as the I2C peripheral is up, but silently drop commands sent
     * too early — which leaves the panel half-initialised and showing
     * static. */
    vTaskDelay(pdMS_TO_TICKS(400));
    g_init_us = esp_timer_get_time();

    /* Try to attach the display on the I2C bus (SDA21/SCL22). The bus is
     * already installed by the sensor add-on init; only reuse, don't own. */
    ssd1306_scan_bus();
    for (int attempt = 0; attempt < 2 && !g_present; attempt++) {
        if (attempt) vTaskDelay(pdMS_TO_TICKS(150));
        if (!display_probe_init()) {
            ESP_LOGW(TAG, "no SSD1306 on I2C (0x%02X) yet - retrying in tick",
                     SSD1306_ADDR);
        }
    }
}

/* Probe the panel and, if it answers, run the full init + first frame.
 * Returns true when the panel is attached. Called from init and re-called
 * from the tick for the first ~15 s to catch a slowly powering panel. */
static bool display_probe_init(void) {
    if (!ssd1306_present()) return false;
    vTaskDelay(pdMS_TO_TICKS(20));   /* settle after first contact */
    int ok = ssd1306_init_seq();
    vTaskDelay(pdMS_TO_TICKS(20));
    /* Boot straight into the 4-value info panel (no "display: OK" splash) so
     * the first thing on the glass is the same panel seen in steady state.
     * Values read "--" until the first telemetry frame arrives. */
    hud_render();
    ssd1306_flush();
    memcpy(g_fb_last, g_fb, SSD1306_BUF_SZ);
    g_fb_last_valid = true;
    g_present = true;
    if (g_found_us == 0) g_found_us = esp_timer_get_time();
    /* Arm the refresh interval so the first tick re-renders (to pick up any
     * telemetry that just arrived) and then holds for RENDER_INTERVAL_US. */
    g_last_render_us = esp_timer_get_time() - RENDER_INTERVAL_US;
    ESP_LOGI(TAG, "SSD1306 found at 0x%02X (SDA%d SCL%d), init %d/26 cmds",
             ssd1306_addr(), (int)WTSN_SSD1306_SDA, (int)WTSN_SSD1306_SCL, ok);
    return true;
}

/* Small JSON float extractor (the vendored wtsn_json only has int/str). */
static bool json_get_f(const char *json, const char *key, float *out) {
    if (!json || !key || !out) return false;
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *h = strstr(json, pat);
    if (!h) return false;
    h = strchr(h, ':');
    if (!h) return false;
    h++;
    while (*h == ' ' || *h == '\t') h++;
    char *end = NULL;
    float v = strtof(h, &end);
    if (end == h) return false;
    *out = v;
    return true;
}

/* The sensor board publishes an aggregated frame:
 *   {"id":"esp32-01","sensors":[{"sensor_id":"temp1","value":22.7,...},...]}
 * This finds the value that belongs to a given sensor_id (the "value" field
 * that follows the matching "sensor_id" entry). */
static bool sensor_value(const char *json, const char *sensor_id, float *out) {
    if (!json || !sensor_id || !out) return false;
    char pat[64];
    snprintf(pat, sizeof(pat), "\"sensor_id\":\"%s\"", sensor_id);
    const char *h = strstr(json, pat);
    if (!h) return false;
    const char *v = strstr(h, "\"value\":");
    if (!v) return false;
    v += 8; /* len("\"value\":") */
    while (*v == ' ' || *v == '\t') v++;
    char *end = NULL;
    float val = strtof(v, &end);
    if (end == v) return false;
    *out = val;
    return true;
}

void wtsn_display_on_telemetry(const char *topic, const char *payload) {
    (void)topic;
    if (!payload || !payload[0]) return;
    /* Only parse the aggregated telemetry from the sensor board node
     * (esp32-01). Our own button/status payloads must not feed the HUD. */
    char did[24] = "";
    wtsn_json_get_str(payload, "id", did, sizeof(did));
    if (did[0] && strstr(did, "esp32-01") == NULL && strcmp(did, "esp32-1") != 0) {
        return;
    }
    float v;
    if (sensor_value(payload, "temp1", &v) || json_get_f(payload, "temp", &v)) g_temp = v;
    if (sensor_value(payload, "hum1", &v) || json_get_f(payload, "hum", &v) || json_get_f(payload, "hum1", &v)) g_hum = v;
    if (sensor_value(payload, "press1", &v) || json_get_f(payload, "press", &v) || json_get_f(payload, "press1", &v)) g_press = v;
    if (sensor_value(payload, "light1", &v) || json_get_f(payload, "light", &v) || json_get_f(payload, "light1", &v)) g_light = (int)v;
    if (sensor_value(payload, "pir1", &v) || sensor_value(payload, "motion", &v) || json_get_f(payload, "motion", &v) || json_get_f(payload, "pir", &v)) g_pir = (v > 0.5f);
    bool first = (g_last_telem_us < 0);
    g_last_telem_us = esp_timer_get_time();
    /* Only force a redraw on the very first frame (so the values show up right
     * after boot). After that the tick's 10 s throttle + dirty check handle
     * updates — redrawing on every telemetry frame (~1 s) made the panel
     * flicker, because each full-frame I2C write is briefly visible. */
    if (first) g_last_render_us = 0;
}

void wtsn_display_set_hud(bool enable) { g_hud = enable; }
bool wtsn_display_hud(void) { return g_hud; }

/* Switch the panel controller between SSD1306 (128 segments) and SH1106
 * (132 segments, 2 invisible -> 2-column offset) at runtime. Re-inits the
 * panel and re-renders the current content. Returns true when the OLED is
 * present. Lets us match the panel without reflashing. */
bool wtsn_display_set_controller(bool sh1106) {
    g_sh1106 = sh1106;
    if (g_present) {
        ssd1306_init_seq();
        fb_clear();
        fb_text(0, 0, g_dev_id);
        fb_text(0, 2, sh1106 ? "sh1106" : "ssd1306");
        fb_text(0, 3, "ctrl set");
        ssd1306_flush();
    }
    ESP_LOGI(TAG, "panel controller -> %s (display %s)",
             sh1106 ? "SH1106" : "SSD1306", g_present ? "on" : "off");
    return g_present;
}
bool wtsn_display_controller_sh1106(void) { return g_sh1106; }

/* Fill the whole panel with a test pattern and flush — a pure I2C transport
 * check. pattern 0xFF = all lit, 0x00 = all off, 0xAA/0x55 = stripes. If the
 * panel shows the solid/striped pattern, data is reaching it and the content
 * (controller type / addressing) is the only thing left to match. */
void wtsn_display_fill(uint8_t pattern) {
    if (!g_present) return;
    /* Re-run the full init first: if the boot-time init was sent before the
     * panel's core finished power-on (and got dropped), the display-control
     * commands (charge pump, display-on) were lost and the panel shows
     * static. Re-initialising right before the fill guarantees the control
     * state is correct at the moment we test it. */
    ssd1306_init_seq();
    vTaskDelay(pdMS_TO_TICKS(20));
    memset(g_fb, pattern, sizeof(g_fb));
    ssd1306_flush();
    g_freeze = true;   /* hold the pattern so the tick doesn't overwrite it */
}

/* Freeze (hold the current frame) or resume normal rendering. */
void wtsn_display_freeze(bool on) { g_freeze = on; }

/* Decisive mapping test: re-init, then fill 4 solid quadrants (TL+BR lit,
 * TR+BL dark) and freeze. Unlike a uniform fill, quadrant edges reveal any
 * wrong segment remap / COM order / column offset — if the user sees the
 * correct 2x2 checkerboard the display geometry is right; if it's scrambled,
 * the panel config must be adjusted. */
void wtsn_display_quad(void) {
    if (!g_present) return;
    ssd1306_init_seq();
    vTaskDelay(pdMS_TO_TICKS(20));
    fb_clear();
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 128; x++) {
            bool tl = (x < 64) && (y < 32);
            bool br = (x >= 64) && (y >= 32);
            if (tl || br) fb_set_px(x, y);
        }
    }
    ssd1306_flush();
    g_freeze = true;
}

/* Combined row+column mapping test: 4 horizontal bars (at y 0,16,32,48),
 * each bar's LEFT half (x<64) lit and RIGHT half dark. 4 clean bars => rows
 * (COM) map correctly. Each bar cleanly split at the middle => columns (SEG)
 * map in order. Bars present but the split scrambled => segment order is the
 * fault (text would look like noise). */
void wtsn_display_bars(void) {
    if (!g_present) return;
    ssd1306_init_seq();
    vTaskDelay(pdMS_TO_TICKS(20));
    fb_clear();
    for (int bar = 0; bar < 4; bar++) {
        int y0 = bar * 16;
        for (int y = y0; y < y0 + 12; y++)
            for (int x = 0; x < 64; x++)
                fb_set_px(x, y);
    }
    ssd1306_flush();
    g_freeze = true;
}

/* Render the current content into the framebuffer (same as the tick) and log
 * it as an ASCII bitmap to the serial console. This shows the exact pixels we
 * compute, independent of the panel — if the dump is clean text but the panel
 * shows noise, the fault is display-side; if the dump is noise, the fault is in
 * our rendering. */
void wtsn_display_dump(void) {
    if (!g_present) { ESP_LOGW(TAG, "dump: no display"); return; }
    if (g_hud) {
        hud_render();   /* also flushes to the panel */
    } else {
        fb_clear();
        fb_text(0, 0, g_line1);
        fb_text(0, 1, g_line2);
        fb_text(0, 7, g_dev_id);
        ssd1306_flush();
    }
    ESP_LOGI(TAG, "=== framebuffer dump (128x64, # = lit) ===");
    for (int y = 0; y < 64; y++) {
        char line[130];
        for (int x = 0; x < 128; x++)
            line[x] = (g_fb[(y >> 3) * 128 + x] & (1 << (y & 7))) ? '#' : '.';
        line[128] = 0;
        ESP_LOGI(TAG, "%02d %s", y, line);
    }
    ESP_LOGI(TAG, "=== end dump ===");
}

/* Read the panel's OWN GDDRAM back over I2C and log it as an ASCII bitmap in
 * the same format as wtsn_display_dump(). Compare the two: if the panel's
 * GDDRAM matches our framebuffer, the data reached the panel and the fault is
 * in its display geometry; if it differs, our writes are being corrupted. */
void wtsn_display_dump_panel(void) {
    if (!g_present) { ESP_LOGW(TAG, "dump_panel: no display"); return; }
    uint8_t *ram = malloc(SSD1306_BUF_SZ);
    if (!ram) { ESP_LOGE(TAG, "dump_panel: oom"); return; }
    uint8_t *p = ram;
    for (int page = 0; page < 8; page++) {
        /* Set the X/Y pointer to this page, column 0 (write transaction). */
        i2c_cmd_handle_t w = i2c_cmd_link_create();
        i2c_master_start(w);
        i2c_master_write_byte(w, (ssd1306_addr() << 1) | I2C_MASTER_WRITE, 1);
        i2c_master_write_byte(w, SSD1306_CTRL_CMD, 1);
        i2c_master_write_byte(w, (uint8_t)(0xB0 | page), 1);
        i2c_master_write_byte(w, 0x00, 1);
        i2c_master_write_byte(w, 0x10, 1);
        i2c_master_stop(w);
        i2c_master_cmd_begin(I2C_NUM_0, w, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(w);

        /* Read the 128 bytes of this page (read transaction). */
        i2c_cmd_handle_t r = i2c_cmd_link_create();
        i2c_master_start(r);
        i2c_master_write_byte(r, (ssd1306_addr() << 1) | I2C_MASTER_READ, 1);
        for (int i = 0; i < 127; i++) i2c_master_read(r, &p[i], 1, I2C_MASTER_ACK);
        i2c_master_read_byte(r, &p[127], I2C_MASTER_NACK);
        i2c_master_stop(r);
        esp_err_t e = i2c_master_cmd_begin(I2C_NUM_0, r, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(r);
        if (e != ESP_OK) ESP_LOGE(TAG, "dump_panel page %d read err 0x%x", page, e);
        p += 128;
    }
    ESP_LOGI(TAG, "=== PANEL GDDRAM dump (128x64, # = lit) ===");
    for (int y = 0; y < 64; y++) {
        char line[130];
        for (int x = 0; x < 128; x++)
            line[x] = (ram[(y >> 3) * 128 + x] & (1 << (y & 7))) ? '#' : '.';
        line[128] = 0;
        ESP_LOGI(TAG, "%02d %s", y, line);
    }
    ESP_LOGI(TAG, "=== end panel dump ===");
    free(ram);
}

/* Read 16 bytes from the GDDRAM at page 0, column 0. */
static void gddram_read16(uint8_t *out, esp_err_t *err) {
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (ssd1306_addr() << 1) | I2C_MASTER_READ, 1);
    i2c_master_read(c, out, 15, I2C_MASTER_ACK);
    i2c_master_read_byte(c, &out[15], I2C_MASTER_NACK);
    i2c_master_stop(c);
    *err = i2c_master_cmd_begin(I2C_NUM_0, c, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(c);
}

/* Write 16 bytes of `val` to the GDDRAM at page 0, column 0. */
static void gddram_write16(uint8_t val, esp_err_t *err) {
    uint8_t data[16];
    memset(data, val, sizeof(data));
    i2c_cmd_handle_t c = i2c_cmd_link_create();
    i2c_master_start(c);
    i2c_master_write_byte(c, (ssd1306_addr() << 1) | I2C_MASTER_WRITE, 1);
    i2c_master_write_byte(c, SSD1306_CTRL_CMD, 1);
    i2c_master_write_byte(c, 0xB0, 1);
    i2c_master_write_byte(c, 0x00, 1);
    i2c_master_write_byte(c, 0x10, 1);
    i2c_master_write_byte(c, SSD1306_CTRL_DATA, 1);
    i2c_master_write(c, data, sizeof(data), 1);
    i2c_master_stop(c);
    *err = i2c_master_cmd_begin(I2C_NUM_0, c, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(c);
}

static void gddram_log16(const char *label, const uint8_t *b, esp_err_t e) {
    char logbuf[160] = "";
    int n = 0;
    for (int i = 0; i < 16; i++)
        n += snprintf(logbuf + n, sizeof(logbuf) - (size_t)n, "%02x ", b[i]);
    ESP_LOGI(TAG, "%s (err=0x%x): %s", label, e, logbuf);
}

/* Comprehensive transport test: read the native GDDRAM, then write 0xFF and
 * 0x00 and read each back. Tells us exactly whether the panel stores our data. */
void wtsn_display_readback(void) {
    if (!g_present) { ESP_LOGW(TAG, "readback: no display"); return; }
    uint8_t b[16];
    esp_err_t e;

    gddram_read16(b, &e);
    gddram_log16("native    ", b, e);

    esp_err_t w;
    gddram_write16(0xFF, &w);
    ESP_LOGI(TAG, "write 0xFF wr-err=0x%x", w);
    gddram_read16(b, &e);
    gddram_log16("after-0xFF", b, e);

    gddram_write16(0x00, &w);
    ESP_LOGI(TAG, "write 0x00 wr-err=0x%x", w);
    gddram_read16(b, &e);
    gddram_log16("after-0x00", b, e);
}
void wtsn_display_set_btn_cb(wtsn_display_btn_cb cb, void *ud) {
    g_btn_cb = cb;
    g_btn_ud = ud;
}

/* Render the 4-corner sensor HUD:
 *   TL = temperature   TR = humidity
 *   BL = light         BR = pressure
 * Middle row = K1..K4 levels. */
static void hud_render(void) {
    char row[24];

    fb_clear();

    if (g_temp > -100) snprintf(row, sizeof(row), "T: %.1f \xB0" "C", g_temp);
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
    /* flush is done by the caller through the dirty check */
}

void wtsn_display_tick(void) {
    buttons_tick();

    /* publish button state + press events on the shared sensor feed */
    if ((esp_timer_get_time() - g_last_pub_us) >= 2000000LL) {
        g_last_pub_us = esp_timer_get_time();
        buttons_publish();
    }

    if (!g_present) {
        /* Keep re-probing for the first 15 s after we first looked: a panel
         * whose supply is still settling (slow power-on / weak 3V3) may only
         * start answering I2C a couple of seconds later. One probe/second. */
        if (g_init_us > 0 && (esp_timer_get_time() - g_init_us) < 15000000LL) {
            static int64_t last_probe_us = 0;
            int64_t now = esp_timer_get_time();
            if (now - last_probe_us >= 1000000LL) {
                last_probe_us = now;
                display_probe_init();
            }
        }
        return;
    }

    /* Re-run the init sequence for ~10 s after the panel first answered.
     * The very first init is sent right after the first I2C contact, but the
     * panel's core is often not ready to latch commands yet (the I2C
     * peripheral ACKs before the display engine is up) — so that first init
     * is silently dropped and the panel shows static. Re-initialising once a
     * second for the first few seconds guarantees the control state
     * (charge-pump, display-on, memory mode) is latched by the time the core
     * is ready. */
    if (g_found_us > 0 && (esp_timer_get_time() - g_found_us) < 10000000LL) {
        static int64_t last_reinit_us = 0;
        int64_t now = esp_timer_get_time();
        if (now - last_reinit_us >= 1000000LL) {
            last_reinit_us = now;
            ssd1306_init_seq();
        }
    }

    if (g_freeze) return;   /* hold the current frame (test pattern) */

    /* Info-panel refresh policy: re-render at most every RENDER_INTERVAL_US.
     * Between renders the panel holds the last frame (no flicker). */
    int64_t now = esp_timer_get_time();
    if (now - g_last_render_us < RENDER_INTERVAL_US) return;

    if (g_hud) {
        hud_render();
    } else {
        fb_clear();
        fb_text(0, 0, g_line1);
        fb_text(0, 1, g_line2);
        char tmp[24];
        int b1 = (int)((g_btn_levels >> 0) & 1u);
        int b2 = (int)((g_btn_levels >> 1) & 1u);
        int b3 = (int)((g_btn_levels >> 2) & 1u);
        int b4 = (int)((g_btn_levels >> 3) & 1u);
        snprintf(tmp, sizeof(tmp), "1:%d 2:%d 3:%d 4:%d", b1, b2, b3, b4);
        fb_text(0, 6, tmp);
        fb_text(0, 7, g_dev_id);
    }

    /* Dirty check: only push a flush when the frame actually changed. This is
     * what stops the panel from flickering on every tick when nothing moved. */
    if (g_fb_last_valid && memcmp(g_fb, g_fb_last, SSD1306_BUF_SZ) == 0) {
        g_last_render_us = now;
        return;
    }
    ssd1306_flush();
    memcpy(g_fb_last, g_fb, SSD1306_BUF_SZ);
    g_fb_last_valid = true;
    g_last_render_us = now;
}
