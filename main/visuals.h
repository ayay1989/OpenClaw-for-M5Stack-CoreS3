#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdbool.h>
#include "esp_err.h"
#include "presence.h"

esp_err_t visuals_init(void);
void visuals_apply(presence_state_t state, const char *emotion, bool mouth_open);
void visuals_apply_now(presence_state_t state, const char *emotion, bool mouth_open);
