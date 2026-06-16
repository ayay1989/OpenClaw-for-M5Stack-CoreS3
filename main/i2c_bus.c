#include "i2c_bus.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "i2c_bus";

static SemaphoreHandle_t s_i2c_lock;
static i2c_master_bus_handle_t s_bus_handle;
static i2c_master_dev_handle_t s_devices[128];
static i2c_port_t s_port = I2C_NUM_MAX;
static uint32_t s_scl_speed_hz = 400000;

static int ticks_to_timeout_ms(TickType_t timeout)
{
    if (timeout == portMAX_DELAY) {
        return -1;
    }
    uint32_t ms = pdTICKS_TO_MS(timeout);
    return ms == 0 ? 1 : (int)ms;
}

static esp_err_t ensure_lock(void)
{
    if (s_i2c_lock != NULL) {
        return ESP_OK;
    }
    s_i2c_lock = xSemaphoreCreateMutex();
    return s_i2c_lock == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

static esp_err_t get_device_locked(uint8_t addr, i2c_master_dev_handle_t *out_handle)
{
    if (out_handle == NULL || addr >= 128 || s_bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_devices[addr] == NULL) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = s_scl_speed_hz,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_devices[addr]),
                            TAG, "add device 0x%02X failed", addr);
    }
    *out_handle = s_devices[addr];
    return ESP_OK;
}

esp_err_t cores3_i2c_bus_init(i2c_port_t port, gpio_num_t sda_gpio, gpio_num_t scl_gpio, uint32_t scl_speed_hz)
{
    esp_err_t err = ensure_lock();
    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_i2c_lock, portMAX_DELAY);
    if (s_bus_handle != NULL) {
        xSemaphoreGive(s_i2c_lock);
        return s_port == port ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = port,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_cfg, &s_bus_handle);
    if (err == ESP_OK) {
        s_port = port;
        s_scl_speed_hz = scl_speed_hz == 0 ? 400000 : scl_speed_hz;
    }
    xSemaphoreGive(s_i2c_lock);
    return err;
}

esp_err_t cores3_i2c_write_to_device(i2c_port_t port, uint8_t addr, const uint8_t *data, size_t len, TickType_t timeout)
{
    if (port != s_port || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_i2c_lock, portMAX_DELAY);
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = get_device_locked(addr, &dev);
    if (err == ESP_OK) {
        err = i2c_master_transmit(dev, data, len, ticks_to_timeout_ms(timeout));
    }
    xSemaphoreGive(s_i2c_lock);
    return err;
}

esp_err_t cores3_i2c_write_read_device(i2c_port_t port, uint8_t addr, const uint8_t *write_buffer,
                                       size_t write_len, uint8_t *read_buffer, size_t read_len,
                                       TickType_t timeout)
{
    if (port != s_port || write_buffer == NULL || write_len == 0 || read_buffer == NULL || read_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_i2c_lock, portMAX_DELAY);
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = get_device_locked(addr, &dev);
    if (err == ESP_OK) {
        err = i2c_master_transmit_receive(dev, write_buffer, write_len, read_buffer, read_len,
                                          ticks_to_timeout_ms(timeout));
    }
    xSemaphoreGive(s_i2c_lock);
    return err;
}

esp_err_t cores3_i2c_probe_device(i2c_port_t port, uint8_t addr, TickType_t timeout)
{
    if (port != s_port || s_bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_i2c_lock, portMAX_DELAY);
    esp_err_t err = i2c_master_probe(s_bus_handle, addr, ticks_to_timeout_ms(timeout));
    xSemaphoreGive(s_i2c_lock);
    return err;
}
