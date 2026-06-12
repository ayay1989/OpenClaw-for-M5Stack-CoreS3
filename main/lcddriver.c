#include "lcddriver.h"

#include <string.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "lcd";
static spi_device_handle_t s_lcd;
static SemaphoreHandle_t s_lcd_lock;

static void lcd_cmd(uint8_t cmd)
{
    gpio_set_level(CORES3_LCD_DC_GPIO, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_lcd, &t));
}

static void lcd_data(const void *data, int len)
{
    if (len <= 0) {
        return;
    }
    gpio_set_level(CORES3_LCD_DC_GPIO, 1);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_lcd, &t));
}

static void lcd_data_u8(uint8_t data)
{
    lcd_data(&data, 1);
}

static void lcd_set_window(int x0, int y0, int x1, int y1)
{
    uint8_t data[4];
    lcd_cmd(0x2A);
    data[0] = (uint8_t)(x0 >> 8);
    data[1] = (uint8_t)x0;
    data[2] = (uint8_t)(x1 >> 8);
    data[3] = (uint8_t)x1;
    lcd_data(data, sizeof(data));

    lcd_cmd(0x2B);
    data[0] = (uint8_t)(y0 >> 8);
    data[1] = (uint8_t)y0;
    data[2] = (uint8_t)(y1 >> 8);
    data[3] = (uint8_t)y1;
    lcd_data(data, sizeof(data));

    lcd_cmd(0x2C);
}

esp_err_t lcd_init(void)
{
    s_lcd_lock = xSemaphoreCreateMutex();
    if (s_lcd_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uint64_t output_mask = (1ULL << CORES3_LCD_DC_GPIO);
    if (CORES3_LCD_RST_GPIO >= 0) {
        output_mask |= (1ULL << CORES3_LCD_RST_GPIO);
    }
    if (CORES3_LCD_BL_GPIO >= 0) {
        output_mask |= (1ULL << CORES3_LCD_BL_GPIO);
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = output_mask,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "gpio_config failed");
    if (CORES3_LCD_BL_GPIO >= 0) {
        gpio_set_level(CORES3_LCD_BL_GPIO, 1);
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CORES3_LCD_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = CORES3_LCD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * 2 + 8,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi_bus_initialize failed");

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode = 2,  // CoreS3 reference firmware uses SPI mode 2 for this panel.
        .spics_io_num = CORES3_LCD_CS_GPIO,
        .queue_size = 7,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI3_HOST, &dev_cfg, &s_lcd), TAG, "spi_bus_add_device failed");

    if (CORES3_LCD_RST_GPIO >= 0) {
        gpio_set_level(CORES3_LCD_RST_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(CORES3_LCD_RST_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    lcd_cmd(0x01);  // Software reset
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(0x11);  // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(0x3A);  // Pixel format
    lcd_data_u8(0x55);  // 16-bit RGB565
    lcd_cmd(0x36);  // Memory access control
    lcd_data_u8(0x48);  // Landscape-ish RGB/BGR order for ILI9341 modules
    lcd_cmd(0x21);  // Display inversion on, common for M5Stack LCD panels
    lcd_cmd(0x29);  // Display on
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "ILI9341 LCD initialized");
    return ESP_OK;
}

void lcd_fill_screen(uint16_t color)
{
    uint16_t *line = heap_caps_malloc(LCD_WIDTH * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (line == NULL) {
        ESP_LOGE(TAG, "no DMA memory for fill");
        return;
    }
    uint16_t be = __builtin_bswap16(color);
    for (int x = 0; x < LCD_WIDTH; ++x) {
        line[x] = be;
    }
    xSemaphoreTake(s_lcd_lock, portMAX_DELAY);
    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    for (int y = 0; y < LCD_HEIGHT; ++y) {
        lcd_data(line, LCD_WIDTH * sizeof(uint16_t));
    }
    xSemaphoreGive(s_lcd_lock);
    heap_caps_free(line);
}

void lcd_draw_rgb565_scaled_center(const uint16_t *pixels, int src_w, int src_h, uint16_t bg_color)
{
    if (pixels == NULL || src_w <= 0 || src_h <= 0) {
        return;
    }

    uint16_t *line = heap_caps_malloc(LCD_WIDTH * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (line == NULL) {
        ESP_LOGE(TAG, "no DMA memory for draw");
        return;
    }

    int scale_x = LCD_WIDTH / src_w;
    int scale_y = LCD_HEIGHT / src_h;
    int scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale < 1) {
        scale = 1;
    }
    int dst_w = src_w * scale;
    int dst_h = src_h * scale;
    int off_x = (LCD_WIDTH - dst_w) / 2;
    int off_y = (LCD_HEIGHT - dst_h) / 2;
    uint16_t bg = __builtin_bswap16(bg_color);

    xSemaphoreTake(s_lcd_lock, portMAX_DELAY);
    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    for (int y = 0; y < LCD_HEIGHT; ++y) {
        for (int x = 0; x < LCD_WIDTH; ++x) {
            uint16_t color = bg;
            if (x >= off_x && x < off_x + dst_w && y >= off_y && y < off_y + dst_h) {
                int sx = (x - off_x) / scale;
                int sy = (y - off_y) / scale;
                color = __builtin_bswap16(pixels[sy * src_w + sx]);
            }
            line[x] = color;
        }
        lcd_data(line, LCD_WIDTH * sizeof(uint16_t));
    }
    xSemaphoreGive(s_lcd_lock);
    heap_caps_free(line);
}

void lcd_draw_rgb565_lines(lcd_line_renderer_t renderer, void *ctx)
{
    if (renderer == NULL) {
        return;
    }

    uint16_t *line = heap_caps_malloc(LCD_WIDTH * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (line == NULL) {
        ESP_LOGE(TAG, "no DMA memory for generated frame");
        return;
    }

    xSemaphoreTake(s_lcd_lock, portMAX_DELAY);
    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    for (int y = 0; y < LCD_HEIGHT; ++y) {
        renderer(y, line, LCD_WIDTH, ctx);
        for (int x = 0; x < LCD_WIDTH; ++x) {
            line[x] = __builtin_bswap16(line[x]);
        }
        lcd_data(line, LCD_WIDTH * sizeof(uint16_t));
    }
    xSemaphoreGive(s_lcd_lock);
    heap_caps_free(line);
}
