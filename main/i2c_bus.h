#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stddef.h>
#include <stdint.h>
#include "driver/i2c.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t cores3_i2c_bus_lock_init(void);
esp_err_t cores3_i2c_write_to_device(i2c_port_t port, uint8_t addr, const uint8_t *data, size_t len, TickType_t timeout);
esp_err_t cores3_i2c_write_read_device(i2c_port_t port, uint8_t addr, const uint8_t *write_buffer,
                                       size_t write_len, uint8_t *read_buffer, size_t read_len,
                                       TickType_t timeout);
