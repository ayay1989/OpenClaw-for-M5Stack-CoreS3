#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define CORES3_AUDIO_SAMPLE_RATE 24000  // Stackchan-HtSz CoreS3 audio rate

esp_err_t audio_init(void);
bool audio_is_available(void);
esp_err_t audio_beep(uint32_t frequency_hz, uint32_t duration_ms, uint8_t volume);
esp_err_t audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms);
