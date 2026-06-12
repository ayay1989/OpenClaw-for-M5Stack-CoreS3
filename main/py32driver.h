#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c.h"
#include "esp_err.h"

#define CORES3_PY32_I2C_ADDR 0x6F  // Stackchan CoreS3 PY32 helper MCU
#define CORES3_PY32_LED_COUNT 12   // Stackchan-style RGB ring on PY32

esp_err_t py32_init(i2c_port_t port);
bool py32_is_available(void);
esp_err_t py32_set_servo_power(bool enabled);
esp_err_t py32_write_led_rgb(uint8_t r, uint8_t g, uint8_t b);

