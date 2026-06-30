#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include "cJSON.h"
#include "esp_err.h"

typedef void (*selftest_send_json_fn_t)(cJSON *root);

esp_err_t selftest_start(selftest_send_json_fn_t send_json);
