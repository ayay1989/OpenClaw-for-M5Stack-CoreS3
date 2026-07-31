#include "leddriver.h"

#include <math.h>
#include <stdbool.h>
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "py32driver.h"

static const char *TAG = "led";

static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static SemaphoreHandle_t s_lock;
static led_effect_t s_effect = LED_EFFECT_SOLID;
static uint8_t s_r;
static uint8_t s_g;
static uint8_t s_b;
static uint8_t s_speed = 3;

static led_write_result_t led_write_raw(uint8_t r, uint8_t g, uint8_t b, bool force)
{
    led_write_result_t result = {
        .led_gpio_write_ok = false,
        .py32_led_write_ok = false,
        .py32_led_available = py32_led_is_available(),
    };

    if (s_channel != NULL && s_encoder != NULL) {
        uint8_t grb[3] = {g, r, b};  // SK6812/NeoPixel byte order is GRB.
        rmt_transmit_config_t tx_cfg = {
            .loop_count = 0,
        };
        esp_err_t gpio_err = rmt_transmit(s_channel, s_encoder, grb, sizeof(grb), &tx_cfg);
        if (gpio_err == ESP_OK) {
            gpio_err = rmt_tx_wait_all_done(s_channel, pdMS_TO_TICKS(100));
        }
        result.led_gpio_write_ok = gpio_err == ESP_OK;
        if (gpio_err != ESP_OK) {
            ESP_LOGW(TAG, "GPIO%d SK6812 write failed: %s", CORES3_LED_GPIO, esp_err_to_name(gpio_err));
        }
    }

    if (py32_is_available()) {
        esp_err_t py32_err = py32_write_led_rgb(r, g, b, force);
        result.py32_led_available = py32_led_is_available();
        result.py32_led_write_ok = py32_err == ESP_OK;
        if (py32_err != ESP_OK && (force || py32_err != ESP_ERR_NOT_FOUND)) {
            ESP_LOGW(TAG, "PY32 LED write failed: %s", esp_err_to_name(py32_err));
        }
    }
    return result;
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
            (void)led_write_raw((uint8_t)(r * brightness), (uint8_t)(g * brightness), (uint8_t)(b * brightness), false);
            wrote_solid = false;
        } else {
            if (!wrote_solid || r != last_r || g != last_g || b != last_b) {
                (void)led_write_raw(r, g, b, false);
                last_r = r;
                last_g = g;
                last_b = b;
                wrote_solid = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
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

    if (rmt_err != ESP_OK && !py32_is_available()) {
        return rmt_err;
    }
    led_set_color(0, 0, 0);
    xTaskCreate(led_task, "led_task", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "SK6812 LED initialized on GPIO%d; PY32 chip %s; PY32 ring %s",
             CORES3_LED_GPIO,
             py32_is_available() ? "available" : "missing",
             py32_led_is_available() ? "configured" : "pending");
    return ESP_OK;
}

bool led_gpio_is_available(void)
{
    return s_channel != NULL && s_encoder != NULL;
}

bool led_is_available(void)
{
    return led_gpio_is_available() || py32_led_is_available();
}

led_write_result_t led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_r = r;
    s_g = g;
    s_b = b;
    s_effect = LED_EFFECT_SOLID;
    xSemaphoreGive(s_lock);
    return led_write_raw(r, g, b, true);
}

led_write_result_t led_set_breath(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_r = r;
    s_g = g;
    s_b = b;
    s_speed = speed == 0 ? 1 : speed;
    s_effect = LED_EFFECT_BREATH;
    xSemaphoreGive(s_lock);
    return led_write_raw(r, g, b, true);
}
