#include "audiodriver.h"

#include <stddef.h>
#include "esp_log.h"

#if CONFIG_OPENCLAW_AUDIO_ENABLE
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

static const char *TAG = "audio";

#if CONFIG_OPENCLAW_AUDIO_ENABLE

#define CORES3_AUDIO_I2S_PORT I2S_NUM_0
#if CONFIG_OPENCLAW_AUDIO_USE_GPIO0_MCLK
#define CORES3_AUDIO_MCLK_GPIO 0  // CoreS3 AW88298 MCLK; also ESP32-S3 BOOT/Button C
#else
#define CORES3_AUDIO_MCLK_GPIO I2S_GPIO_UNUSED
#endif
#define CORES3_AUDIO_WS_GPIO 33    // I2S word select
#define CORES3_AUDIO_BCLK_GPIO 34  // I2S bit clock
#define CORES3_AUDIO_DOUT_GPIO 13  // I2S speaker data out
#define AUDIO_BEEP_CHUNK_SAMPLES 128

typedef struct {
    uint32_t frequency_hz;
    uint32_t duration_ms;
    uint8_t volume;
} audio_beep_cmd_t;

static i2s_chan_handle_t s_tx_handle;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_beep_queue;
static bool s_available;

static int16_t clamp_sample(int32_t sample)
{
    if (sample > 32767) {
        return 32767;
    }
    if (sample < -32768) {
        return -32768;
    }
    return (int16_t)sample;
}

static esp_err_t play_beep_now(uint32_t frequency_hz, uint32_t duration_ms, uint8_t volume)
{
    int16_t stereo[AUDIO_BEEP_CHUNK_SAMPLES * 2];
    uint32_t total_samples = (CORES3_AUDIO_SAMPLE_RATE * duration_ms) / 1000;
    uint32_t phase = 0;
    uint32_t phase_step = frequency_hz * 2;
    int32_t amplitude = (int32_t)volume * 120;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    while (total_samples > 0) {
        size_t frames = total_samples > AUDIO_BEEP_CHUNK_SAMPLES ? AUDIO_BEEP_CHUNK_SAMPLES : total_samples;
        for (size_t i = 0; i < frames; ++i) {
            phase += phase_step;
            if (phase >= CORES3_AUDIO_SAMPLE_RATE * 2) {
                phase -= CORES3_AUDIO_SAMPLE_RATE * 2;
            }
            int16_t sample = clamp_sample(phase < CORES3_AUDIO_SAMPLE_RATE ? amplitude : -amplitude);
            stereo[i * 2] = sample;
            stereo[i * 2 + 1] = sample;
        }
        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx_handle, stereo, frames * 2 * sizeof(int16_t),
                                          &written, pdMS_TO_TICKS(200));
        if (err != ESP_OK) {
            xSemaphoreGive(s_lock);
            return err;
        }
        total_samples -= frames;
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

static void audio_task(void *arg)
{
    (void)arg;
    audio_beep_cmd_t cmd;
    while (true) {
        if (xQueueReceive(s_beep_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            esp_err_t err = play_beep_now(cmd.frequency_hz, cmd.duration_ms, cmd.volume);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "beep playback failed: %s", esp_err_to_name(err));
            }
        }
    }
}

esp_err_t audio_init(void)
{
    if (s_available) {
        return ESP_OK;
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_beep_queue == NULL) {
        s_beep_queue = xQueueCreate(2, sizeof(audio_beep_cmd_t));
        if (s_beep_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    i2s_chan_config_t chan_cfg = {
        .id = CORES3_AUDIO_I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = 256,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S TX channel init failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = CORES3_AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = CORES3_AUDIO_MCLK_GPIO,
            .bclk = CORES3_AUDIO_BCLK_GPIO,
            .ws = CORES3_AUDIO_WS_GPIO,
            .dout = CORES3_AUDIO_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_tx_handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S TX setup failed: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return err;
    }

    s_available = true;
    if (xTaskCreate(audio_task, "audio_task", 4096, NULL, 4, NULL) != pdPASS) {
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        s_available = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "I2S speaker TX initialized: rate=%d mclk=%d bclk=GPIO%d ws=GPIO%d dout=GPIO%d",
             CORES3_AUDIO_SAMPLE_RATE, CORES3_AUDIO_MCLK_GPIO, CORES3_AUDIO_BCLK_GPIO,
             CORES3_AUDIO_WS_GPIO, CORES3_AUDIO_DOUT_GPIO);
    return ESP_OK;
}

bool audio_is_available(void)
{
    return s_available && s_tx_handle != NULL;
}

esp_err_t audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms)
{
    if (!audio_is_available()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t written = 0;
    esp_err_t err = i2s_channel_write(s_tx_handle, samples, sample_count * sizeof(int16_t),
                                      &written, pdMS_TO_TICKS(timeout_ms));
    xSemaphoreGive(s_lock);
    return err == ESP_OK && written == sample_count * sizeof(int16_t) ? ESP_OK : err;
}

esp_err_t audio_beep(uint32_t frequency_hz, uint32_t duration_ms, uint8_t volume)
{
    if (!audio_is_available()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (frequency_hz < 80 || frequency_hz > 4000 || duration_ms < 20 || duration_ms > 2000 || volume > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    audio_beep_cmd_t cmd = {
        .frequency_hz = frequency_hz,
        .duration_ms = duration_ms,
        .volume = volume,
    };
    if (xQueueSend(s_beep_queue, &cmd, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

#else

esp_err_t audio_init(void)
{
    ESP_LOGI(TAG, "audio disabled by CONFIG_OPENCLAW_AUDIO_ENABLE");
    return ESP_ERR_NOT_SUPPORTED;
}

bool audio_is_available(void)
{
    return false;
}

esp_err_t audio_beep(uint32_t frequency_hz, uint32_t duration_ms, uint8_t volume)
{
    (void)frequency_hz;
    (void)duration_ms;
    (void)volume;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms)
{
    (void)samples;
    (void)sample_count;
    (void)timeout_ms;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
