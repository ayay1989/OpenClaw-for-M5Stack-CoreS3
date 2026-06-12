#include "scservo_bus.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "scservo";

#define SCSERVO_HEADER 0xFF
#define SCSERVO_INST_PING 0x01
#define SCSERVO_INST_WRITE_DATA 0x03
#define SCSERVO_REG_TORQUE_ENABLE 40
#define SCSERVO_REG_GOAL_POSITION_L 42
#define SCSERVO_RX_BUF_SIZE 128
#define SCSERVO_TX_BUF_SIZE 128
#define SCSERVO_STATUS_OK 0

static SemaphoreHandle_t s_bus_lock;
static bool s_initialized;
static bool s_available;
static uint8_t s_failures;

static uint8_t checksum_packet(uint8_t id, uint8_t length, uint8_t instruction,
                               const uint8_t *params, size_t param_len)
{
    uint16_t sum = id + length + instruction;
    for (size_t i = 0; i < param_len; ++i) {
        sum += params[i];
    }
    return (uint8_t)(~sum);
}

static esp_err_t send_packet(uint8_t id, uint8_t instruction, const uint8_t *params, size_t param_len)
{
    if (param_len > 250) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t packet[256];
    uint8_t length = (uint8_t)(param_len + 2);
    packet[0] = SCSERVO_HEADER;
    packet[1] = SCSERVO_HEADER;
    packet[2] = id;
    packet[3] = length;
    packet[4] = instruction;
    if (param_len > 0) {
        memcpy(packet + 5, params, param_len);
    }
    packet[5 + param_len] = checksum_packet(id, length, instruction, params, param_len);

    uart_flush_input(SCSERVO_UART_PORT);
    int written = uart_write_bytes(SCSERVO_UART_PORT, packet, param_len + 6);
    if (written != (int)(param_len + 6)) {
        return ESP_FAIL;
    }
    return uart_wait_tx_done(SCSERVO_UART_PORT, pdMS_TO_TICKS(20));
}

static esp_err_t read_status(uint8_t expected_id, uint32_t timeout_ms)
{
    uint8_t rx[SCSERVO_RX_BUF_SIZE];
    int len = uart_read_bytes(SCSERVO_UART_PORT, rx, sizeof(rx), pdMS_TO_TICKS(timeout_ms));
    if (len < 6) {
        return ESP_ERR_TIMEOUT;
    }

    for (int i = 0; i <= len - 6; ++i) {
        if (rx[i] != SCSERVO_HEADER || rx[i + 1] != SCSERVO_HEADER) {
            continue;
        }

        uint8_t id = rx[i + 2];
        uint8_t length = rx[i + 3];
        int packet_len = length + 4;
        if (id != expected_id || i + packet_len > len || length < 2) {
            continue;
        }

        uint8_t error = rx[i + 4];
        uint8_t received_checksum = rx[i + packet_len - 1];
        uint16_t sum = id + length + error;
        for (int j = 0; j < length - 2; ++j) {
            sum += rx[i + 5 + j];
        }
        if ((uint8_t)(~sum) != received_checksum) {
            continue;
        }

        if (error != SCSERVO_STATUS_OK) {
            continue;
        }
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

static void record_result(esp_err_t err)
{
    if (err == ESP_OK) {
        s_available = true;
        s_failures = 0;
        return;
    }

    if (s_failures < UINT8_MAX) {
        s_failures++;
    }
    if (s_failures >= 3) {
        s_available = false;
    }
}

esp_err_t scservo_bus_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_bus_lock = xSemaphoreCreateMutex();
    if (s_bus_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uart_config_t uart_config = {
        .baud_rate = SCSERVO_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(SCSERVO_UART_PORT, SCSERVO_RX_BUF_SIZE,
                                        SCSERVO_TX_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "UART%d driver install failed: %s", SCSERVO_UART_PORT, esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(SCSERVO_UART_PORT, &uart_config);
    if (err == ESP_OK) {
        err = uart_set_pin(SCSERVO_UART_PORT, SCSERVO_TX_GPIO, SCSERVO_RX_GPIO,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        err = gpio_set_pull_mode(SCSERVO_RX_GPIO, GPIO_PULLUP_ONLY);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "UART%d setup failed: %s", SCSERVO_UART_PORT, esp_err_to_name(err));
        uart_driver_delete(SCSERVO_UART_PORT);
        return err;
    }

    s_initialized = true;
    s_available = false;
    s_failures = 0;
    ESP_LOGI(TAG, "SCServo UART%d initialized TX=GPIO%d RX=GPIO%d baud=%d",
             SCSERVO_UART_PORT, SCSERVO_TX_GPIO, SCSERVO_RX_GPIO, SCSERVO_BAUD_RATE);
    return ESP_OK;
}

bool scservo_bus_is_available(void)
{
    return s_initialized && s_available;
}

esp_err_t scservo_bus_ping(uint8_t id)
{
    if (!s_initialized || id == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_bus_lock, portMAX_DELAY);
    esp_err_t err = send_packet(id, SCSERVO_INST_PING, NULL, 0);
    if (err == ESP_OK) {
        err = read_status(id, 30);
    }
    xSemaphoreGive(s_bus_lock);

    record_result(err);
    return err;
}

esp_err_t scservo_bus_enable_torque(uint8_t id, bool enable)
{
    if (!s_initialized || id == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t params[] = {
        SCSERVO_REG_TORQUE_ENABLE,
        enable ? 1 : 0,
    };

    xSemaphoreTake(s_bus_lock, portMAX_DELAY);
    esp_err_t err = send_packet(id, SCSERVO_INST_WRITE_DATA, params, sizeof(params));
    xSemaphoreGive(s_bus_lock);

    record_result(err);
    return err;
}

esp_err_t scservo_bus_write_position(uint8_t id, uint16_t position, uint16_t time_ms, uint16_t speed)
{
    if (!s_initialized || id == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t params[] = {
        SCSERVO_REG_GOAL_POSITION_L,
        (uint8_t)(position & 0xFF),
        (uint8_t)(position >> 8),
        (uint8_t)(time_ms & 0xFF),
        (uint8_t)(time_ms >> 8),
        (uint8_t)(speed & 0xFF),
        (uint8_t)(speed >> 8),
    };

    xSemaphoreTake(s_bus_lock, portMAX_DELAY);
    esp_err_t err = send_packet(id, SCSERVO_INST_WRITE_DATA, params, sizeof(params));
    xSemaphoreGive(s_bus_lock);

    record_result(err);
    return err;
}
