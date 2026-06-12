#include "leddriver.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "driver/i2c.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "led";

static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static SemaphoreHandle_t s_lock;
static led_effect_t s_effect = LED_EFFECT_SOLID;
static uint8_t s_r;
static uint8_t s_g;
static uint8_t s_b;
static uint8_t s_speed = 3;
static bool s_py32_available;
static uint8_t s_py32_failures;

#define CORES3_LED_I2C_PORT I2C_NUM_1
#define CORES3_PY32_I2C_ADDR 0x6F
#define CORES3_PY32_LED_COUNT 12
#define PY32_REG_LED_CFG 0x24
#define PY32_REG_LED_RAM 0x30
#define PY32_LED_REFRESH_BIT 0x40

static uint16_t led_rgb888_to_565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

static esp_err_t py32_write_block(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buffer[1 + CORES3_PY32_LED_COUNT * 2];
    if (data == NULL || len + 1 > sizeof(buffer)) {
        return ESP_ERR_INVALID_ARG;
    }
    buffer[0] = reg;
    memcpy(buffer + 1, data, len);
    return i2c_master_write_to_device(CORES3_LED_I2C_PORT, CORES3_PY32_I2C_ADDR,
                                      buffer, len + 1, pdMS_TO_TICKS(100));
}

static esp_err_t py32_write_led_frame(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t color = led_rgb888_to_565(r, g, b);
    uint8_t data[CORES3_PY32_LED_COUNT * 2];
    for (int i = 0; i < CORES3_PY32_LED_COUNT; ++i) {
        data[i * 2] = (uint8_t)(color & 0xFF);
        data[i * 2 + 1] = (uint8_t)(color >> 8);
    }

    esp_err_t err = py32_write_block(PY32_REG_LED_RAM, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }
    uint8_t refresh = CORES3_PY32_LED_COUNT | PY32_LED_REFRESH_BIT;
    return py32_write_block(PY32_REG_LED_CFG, &refresh, sizeof(refresh));
}

static void py32_try_init(void)
{
    uint8_t count = CORES3_PY32_LED_COUNT;
    esp_err_t err = py32_write_block(PY32_REG_LED_CFG, &count, sizeof(count));
    if (err == ESP_OK) {
        err = py32_write_led_frame(0, 0, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PY32 LED ring not available at 0x%02X: %s",
                 CORES3_PY32_I2C_ADDR, esp_err_to_name(err));
        s_py32_available = false;
        return;
    }

    s_py32_available = true;
    s_py32_failures = 0;
    ESP_LOGI(TAG, "PY32 LED ring initialized at 0x%02X", CORES3_PY32_I2C_ADDR);
}

static void led_write_raw(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_channel != NULL && s_encoder != NULL) {
        uint8_t grb[3] = {g, r, b};  // SK6812/NeoPixel byte order is GRB.
        rmt_transmit_config_t tx_cfg = {
            .loop_count = 0,
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_transmit(s_channel, s_encoder, grb, sizeof(grb), &tx_cfg));
        ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_tx_wait_all_done(s_channel, pdMS_TO_TICKS(100)));
    }

    if (s_py32_available) {
        esp_err_t err = py32_write_led_frame(r, g, b);
        if (err != ESP_OK) {
            s_py32_failures++;
            ESP_LOGW(TAG, "PY32 LED ring write failed (%u/3): %s",
                     s_py32_failures, esp_err_to_name(err));
            if (s_py32_failures >= 3) {
                s_py32_available = false;
                ESP_LOGW(TAG, "PY32 LED ring disabled after repeated write failures");
            }
        } else {
            s_py32_failures = 0;
        }
    }
}

static void led_task(void *arg)
{
    (void)arg;
    float phase = 0.0f;
    bool wrote_solid = false;
    uint8_t last_r = 0;
    uint8_t last_g = 0;
    uint8_t last_b = 0;
    while (true) {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t speed;
        led_effect_t effect;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        r = s_r;
        g = s_g;
        b = s_b;
        speed = s_speed == 0 ? 1 : s_speed;
        effect = s_effect;
        xSemaphoreGive(s_lock);

        if (effect == LED_EFFECT_BREATH) {
            phase += 0.018f * speed;
            if (phase > 6.28318f) {
                phase -= 6.28318f;
            }
            float brightness = (sinf(phase) + 1.0f) * 0.5f;
            led_write_raw((uint8_t)(r * brightness), (uint8_t)(g * brightness), (uint8_t)(b * brightness));
            wrote_solid = false;
        } else {
            if (!wrote_solid || r != last_r || g != last_g || b != last_b) {
                led_write_raw(r, g, b);
                last_r = r;
                last_g = g;
                last_b = b;
                wrote_solid = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

esp_err_t led_init(void)
{
    esp_err_t rmt_err = ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = CORES3_LED_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    rmt_err = rmt_new_tx_channel(&tx_cfg, &s_channel);

    if (rmt_err == ESP_OK) {
        rmt_bytes_encoder_config_t encoder_cfg = {
            .bit0 = {
                .level0 = 1,
                .duration0 = 3,  // 0.3us at 10MHz
                .level1 = 0,
                .duration1 = 9,  // 0.9us at 10MHz
            },
            .bit1 = {
                .level0 = 1,
                .duration0 = 6,  // 0.6us at 10MHz
                .level1 = 0,
                .duration1 = 6,  // 0.6us at 10MHz
            },
            .flags.msb_first = 1,
        };
        rmt_err = rmt_new_bytes_encoder(&encoder_cfg, &s_encoder);
    }
    if (rmt_err == ESP_OK) {
        rmt_err = rmt_enable(s_channel);
    }
    if (rmt_err != ESP_OK) {
        ESP_LOGW(TAG, "GPIO%d SK6812 RMT init failed: %s", CORES3_LED_GPIO, esp_err_to_name(rmt_err));
        s_channel = NULL;
        s_encoder = NULL;
    }

    py32_try_init();
    if (rmt_err != ESP_OK && !s_py32_available) {
        return rmt_err;
    }
    led_set_color(0, 0, 0);
    xTaskCreate(led_task, "led_task", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "SK6812 LED initialized on GPIO%d; PY32 ring %s",
             CORES3_LED_GPIO, s_py32_available ? "enabled" : "disabled");
    return ESP_OK;
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_r = r;
    s_g = g;
    s_b = b;
    s_effect = LED_EFFECT_SOLID;
    xSemaphoreGive(s_lock);
}

void led_set_breath(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_r = r;
    s_g = g;
    s_b = b;
    s_speed = speed == 0 ? 1 : speed;
    s_effect = LED_EFFECT_BREATH;
    xSemaphoreGive(s_lock);
}
