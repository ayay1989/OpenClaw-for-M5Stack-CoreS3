#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdbool.h>
#include <stdint.h>
#include "driver/uart.h"
#include "esp_err.h"

#define SCSERVO_UART_PORT UART_NUM_1
#define SCSERVO_TX_GPIO 6          // Stackchan CoreS3 servo UART TX
#define SCSERVO_RX_GPIO 7          // Stackchan CoreS3 servo UART RX
#define SCSERVO_BAUD_RATE 1000000  // Feetech/SCS bus speed used by Stackchan-HtSz

esp_err_t scservo_bus_init(void);
bool scservo_bus_is_available(void);
esp_err_t scservo_bus_ping(uint8_t id);
esp_err_t scservo_bus_enable_torque(uint8_t id, bool enable);
esp_err_t scservo_bus_write_position(uint8_t id, uint16_t position, uint16_t time_ms, uint16_t speed);
