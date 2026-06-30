#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t si12t_init(i2c_port_t port);
bool si12t_is_available(void);
