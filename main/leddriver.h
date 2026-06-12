#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdint.h>
#include "esp_err.h"

#define CORES3_LED_GPIO 45  // ESP32-S3 compatible RMT GPIO for external NeoPixel/SK6812
// GPIO45 remains the known-good external NeoPixel/SK6812 output.
// Stackchan/PY32 LED rings are optional and are driven over the internal I2C bus.

typedef enum {
    LED_EFFECT_SOLID = 0,
    LED_EFFECT_BREATH,
} led_effect_t;

esp_err_t led_init(void);
void led_set_color(uint8_t r, uint8_t g, uint8_t b);
void led_set_breath(uint8_t r, uint8_t g, uint8_t b, uint8_t speed);
