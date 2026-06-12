#include "py32driver.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_bus.h"

static const char *TAG = "py32";

#define PY32_REG_VERSION 0x02
#define PY32_REG_PORT_MODE 0x03
#define PY32_REG_PORT_PULL 0x05
#define PY32_REG_PORT_OUTPUT 0x09
#define PY32_REG_LED_CFG 0x24
#define PY32_REG_LED_RAM 0x30
#define PY32_LED_REFRESH_BIT 0x40
#define PY32_VM_EN_BIT 0x01

static i2c_port_t s_port = I2C_NUM_MAX;
static bool s_available;
static bool s_initialized;
static uint8_t s_failures;
static int64_t s_last_led_write_us;
static SemaphoreHandle_t s_lock;

static uint16_t rgb888_to_565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

static esp_err_t write_block_locked(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buffer[1 + CORES3_PY32_LED_COUNT * 2];
    if (s_port == I2C_NUM_MAX || data == NULL || len + 1 > sizeof(buffer)) {
        return ESP_ERR_INVALID_ARG;
    }
    buffer[0] = reg;
    memcpy(buffer + 1, data, len);
    return cores3_i2c_write_to_device(s_port, CORES3_PY32_I2C_ADDR, buffer, len + 1, pdMS_TO_TICKS(100));
}

static esp_err_t read_reg_locked(uint8_t reg, uint8_t *value)
{
    if (s_port == I2C_NUM_MAX || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return cores3_i2c_write_read_device(s_port, CORES3_PY32_I2C_ADDR, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}

static esp_err_t write_reg_locked(uint8_t reg, uint8_t value)
{
    return write_block_locked(reg, &value, sizeof(value));
}

static esp_err_t set_bit_locked(uint8_t reg, uint8_t bit, bool enabled)
{
    uint8_t value = 0;
    esp_err_t err = read_reg_locked(reg, &value);
    if (err != ESP_OK) {
        return err;
    }
    value = enabled ? (value | bit) : (value & (uint8_t)~bit);
    return write_reg_locked(reg, value);
}

esp_err_t py32_init(i2c_port_t port)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_port = port;
    s_initialized = true;
    s_available = false;
    s_failures = 0;

    uint8_t version = 0;
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 5; ++i) {
        err = read_reg_locked(PY32_REG_VERSION, &version);
        if (err == ESP_OK && version != 0 && version != 0xFF) {
            s_available = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!s_available) {
        ESP_LOGW(TAG, "PY32 not available at 0x%02X: %s", CORES3_PY32_I2C_ADDR, esp_err_to_name(err));
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t count = CORES3_PY32_LED_COUNT;
    err = write_reg_locked(PY32_REG_LED_CFG, count);
    xSemaphoreGive(s_lock);

    if (err != ESP_OK) {
        s_available = false;
        ESP_LOGW(TAG, "PY32 LED config failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "PY32 initialized at 0x%02X, version=0x%02X", CORES3_PY32_I2C_ADDR, version);
    return ESP_OK;
}

bool py32_is_available(void)
{
    return s_initialized && s_available;
}

esp_err_t py32_set_servo_power(bool enabled)
{
    if (!py32_is_available()) {
        return ESP_ERR_NOT_FOUND;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = set_bit_locked(PY32_REG_PORT_MODE, PY32_VM_EN_BIT, enabled);
    if (err == ESP_OK) {
        err = set_bit_locked(PY32_REG_PORT_OUTPUT, PY32_VM_EN_BIT, enabled);
    }
    if (err == ESP_OK) {
        err = set_bit_locked(PY32_REG_PORT_PULL, PY32_VM_EN_BIT, enabled);
    }
    xSemaphoreGive(s_lock);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "servo VM_EN power %s", enabled ? "enabled" : "disabled");
    } else {
        ESP_LOGW(TAG, "servo VM_EN power write failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t py32_write_led_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!py32_is_available()) {
        return ESP_ERR_NOT_FOUND;
    }

    uint16_t color = rgb888_to_565(r, g, b);
    uint8_t data[CORES3_PY32_LED_COUNT * 2];
    for (int i = 0; i < CORES3_PY32_LED_COUNT; ++i) {
        data[i * 2] = (uint8_t)(color & 0xFF);
        data[i * 2 + 1] = (uint8_t)(color >> 8);
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now_us = esp_timer_get_time();
    if (s_last_led_write_us != 0 && now_us - s_last_led_write_us < 50000) {
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    esp_err_t err = write_block_locked(PY32_REG_LED_RAM, data, sizeof(data));
    if (err == ESP_OK) {
        uint8_t refresh = CORES3_PY32_LED_COUNT | PY32_LED_REFRESH_BIT;
        err = write_reg_locked(PY32_REG_LED_CFG, refresh);
    }
    if (err == ESP_OK) {
        s_last_led_write_us = now_us;
    }
    xSemaphoreGive(s_lock);

    if (err != ESP_OK) {
        s_failures++;
        ESP_LOGW(TAG, "PY32 LED write failed (%u/3): %s", s_failures, esp_err_to_name(err));
        if (s_failures >= 3) {
            s_available = false;
            ESP_LOGW(TAG, "PY32 disabled after repeated write failures");
        }
    } else {
        s_failures = 0;
    }
    return err;
}
