#include "i2c_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_i2c_lock;

esp_err_t cores3_i2c_bus_lock_init(void)
{
    if (s_i2c_lock != NULL) {
        return ESP_OK;
    }
    s_i2c_lock = xSemaphoreCreateMutex();
    return s_i2c_lock == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t cores3_i2c_write_to_device(i2c_port_t port, uint8_t addr, const uint8_t *data, size_t len, TickType_t timeout)
{
    esp_err_t err = cores3_i2c_bus_lock_init();
    if (err != ESP_OK) {
        return err;
    }
    xSemaphoreTake(s_i2c_lock, portMAX_DELAY);
    err = i2c_master_write_to_device(port, addr, data, len, timeout);
    xSemaphoreGive(s_i2c_lock);
    return err;
}

esp_err_t cores3_i2c_write_read_device(i2c_port_t port, uint8_t addr, const uint8_t *write_buffer,
                                       size_t write_len, uint8_t *read_buffer, size_t read_len,
                                       TickType_t timeout)
{
    esp_err_t err = cores3_i2c_bus_lock_init();
    if (err != ESP_OK) {
        return err;
    }
    xSemaphoreTake(s_i2c_lock, portMAX_DELAY);
    err = i2c_master_write_read_device(port, addr, write_buffer, write_len, read_buffer, read_len, timeout);
    xSemaphoreGive(s_i2c_lock);
    return err;
}

