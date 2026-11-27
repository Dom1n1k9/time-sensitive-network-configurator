/* I2S speaker (MAX98357A) for esp32-01 — see htsn_audio.h.
 *
 * The I2S TX channel is enabled ONCE at a fixed 22050 Hz / 16-bit / mono (which
 * matches espeak-ng on the brain) and a single task keeps it fed: it writes real
 * PCM when a clip is queued, otherwise continuous silence (zeros). Because the
 * DMA is always fed there is no underrun "stuck buzz", and enabling once avoids
 * the per-playback enable/disable problems.
 */
#include "htsn_audio.h"

#include "esp_log.h"
#include "driver/i2s_std.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "audio";

#define AUDIO_BCLK_GPIO  GPIO_NUM_23
#define AUDIO_LRC_GPIO   GPIO_NUM_4
#define AUDIO_DIN_GPIO   GPIO_NUM_5
#define AUDIO_RATE       22050
#define AUDIO_URL_MAX    256
#define AUDIO_DL_MAX     (2 * 1024 * 1024)

typedef struct { int kind; int freq; int ms; char url[AUDIO_URL_MAX]; } audio_msg;

static i2s_chan_handle_t s_tx = NULL;
static bool s_ready = false;
static QueueHandle_t s_q = NULL;

bool htsn_audio_ready(void) { return s_ready; }

static void i2s_write(const void *data, size_t len)
{
    const uint8_t *p = data;
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > 2048) chunk = 2048;
        size_t w = 0;
        if (i2s_channel_write(s_tx, p + off, chunk, &w, pdMS_TO_TICKS(2000)) != ESP_OK) break;
        if (w == 0) break;
        off += w;
    }
}

static void play_tone(int freq_hz, int ms)
{
    if (freq_hz <= 0 || ms <= 0) return;
    const int total = AUDIO_RATE * ms / 1000;
    int16_t buf[512];
    int n = 0;
    for (int i = 0; i < total; i++) {
        buf[n++] = (int16_t)(11000.0f * sinf(2.0f * (float)M_PI * (float)freq_hz * (float)i / (float)AUDIO_RATE));
        if (n == 512) { i2s_write(buf, sizeof(buf)); n = 0; }
    }
    if (n) i2s_write(buf, (size_t)n * sizeof(int16_t));
    ESP_LOGI(TAG, "tone %d Hz %d ms", freq_hz, ms);
}

static uint8_t *http_fetch(const char *url, size_t *out_len)
{
    *out_len = 0;
    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 8000 };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return NULL;
    if (esp_http_client_open(c, 0) != ESP_OK) { esp_http_client_cleanup(c); return NULL; }
    int clen = esp_http_client_fetch_headers(c);
    size_t cap = (clen > 0) ? (size_t)clen : 262144;
    if (cap > AUDIO_DL_MAX) { esp_http_client_close(c); esp_http_client_cleanup(c); return NULL; }
    uint8_t *buf = malloc(cap + 1024);
    if (!buf) { esp_http_client_close(c); esp_http_client_cleanup(c); return NULL; }
    size_t total = 0;
    while (total < cap + 1024) {
        int r = esp_http_client_read(c, (char *)buf + total, (int)(cap + 1024 - total));
        if (r <= 0) break;
        total += (size_t)r;
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    *out_len = total;
    return buf;
}

static void play_wav(const uint8_t *b, size_t n)
{
    if (n < 44 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { ESP_LOGW(TAG, "not WAV"); return; }
    size_t off = 12;
    uint16_t bits = 0;
    while (off + 8 <= n) {
        uint32_t sz = (uint32_t)b[off+4] | ((uint32_t)b[off+5] << 8) |
                      ((uint32_t)b[off+6] << 16) | ((uint32_t)b[off+7] << 24);
        if (!memcmp(b + off, "fmt ", 4) && off + 24 <= n) {
            bits = (uint16_t)(b[off+22] | (b[off+23] << 8));
        } else if (!memcmp(b + off, "data", 4)) {
            size_t start = off + 8;
            size_t avail = (start <= n) ? (n - start) : 0;
            size_t dlen = (sz < avail) ? sz : avail;
            if (bits != 16) { ESP_LOGW(TAG, "WAV not 16-bit"); return; }
            ESP_LOGI(TAG, "play WAV %u bytes", (unsigned)dlen);
            i2s_write(b + start, dlen);
            return;
        }
        off += 8 + sz + (sz & 1);
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    audio_msg m;
    static const int16_t zeros[1024] = {0};
    for (;;) {
        if (xQueueReceive(s_q, &m, 0) == pdTRUE) {
            if (m.kind == 0) {
                play_tone(m.freq, m.ms);
            } else {
                size_t len = 0;
                uint8_t *buf = http_fetch(m.url, &len);
                if (buf && len) play_wav(buf, len);
                free(buf);
            }
        } else {
            size_t w = 0;
            i2s_channel_write(s_tx, zeros, sizeof(zeros), &w, pdMS_TO_TICKS(500));  /* idle silence */
        }
    }
}

void htsn_audio_tone(int freq_hz, int ms)
{
    if (!s_ready) return;
    audio_msg m = { .kind = 0, .freq = freq_hz, .ms = ms };
    xQueueSend(s_q, &m, 0);
}

void htsn_audio_play_url(const char *url)
{
    if (!s_ready || !url || !url[0]) return;
    audio_msg m = { .kind = 1 };
    strncpy(m.url, url, sizeof(m.url) - 1);
    xQueueSend(s_q, &m, 0);
}

void htsn_audio_init(void)
{
    if (s_ready) return;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan, &s_tx, NULL) != ESP_OK) { ESP_LOGW(TAG, "new_channel failed"); return; }
    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = AUDIO_BCLK_GPIO, .ws = AUDIO_LRC_GPIO,
            .dout = AUDIO_DIN_GPIO, .din = I2S_GPIO_UNUSED, .invert_flags = { 0 },
        },
    };
    if (i2s_channel_init_std_mode(s_tx, &std) != ESP_OK) {
        ESP_LOGW(TAG, "init std failed"); i2s_del_channel(s_tx); s_tx = NULL; return;
    }
    i2s_channel_enable(s_tx);                 /* enable ONCE, always fed */
    s_q = xQueueCreate(4, sizeof(audio_msg));
    xTaskCreate(audio_task, "htsn_audio", 6144, NULL, 4, NULL);
    s_ready = true;
    ESP_LOGI(TAG, "MAX98357A ready (BCLK=23 LRC=4 DIN=5, %d Hz)", AUDIO_RATE);
}
