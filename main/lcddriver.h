#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdint.h>
#include "esp_err.h"

#define LCD_WIDTH 320
#define LCD_HEIGHT 240

// CoreS3 board pins verified against mo-hantang/Stackchan-HtSz.
// Some generic ILI9341 ESP32-S3 examples use MOSI=23/SCLK=18/CS=5/DC=15/RST=12/BL=32;
// those are not the M5Stack CoreS3 board-level LCD pins.
#define CORES3_LCD_MOSI_GPIO 37  // M5CoreS3 LCD SPI MOSI
#define CORES3_LCD_SCLK_GPIO 36  // M5CoreS3 LCD SPI SCLK
#define CORES3_LCD_CS_GPIO    3  // M5CoreS3 LCD chip select
#define CORES3_LCD_DC_GPIO   35  // M5CoreS3 LCD data/command
#define CORES3_LCD_RST_GPIO  -1  // M5CoreS3 LCD reset is routed through AW9523
#define CORES3_LCD_BL_GPIO   -1  // M5CoreS3 backlight is managed by AXP2101 PMIC

esp_err_t lcd_init(void);
void lcd_fill_screen(uint16_t color);
void lcd_draw_rgb565_scaled_center(const uint16_t *pixels, int src_w, int src_h, uint16_t bg_color);

typedef void (*lcd_line_renderer_t)(int y, uint16_t *line, int width, void *ctx);
void lcd_draw_rgb565_lines(lcd_line_renderer_t renderer, void *ctx);
