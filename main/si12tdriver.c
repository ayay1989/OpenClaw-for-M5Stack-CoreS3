#include "si12tdriver.h"

#include <stdbool.h>
#include "body_events.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "protocol.h"

static const char *TAG = "si12t";

#define SI12T_I2C_ADDR 0x68
#define SI12T_REG_THRESH_BASE 0x02
#define SI12T_REG_CTRL1 0x08
#define SI12T_REG_CTRL2 0x09
#define SI12T_REG_REF_RST1 0x0A
#define SI12T_REG_REF_RST2 0x0B
#define SI12T_REG_CH_HOLD1 0x0C
#define SI12T_REG_CH_HOLD2 0x0D
#define SI12T_REG_CAL_HOLD1 0x0E
#define SI12T_REG_CAL_HOLD2 0x0F
#define SI12T_REG_STATE 0x10
#define SI12T_CTRL1_BSP_SETTING 0x22
#define SI12T_CTRL2_RESET 0x0F
#define SI12T_CTRL2_NORMAL 0x07
#define SI12T_THRESH_BSP_SETTING 0xCC
#define SI12T_POLL_MS 100
#define SI12T_FAST_CALIBRATION_MS 12000
#define SI12T_TOUCH_COOLDOWN_US 5000000LL
#define SI12T_TIMEOUT_MS 200
#define SI12T_MAX_READ_FAILURES 5
#define SI12T_INIT_STEP(expr) do { \
        err = (expr); \
        if (err != ESP_OK) { \
            ESP_LOGW(TAG, "SI12T init step failed: %s", esp_err_to_name(err)); \
            protocol_set_body_touch_available(false); \
            return err; \
        } \
    } while (0)

static i2c_port_t s_port = I2C_NUM_MAX;
static bool s_available;
static uint8_t s_last_state;

static void si12t_set_available(bool available)
{
    if (s_available == available) {
        return;
    }
    s_available = available;
    protocol_set_body_touch_available(available);
    protocol_emit_hello();
}

static esp_err_t si12t_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buffer[2] = {reg, value};
    return cores3_i2c_write_to_device(s_port, SI12T_I2C_ADDR, buffer, sizeof(buffer),
                                      pdMS_TO_TICKS(SI12T_TIMEOUT_MS));
}

static esp_err_t si12t_read_reg(uint8_t reg, uint8_t *value)
{
    return cores3_i2c_write_read_device(s_port, SI12T_I2C_ADDR, &reg, 1, value, 1,
                                        pdMS_TO_TICKS(SI12T_TIMEOUT_MS));
}

static void si12t_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(SI12T_FAST_CALIBRATION_MS));
    if (si12t_read_reg(SI12T_REG_STATE, &s_last_state) != ESP_OK) {
        ESP_LOGW(TAG, "SI12T state read failed after calibration");
        si12t_set_available(false);
        vTaskDelete(NULL);
        return;
    }
    si12t_set_available(true);
    ESP_LOGI(TAG, "SI12T body touch ready after calibration");

    int64_t last_touch_us = 0;
    int read_failures = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(SI12T_POLL_MS));
        uint8_t state = 0;
        if (si12t_read_reg(SI12T_REG_STATE, &state) != ESP_OK) {
            read_failures++;
            if (read_failures == SI12T_MAX_READ_FAILURES) {
                ESP_LOGW(TAG, "SI12T read failed %d times, marking body touch unavailable", read_failures);
                si12t_set_available(false);
            }
            continue;
        }
        if (!s_available) {
            ESP_LOGI(TAG, "SI12T read recovered, marking body touch available");
            si12t_set_available(true);
        }
        read_failures = 0;

        int64_t now_us = esp_timer_get_time();
        for (int zone = 0; zone < 3; ++zone) {
            uint8_t current = (state >> (zone * 2)) & 0x03;
            uint8_t previous = (s_last_state >> (zone * 2)) & 0x03;
            if (current != 0 && previous == 0 && now_us - last_touch_us > SI12T_TOUCH_COOLDOWN_US) {
                ESP_LOGI(TAG, "SI12T touch zone=%d value=%u", zone, current);
                protocol_emit_pressure_source("pet", "head_si12t", -1, -1, 70);
                last_touch_us = now_us;
                break;
            }
        }
        s_last_state = state;
    }
}

esp_err_t si12t_init(i2c_port_t port)
{
    s_port = port;
    s_available = false;
    esp_err_t err = cores3_i2c_set_device_speed(port, SI12T_I2C_ADDR, 100000);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SI12T I2C speed setup failed: %s", esp_err_to_name(err));
        protocol_set_body_touch_available(false);
        return err;
    }

    err = cores3_i2c_probe_device(port, SI12T_I2C_ADDR, pdMS_TO_TICKS(50));
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "SI12T body touch not found at 0x%02X: %s", SI12T_I2C_ADDR, esp_err_to_name(err));
        protocol_set_body_touch_available(false);
        return ESP_ERR_NOT_FOUND;
    }

    SI12T_INIT_STEP(si12t_write_reg(SI12T_REG_REF_RST1, 0x00));
    SI12T_INIT_STEP(si12t_write_reg(SI12T_REG_CH_HOLD1, 0x00));
    SI12T_INIT_STEP(si12t_write_reg(SI12T_REG_CAL_HOLD1, 0x00));
    SI12T_INIT_STEP(si12t_write_reg(SI12T_REG_REF_RST2, 0x00));
    SI12T_INIT_STEP(si12t_write_reg(SI12T_REG_CH_HOLD2, 0x00));
    SI12T_INIT_STEP(si12t_write_reg(SI12T_REG_CAL_HOLD2, 0x00));
    SI12T_INIT_STEP(si12t_write_reg(SI12T_REG_CTRL2, SI12T_CTRL2_RESET));
    vTaskDelay(pdMS_TO_TICKS(10));
    SI12T_INIT_STEP(si12t_write_reg(SI12T_REG_CTRL2, SI12T_CTRL2_NORMAL));
    SI12T_INIT_STEP(si12t_write_reg(SI12T_REG_CTRL1, SI12T_CTRL1_BSP_SETTING));
    for (uint8_t reg = SI12T_REG_THRESH_BASE; reg <= 0x07; ++reg) {
        SI12T_INIT_STEP(si12t_write_reg(reg, SI12T_THRESH_BSP_SETTING));
    }

    if (xTaskCreate(si12t_task, "si12t_task", 3072, NULL, 5, NULL) != pdPASS) {
        s_available = false;
        protocol_set_body_touch_available(false);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "SI12T body touch detected, waiting %dms calibration", SI12T_FAST_CALIBRATION_MS);
    return ESP_OK;
}

bool si12t_is_available(void)
{
    return s_available;
}
