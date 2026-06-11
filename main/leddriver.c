#include "leddriver.h"

#include <math.h>
#include <string.h>
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

static void led_write_raw(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_channel == NULL || s_encoder == NULL) {
        return;
    }
    uint8_t grb[3] = {g, r, b};  // SK6812/NeoPixel byte order is GRB.
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_transmit(s_channel, s_encoder, grb, sizeof(grb), &tx_cfg));
    ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_tx_wait_all_done(s_channel, pdMS_TO_TICKS(100)));
}

static void led_task(void *arg)
{
    (void)arg;
    float phase = 0.0f;
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
        } else {
            led_write_raw(r, g, b);
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

esp_err_t led_init(void)
{
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
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s_channel), TAG, "rmt_new_tx_channel failed");

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
    ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&encoder_cfg, &s_encoder), TAG, "rmt_new_bytes_encoder failed");
    ESP_RETURN_ON_ERROR(rmt_enable(s_channel), TAG, "rmt_enable failed");

    led_set_color(0, 0, 0);
    xTaskCreate(led_task, "led_task", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "SK6812 LED initialized on GPIO%d", CORES3_LED_GPIO);
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
    led_write_raw(r, g, b);
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
