#include "button.h"

#include <stdbool.h>
#include <stddef.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol.h"

static const char *TAG = "button";

typedef struct {
    gpio_num_t gpio;
    const char *name;
    bool last_pressed;
    uint32_t stable_ms;
} button_state_t;

static button_state_t s_buttons[] = {
    {CORES3_BUTTON_A_GPIO, "A", false, 0},
    {CORES3_BUTTON_B_GPIO, "B", false, 0},
#if !CONFIG_OPENCLAW_AUDIO_USE_GPIO0_MCLK
    {CORES3_BUTTON_C_GPIO, "C", false, 0},
#endif
};

static void button_task(void *arg)
{
    (void)arg;
    const uint32_t poll_ms = 10;
    const uint32_t debounce_ms = 40;

    while (true) {
        for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
            bool pressed_now = gpio_get_level(s_buttons[i].gpio) == 0;
            if (pressed_now != s_buttons[i].last_pressed) {
                s_buttons[i].stable_ms += poll_ms;
                if (s_buttons[i].stable_ms >= debounce_ms) {
                    s_buttons[i].last_pressed = pressed_now;
                    s_buttons[i].stable_ms = 0;
                    protocol_emit_button(s_buttons[i].name, pressed_now ? "press" : "release");
                }
            } else {
                s_buttons[i].stable_ms = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
    }
}

esp_err_t button_init(void)
{
    uint64_t mask = (1ULL << CORES3_BUTTON_A_GPIO) |
                    (1ULL << CORES3_BUTTON_B_GPIO);
#if !CONFIG_OPENCLAW_AUDIO_USE_GPIO0_MCLK
    mask |= (1ULL << CORES3_BUTTON_C_GPIO);
#endif
    gpio_config_t io_conf = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "gpio_config failed");
    ESP_LOGI(TAG, "buttons initialized: A=%d B=%d C=%s",
             CORES3_BUTTON_A_GPIO, CORES3_BUTTON_B_GPIO,
#if CONFIG_OPENCLAW_AUDIO_USE_GPIO0_MCLK
             "disabled for audio MCLK on GPIO0"
#else
             "GPIO0/BOOT"
#endif
    );
    xTaskCreate(button_task, "button_task", 3072, NULL, 8, NULL);
    return ESP_OK;
}
