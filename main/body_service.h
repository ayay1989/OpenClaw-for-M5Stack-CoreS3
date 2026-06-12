#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "presence.h"

esp_err_t body_service_init(void);
bool body_motion_available(void);
esp_err_t body_look_at(int yaw_deg, int pitch_deg, uint32_t duration_ms);
esp_err_t body_motion_gesture(const char *gesture);
void body_apply_presence(presence_state_t state);
void body_apply_touch_gesture(const char *gesture);

