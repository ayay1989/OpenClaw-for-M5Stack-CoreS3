#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdint.h>
#include "esp_err.h"

#define CORES3_LED_GPIO 45  // ESP32-S3 compatible RMT GPIO for external NeoPixel/SK6812
// NOTE: CoreS3 onboard SK6812 is behind AW9523 GPIO expander (I2C), not a direct GPIO.
// This pin works with ESP32-S3 RMT TX; wire an external NeoPixel to it.

typedef enum {
    LED_EFFECT_SOLID = 0,
    LED_EFFECT_BREATH,
} led_effect_t;

esp_err_t led_init(void);
void led_set_color(uint8_t r, uint8_t g, uint8_t b);
void led_set_breath(uint8_t r, uint8_t g, uint8_t b, uint8_t speed);
