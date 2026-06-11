#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include "esp_err.h"

#define CORES3_BUTTON_A_GPIO 41  // Button A, active-low
#define CORES3_BUTTON_B_GPIO 42  // Button B, active-low
#define CORES3_BUTTON_C_GPIO  0  // Button C, active-low; also ESP32 BOOT pin

esp_err_t button_init(void);
