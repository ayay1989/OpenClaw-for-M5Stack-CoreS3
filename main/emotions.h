#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    const char *name;
} emotion_bitmap_t;

extern const emotion_bitmap_t g_emotions[];
extern const size_t g_emotion_count;

const emotion_bitmap_t *emotion_find(const char *name);
bool emotion_draw(const char *name);
