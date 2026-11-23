/* I2S speaker (MAX98357A) for esp32-01 — see htsn_audio.h. */
#include "htsn_audio.h"

#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "audio";

/* esp32-01 free pins */
#define AUDIO_BCLK_GPIO  GPIO_NUM_23
#define AUDIO_LRC_GPIO   GPIO_NUM_4
#define AUDIO_DIN_GPIO   GPIO_NUM_5

#define AUDIO_DEFAULT_RATE 16000
#define AUDIO_URL_MAX      256
#define AUDIO_DL_MAX       (2 * 1024 * 1024)   /* cap the downloaded WAV */

static i2s_chan_handle_t s_tx = NULL;
static bool s_ready = false;
static QueueHandle_t s_q = NULL;

bool htsn_audio_ready(void) { return s_ready; }

static bool i2s_apply(uint32_t rate, int channels)
{
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    if (i2s_channel_disable(s_tx) != ESP_OK) return false;
    if (i2s_channel_reconfig_std_clock(s_tx, &clk) != ESP_OK) return false;
    i2s_slot_mode_t smode = (channels >= 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
    i2s_std_slot_config_t slot = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, smode);
    if (i2s_channel_reconfig_std_slot(s_tx, &slot) != ESP_OK) return false;
    return i2s_channel_enable(s_tx) == ESP_OK;
}

/* Write PCM data to the amp in chunks. */
static void i2s_write_pcm(const uint8_t *data, size_t len)
{
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > 2048) chunk = 2048;
        size_t written = 0;
        if (i2s_channel_write(s_tx, data + off, chunk, &written,
                              pdMS_TO_TICKS(1000)) != ESP_OK) {
            break;
        }
        if (written == 0) break;
        off += written;
    }
}

/* Minimal WAV parser: returns data pointer/len + rate/channels/bits. */
static bool wav_parse(const uint8_t *b, size_t n, uint32_t *rate,
                      uint16_t *ch, uint16_t *bits,
                      const uint8_t **data, size_t *dlen)
{
    if (n < 44 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return false;
    size_t off = 12;
    bool have_fmt = false;
    while (off + 8 <= n) {
        const uint8_t *id = b + off;
        uint32_t sz = (uint32_t)b[off+4] | ((uint32_t)b[off+5] << 8) |
                      ((uint32_t)b[off+6] << 16) | ((uint32_t)b[off+7] << 24);
        if (!memcmp(id, "fmt ", 4) && off + 8 + 16 <= n) {
            *ch   = (uint16_t)(b[off+10] | (b[off+11] << 8));
            *rate = (uint32_t)b[off+12] | ((uint32_t)b[off+13] << 8) |
                    ((uint32_t)b[off+14] << 16) | ((uint32_t)b[off+15] << 24);
            *bits = (uint16_t)(b[off+22] | (b[off+23] << 8));
            have_fmt = true;
        } else if (!memcmp(id, "data", 4)) {
            size_t start = off + 8;
            size_t avail = (start <= n) ? (n - start) : 0;
            *data = b + start;
            *dlen = (sz < avail) ? sz : avail;
            return have_fmt && *rate && *bits;
        }
        off += 8 + sz + (sz & 1);   /* chunks are word-aligned */
    }
    return false;
}

static void play_wav_buf(const uint8_t *buf, size_t len)
{
    uint32_t rate = 0;
    uint16_t ch = 0, bits = 0;
    const uint8_t *data = NULL;
    size_t dlen = 0;

    if (!wav_parse(buf, len, &rate, &ch, &bits, &data, &dlen)) {
        ESP_LOGW(TAG, "not a supported WAV");
        return;
    }
    if (bits != 16) {
        ESP_LOGW(TAG, "only 16-bit PCM supported (got %u)", bits);
        return;
    }
    ESP_LOGI(TAG, "play %u Hz, %u ch, %u bytes", (unsigned)rate, ch, (unsigned)dlen);
    if (!i2s_apply(rate, ch)) {
        ESP_LOGE(TAG, "i2s reconfigure failed");
        return;
    }
    i2s_write_pcm(data, dlen);
}

/* Download a URL fully into a malloc'd buffer (capped). */
static uint8_t *http_fetch(const char *url, size_t *out_len)
{
    *out_len = 0;
    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 8000 };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return NULL;

    if (esp_http_client_open(c, 0) != ESP_OK) {
        ESP_LOGW(TAG, "http open failed: %s", url);
        esp_http_client_cleanup(c);
        return NULL;
    }
    int clen = esp_http_client_fetch_headers(c);
    size_t cap = (clen > 0) ? (size_t)clen : 262144;
    if (cap > AUDIO_DL_MAX) { ESP_LOGW(TAG, "too big"); esp_http_client_close(c); esp_http_client_cleanup(c); return NULL; }

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

static void audio_task(void *arg)
{
    (void)arg;
    char url[AUDIO_URL_MAX];
    for (;;) {
        if (xQueueReceive(s_q, url, portMAX_DELAY) == pdTRUE) {
            size_t len = 0;
            uint8_t *buf = http_fetch(url, &len);
            if (buf && len) {
                play_wav_buf(buf, len);
            } else {
                ESP_LOGW(TAG, "fetch failed: %s", url);
            }
            free(buf);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void htsn_audio_tone(int freq_hz, int ms)
{
    if (!s_ready) return;
    if (freq_hz <= 0 || ms <= 0) return;
    const int rate = AUDIO_DEFAULT_RATE;
    if (!i2s_apply(rate, 1)) return;
    const int total = rate * ms / 1000;
    int16_t buf[512];
    int n = 0;
    for (int i = 0; i < total; i++) {
        buf[n++] = (int16_t)(12000.0f * sinf(2.0f * (float)M_PI * (float)freq_hz * (float)i / (float)rate));
        if (n == 512) {
            size_t w = 0;
            i2s_channel_write(s_tx, buf, sizeof(buf), &w, pdMS_TO_TICKS(1000));
            n = 0;
        }
    }
    if (n) {
        size_t w = 0;
        i2s_channel_write(s_tx, buf, (size_t)n * sizeof(int16_t), &w, pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "tone %d Hz %d ms", freq_hz, ms);
}

void htsn_audio_play_url(const char *url)
{
    if (!s_ready || !url || !url[0]) return;
    char m[AUDIO_URL_MAX];
    strncpy(m, url, sizeof(m) - 1);
    m[sizeof(m) - 1] = 0;
    if (xQueueSend(s_q, m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "audio queue full, dropped");
    }
}

void htsn_audio_init(void)
{
    if (s_ready) return;

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan, &s_tx, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "i2s_new_channel failed");
        return;
    }
    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_DEFAULT_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_BCLK_GPIO,
            .ws   = AUDIO_LRC_GPIO,
            .dout = AUDIO_DIN_GPIO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { 0 },
        },
    };
    if (i2s_channel_init_std_mode(s_tx, &std) != ESP_OK) {
        ESP_LOGW(TAG, "i2s init std mode failed");
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return;
    }
    i2s_channel_enable(s_tx);

    s_q = xQueueCreate(2, AUDIO_URL_MAX);
    xTaskCreate(audio_task, "htsn_audio", 6144, NULL, 4, NULL);

    s_ready = true;
    ESP_LOGI(TAG, "MAX98357A ready (BCLK=23 LRC=4 DIN=5)");
}
