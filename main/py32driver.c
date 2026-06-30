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
#define PY32_INIT_ATTEMPTS 10
#define PY32_INIT_RETRY_DELAY_MS 200
#define PY32_INIT_TIMEOUT_MS 200
#define PY32_CONTROL_TIMEOUT_MS 200
#define PY32_LED_TIMEOUT_MS 50

static i2c_port_t s_port = I2C_NUM_MAX;
static bool s_available;
static bool s_led_available;
static bool s_initialized;
static bool s_servo_power_enabled;
static uint8_t s_failures;
static int64_t s_last_led_write_us;
static SemaphoreHandle_t s_lock;

static uint16_t rgb888_to_565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

static esp_err_t write_block_with_timeout_locked(uint8_t reg, const uint8_t *data, size_t len, uint32_t timeout_ms);

static esp_err_t write_block_locked(uint8_t reg, const uint8_t *data, size_t len)
{
    return write_block_with_timeout_locked(reg, data, len, PY32_CONTROL_TIMEOUT_MS);
}

static esp_err_t write_block_with_timeout_locked(uint8_t reg, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    uint8_t buffer[1 + CORES3_PY32_LED_COUNT * 2];
    if (s_port == I2C_NUM_MAX || data == NULL || len + 1 > sizeof(buffer)) {
        return ESP_ERR_INVALID_ARG;
    }
    buffer[0] = reg;
    memcpy(buffer + 1, data, len);
    return cores3_i2c_write_to_device(s_port, CORES3_PY32_I2C_ADDR, buffer, len + 1, pdMS_TO_TICKS(timeout_ms));
}

static esp_err_t read_reg_with_timeout_locked(uint8_t reg, uint8_t *value, uint32_t timeout_ms)
{
    if (s_port == I2C_NUM_MAX || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return cores3_i2c_write_read_device(s_port, CORES3_PY32_I2C_ADDR, &reg, 1, value, 1, pdMS_TO_TICKS(timeout_ms));
}

static esp_err_t read_reg_locked(uint8_t reg, uint8_t *value)
{
    return read_reg_with_timeout_locked(reg, value, PY32_CONTROL_TIMEOUT_MS);
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
    s_led_available = false;
    s_servo_power_enabled = false;
    s_failures = 0;

    uint8_t version = 0;
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < PY32_INIT_ATTEMPTS; ++i) {
        vTaskDelay(pdMS_TO_TICKS(PY32_INIT_RETRY_DELAY_MS));
        err = read_reg_with_timeout_locked(PY32_REG_VERSION, &version, PY32_INIT_TIMEOUT_MS);
        if (err == ESP_OK && version != 0 && version != 0xFF) {
            s_available = true;
            break;
        }
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
        s_led_available = false;
        ESP_LOGW(TAG, "PY32 LED config failed: %s", esp_err_to_name(err));
        ESP_LOGI(TAG, "PY32 initialized at 0x%02X, version=0x%02X; LED ring unavailable", CORES3_PY32_I2C_ADDR, version);
        return ESP_OK;
    }

    s_led_available = true;
    ESP_LOGI(TAG, "PY32 initialized at 0x%02X, version=0x%02X", CORES3_PY32_I2C_ADDR, version);
    return ESP_OK;
}

bool py32_is_available(void)
{
    return s_initialized && s_available;
}

bool py32_led_is_available(void)
{
    return py32_is_available() && s_led_available;
}

bool py32_servo_power_is_enabled(void)
{
    return py32_is_available() && s_servo_power_enabled;
}

static void log_power_regs_locked(const char *phase)
{
    uint8_t mode = 0;
    uint8_t pull = 0;
    uint8_t output = 0;
    uint8_t led_cfg = 0;
    esp_err_t mode_err = read_reg_locked(PY32_REG_PORT_MODE, &mode);
    esp_err_t pull_err = read_reg_locked(PY32_REG_PORT_PULL, &pull);
    esp_err_t output_err = read_reg_locked(PY32_REG_PORT_OUTPUT, &output);
    esp_err_t led_err = read_reg_locked(PY32_REG_LED_CFG, &led_cfg);
    ESP_LOGI(TAG,
             "servo power regs %s: PORT_MODE[0x03]=0x%02X(%s) PORT_PULL[0x05]=0x%02X(%s) PORT_OUTPUT[0x09]=0x%02X(%s) LED_CFG[0x24]=0x%02X(%s)",
             phase,
             mode, esp_err_to_name(mode_err),
             pull, esp_err_to_name(pull_err),
             output, esp_err_to_name(output_err),
             led_cfg, esp_err_to_name(led_err));
}

esp_err_t py32_set_servo_power(bool enabled)
{
    if (!py32_is_available()) {
        return ESP_ERR_NOT_FOUND;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    log_power_regs_locked("before");
    esp_err_t err = set_bit_locked(PY32_REG_PORT_MODE, PY32_VM_EN_BIT, enabled);
    if (err == ESP_OK) {
        err = set_bit_locked(PY32_REG_PORT_OUTPUT, PY32_VM_EN_BIT, enabled);
    }
    if (err == ESP_OK) {
        err = set_bit_locked(PY32_REG_PORT_PULL, PY32_VM_EN_BIT, enabled);
    }
    log_power_regs_locked("after");
    xSemaphoreGive(s_lock);
    if (err == ESP_OK) {
        s_servo_power_enabled = enabled;
        ESP_LOGI(TAG, "servo VM_EN power %s", enabled ? "enabled" : "disabled");
    } else {
        ESP_LOGW(TAG, "servo VM_EN power write failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t py32_write_led_rgb(uint8_t r, uint8_t g, uint8_t b, bool force)
{
    if (!py32_led_is_available()) {
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
    if (!force && s_last_led_write_us != 0 && now_us - s_last_led_write_us < 50000) {
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    esp_err_t err = write_block_with_timeout_locked(PY32_REG_LED_RAM, data, sizeof(data), PY32_LED_TIMEOUT_MS);
    if (err == ESP_OK) {
        uint8_t refresh = CORES3_PY32_LED_COUNT | PY32_LED_REFRESH_BIT;
        err = write_block_with_timeout_locked(PY32_REG_LED_CFG, &refresh, sizeof(refresh), PY32_LED_TIMEOUT_MS);
    }
    if (err == ESP_OK) {
        s_last_led_write_us = now_us;
        s_failures = 0;
    } else {
        s_failures++;
        ESP_LOGW(TAG, "PY32 LED write failed (%u/3): %s", s_failures, esp_err_to_name(err));
        if (s_failures >= 3) {
            s_led_available = false;
            ESP_LOGW(TAG, "PY32 LED ring disabled after repeated write failures; PY32 control remains available");
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}
