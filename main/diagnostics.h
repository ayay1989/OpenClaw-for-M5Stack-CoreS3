#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include "cJSON.h"
#include "esp_err.h"
#include "leddriver.h"

void diagnostics_add_led_write_result(cJSON *root, const led_write_result_t *result);
void diagnostics_add_hardware_status(cJSON *root);
const char *diagnostics_motion_error_message(esp_err_t err);
const char *diagnostics_audio_error_message(esp_err_t err);
