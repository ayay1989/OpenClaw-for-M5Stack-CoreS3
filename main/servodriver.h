#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define SERVO_YAW_MIN_DEG (-45)
#define SERVO_YAW_MAX_DEG 45
#define SERVO_PITCH_MIN_DEG 5
#define SERVO_PITCH_MAX_DEG 60
#define SERVO_YAW_CENTER_DEG 0
#define SERVO_PITCH_CENTER_DEG 30

esp_err_t servo_init(void);
bool servo_is_available(void);
esp_err_t servo_enable(bool enable);
esp_err_t servo_center(void);
esp_err_t servo_move_to(int yaw_deg, int pitch_deg, uint32_t duration_ms);
esp_err_t servo_nod(void);
esp_err_t servo_shake(void);
esp_err_t servo_tilt(void);
