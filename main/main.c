#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "button.h"
#include "emotions.h"
#include "lcddriver.h"
#include "leddriver.h"
#include "protocol.h"

#define WIFI_SSID CONFIG_OPENCLAW_WIFI_SSID
#define WIFI_PASSWORD CONFIG_OPENCLAW_WIFI_PASSWORD
#define TCP_HOST CONFIG_OPENCLAW_TCP_HOST
#define TCP_PORT CONFIG_OPENCLAW_TCP_PORT

// CoreS3 board pins verified against mo-hantang/Stackchan-HtSz.
// The earlier MOSI=23/SCLK=18/I2C=21/22/IP5306 notes match common ESP32 examples,
// but not the CoreS3 board support used by Stackchan-HtSz.
#define CORES3_INTERNAL_I2C_PORT I2C_NUM_1
#define CORES3_I2C_SDA_GPIO 12  // M5CoreS3 internal I2C SDA
#define CORES3_I2C_SCL_GPIO 11  // M5CoreS3 internal I2C SCL
#define AXP2101_I2C_ADDR 0x34   // CoreS3 PMIC
#define AW9523_I2C_ADDR  0x58   // CoreS3 IO expander; resets LCD and speaker amp
#define FT6336_I2C_ADDR  0x38   // CoreS3 capacitive touch controller
#define IP5306_I2C_ADDR  0x75   // Fallback probe only; not present on normal CoreS3

static const char *TAG = "openclaw";
static EventGroupHandle_t s_wifi_events;
static SemaphoreHandle_t s_sock_lock;
static int s_sock = -1;

#define WIFI_CONNECTED_BIT BIT0

static void set_socket(int sock)
{
    xSemaphoreTake(s_sock_lock, portMAX_DELAY);
    if (s_sock >= 0 && s_sock != sock) {
        close(s_sock);
    }
    s_sock = sock;
    xSemaphoreGive(s_sock_lock);
}

static bool socket_is_active(void)
{
    xSemaphoreTake(s_sock_lock, portMAX_DELAY);
    bool active = s_sock >= 0;
    xSemaphoreGive(s_sock_lock);
    return active;
}

static esp_err_t i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t buffer[2] = {reg, value};
    return i2c_master_write_to_device(CORES3_INTERNAL_I2C_PORT, addr, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
}

static esp_err_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *value)
{
    return i2c_master_write_read_device(CORES3_INTERNAL_I2C_PORT, addr, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}

static esp_err_t i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buffer, size_t len)
{
    return i2c_master_write_read_device(CORES3_INTERNAL_I2C_PORT, addr, &reg, 1, buffer, len, pdMS_TO_TICKS(100));
}

static void protocol_sender(const char *line, void *ctx)
{
    (void)ctx;
    printf("%s\n", line);
    fflush(stdout);

    xSemaphoreTake(s_sock_lock, portMAX_DELAY);
    if (s_sock >= 0) {
        char buffer[384];
        int len = snprintf(buffer, sizeof(buffer), "%s\n", line);
        if (len > 0 && len < (int)sizeof(buffer)) {
            int err = send(s_sock, buffer, len, 0);
            if (err < 0) {
                ESP_LOGW(TAG, "tcp send failed: errno=%d", errno);
                close(s_sock);
                s_sock = -1;
            }
        }
    }
    xSemaphoreGive(s_sock_lock);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected, IP=" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init(void)
{
    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "esp_event_loop_create_default failed");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL), TAG, "wifi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL), TAG, "ip handler failed");

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");
    ESP_LOGI(TAG, "WiFi connecting to SSID=%s", WIFI_SSID);
    return ESP_OK;
}

static esp_err_t i2c_init_internal(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CORES3_I2C_SDA_GPIO,
        .scl_io_num = CORES3_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(CORES3_INTERNAL_I2C_PORT, &conf), TAG, "i2c_param_config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(CORES3_INTERNAL_I2C_PORT, conf.mode, 0, 0, 0), TAG, "i2c_driver_install failed");

    uint8_t axp90 = 0;
    esp_err_t axp = i2c_read_reg(AXP2101_I2C_ADDR, 0x90, &axp90);
    if (axp == ESP_OK) {
        i2c_write_reg(AXP2101_I2C_ADDR, 0x90, axp90 | 0b10110100);
        i2c_write_reg(AXP2101_I2C_ADDR, 0x99, (0b11110 - 5));
        i2c_write_reg(AXP2101_I2C_ADDR, 0x97, (0b11110 - 2));
        i2c_write_reg(AXP2101_I2C_ADDR, 0x69, 0b00110101);
        i2c_write_reg(AXP2101_I2C_ADDR, 0x30, 0b111111);
        i2c_write_reg(AXP2101_I2C_ADDR, 0x90, 0xBF);
        i2c_write_reg(AXP2101_I2C_ADDR, 0x94, 33 - 5);
        i2c_write_reg(AXP2101_I2C_ADDR, 0x95, 33 - 5);
        ESP_LOGI(TAG, "AXP2101 PMIC init done");
    } else {
        ESP_LOGW(TAG, "AXP2101 PMIC not found: %s", esp_err_to_name(axp));
    }

    esp_err_t aw = i2c_write_reg(AW9523_I2C_ADDR, 0x02, 0b00000111);
    if (aw == ESP_OK) {
        i2c_write_reg(AW9523_I2C_ADDR, 0x03, 0b10001111);
        i2c_write_reg(AW9523_I2C_ADDR, 0x04, 0b00011000);
        i2c_write_reg(AW9523_I2C_ADDR, 0x05, 0b00001100);
        i2c_write_reg(AW9523_I2C_ADDR, 0x11, 0b00010000);
        i2c_write_reg(AW9523_I2C_ADDR, 0x12, 0b11111111);
        i2c_write_reg(AW9523_I2C_ADDR, 0x13, 0b11111111);

        i2c_write_reg(AW9523_I2C_ADDR, 0x02, 0b00000011);  // Reset AW88298 amp
        vTaskDelay(pdMS_TO_TICKS(10));
        i2c_write_reg(AW9523_I2C_ADDR, 0x02, 0b00000111);
        vTaskDelay(pdMS_TO_TICKS(50));

        i2c_write_reg(AW9523_I2C_ADDR, 0x03, 0b10000001);  // Reset LCD panel
        vTaskDelay(pdMS_TO_TICKS(20));
        i2c_write_reg(AW9523_I2C_ADDR, 0x03, 0b10000011);
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_LOGI(TAG, "AW9523 IO expander init done");
    } else {
        ESP_LOGW(TAG, "AW9523 init failed: %s", esp_err_to_name(aw));
    }

    uint8_t ip5306_probe = 0;
    esp_err_t ip5306 = i2c_read_reg(IP5306_I2C_ADDR, 0x00, &ip5306_probe);
    ESP_LOGI(TAG, "IP5306 fallback probe: %s", esp_err_to_name(ip5306));
    return ESP_OK;
}

static void handle_rx_bytes(char *line_buf, size_t line_size, size_t *line_len, const char *data, int len, const char *source)
{
    for (int i = 0; i < len; ++i) {
        char ch = data[i];
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            line_buf[*line_len] = '\0';
            if (*line_len > 0) {
                protocol_handle_line(line_buf, source);
            }
            *line_len = 0;
            continue;
        }
        if (*line_len + 1 < line_size) {
            line_buf[(*line_len)++] = ch;
        } else {
            ESP_LOGW(TAG, "%s line too long, dropping", source);
            *line_len = 0;
        }
    }
}

static void serial_task(void *arg)
{
    (void)arg;
    char line[512];
    size_t len = 0;
    while (true) {
        int ch = fgetc(stdin);
        if (ch == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            clearerr(stdin);
            continue;
        }
        char c = (char)ch;
        handle_rx_bytes(line, sizeof(line), &len, &c, 1, "serial");
    }
}

static void tcp_task(void *arg)
{
    (void)arg;
    char rx[256];
    char line[512];
    size_t line_len = 0;

    while (true) {
        xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

        struct sockaddr_in dest = {
            .sin_family = AF_INET,
            .sin_port = htons(TCP_PORT),
        };
        inet_pton(AF_INET, TCP_HOST, &dest.sin_addr);

        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "socket create failed: errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        ESP_LOGI(TAG, "connecting TCP %s:%d", TCP_HOST, TCP_PORT);
        if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
            ESP_LOGW(TAG, "TCP connect failed: errno=%d; retry in 5s", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        struct timeval timeout = {
            .tv_sec = 2,
            .tv_usec = 0,
        };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        ESP_LOGI(TAG, "TCP connected");
        set_socket(sock);
        line_len = 0;

        while (true) {
            int r = recv(sock, rx, sizeof(rx), 0);
            if (r > 0) {
                handle_rx_bytes(line, sizeof(line), &line_len, rx, r, "tcp");
            } else if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                EventBits_t bits = xEventGroupGetBits(s_wifi_events);
                if ((bits & WIFI_CONNECTED_BIT) == 0 || !socket_is_active()) {
                    break;
                }
            } else {
                ESP_LOGW(TAG, "TCP disconnected: recv=%d errno=%d", r, errno);
                break;
            }
        }
        set_socket(-1);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        protocol_emit_heartbeat();
    }
}

static void touch_task(void *arg)
{
    (void)arg;
    uint8_t chip_id = 0;
    esp_err_t err = i2c_read_reg(FT6336_I2C_ADDR, 0xA3, &chip_id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FT6336 not found: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "FT6336 touch initialized, chip_id=0x%02X", chip_id);

    const int64_t short_touch_ms = 500;
    const int64_t long_press_ms = 1500;
    const int64_t double_tap_ms = 500;
    const int swipe_threshold_px = 20;
    const int tap_max_move_px = 8;

    bool was_touched = false;
    bool long_press_sent = false;
    bool pending_tap = false;
    int start_x = -1;
    int start_y = -1;
    int last_x = -1;
    int last_y = -1;
    int64_t start_ms = 0;
    int64_t pending_tap_ms = 0;
    while (true) {
        uint8_t data[6] = {0};
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (pending_tap && now_ms - pending_tap_ms > double_tap_ms) {
            pending_tap = false;
            protocol_emit_gesture("tap", last_x, last_y);
        }

        if (i2c_read_regs(FT6336_I2C_ADDR, 0x02, data, sizeof(data)) == ESP_OK) {
            int points = data[0] & 0x0F;
            if (points > 0) {
                int x = ((data[1] & 0x0F) << 8) | data[2];
                int y = ((data[3] & 0x0F) << 8) | data[4];
                if (!was_touched) {
                    was_touched = true;
                    long_press_sent = false;
                    start_x = x;
                    start_y = y;
                    start_ms = now_ms;
                    protocol_emit_touch(x, y);
                } else if (!long_press_sent && now_ms - start_ms >= long_press_ms &&
                           abs(x - start_x) + abs(y - start_y) <= tap_max_move_px) {
                    long_press_sent = true;
                    pending_tap = false;
                    protocol_emit_gesture("long_press", x, y);
                }
                last_x = x;
                last_y = y;
            } else {
                if (was_touched) {
                    int duration_ms = (int)(now_ms - start_ms);
                    int dx = last_x - start_x;
                    int dy = last_y - start_y;
                    int abs_dx = abs(dx);
                    int abs_dy = abs(dy);

                    if (!long_press_sent) {
                        if (duration_ms < short_touch_ms && (abs_dx >= swipe_threshold_px || abs_dy >= swipe_threshold_px)) {
                            if (abs_dx > abs_dy) {
                                protocol_emit_gesture(dx < 0 ? "swipe_left" : "swipe_right", -1, -1);
                            } else {
                                protocol_emit_gesture(dy < 0 ? "swipe_up" : "swipe_down", -1, -1);
                            }
                            pending_tap = false;
                        } else if (duration_ms < short_touch_ms && abs_dx + abs_dy <= tap_max_move_px) {
                            if (pending_tap && now_ms - pending_tap_ms <= double_tap_ms) {
                                pending_tap = false;
                                protocol_emit_gesture("double_tap", last_x, last_y);
                            } else {
                                pending_tap = true;
                                pending_tap_ms = now_ms;
                            }
                        }
                    }
                    was_touched = false;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_sock_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_sock_lock == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    protocol_init(protocol_sender, NULL);
    ESP_ERROR_CHECK(i2c_init_internal());
    ESP_ERROR_CHECK(lcd_init());
    ESP_ERROR_CHECK(led_init());
    ESP_ERROR_CHECK(button_init());

    emotion_draw("happy");
    led_set_breath(0, 100, 255, 3);

    ESP_ERROR_CHECK(wifi_init());
    xTaskCreate(serial_task, "serial_task", 4096, NULL, 6, NULL);
    xTaskCreate(tcp_task, "tcp_task", 6144, NULL, 7, NULL);
    xTaskCreate(heartbeat_task, "heartbeat_task", 3072, NULL, 5, NULL);
    xTaskCreate(touch_task, "touch_task", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "OpenClaw Stackchan CoreS3 firmware started");
}
