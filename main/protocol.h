#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stddef.h>

typedef void (*protocol_send_fn_t)(const char *line, void *ctx);

void protocol_init(protocol_send_fn_t sender, void *sender_ctx);
void protocol_handle_line(const char *line, const char *source);
void protocol_emit_button(const char *pin, const char *action);
void protocol_emit_touch(int x, int y);
void protocol_emit_heartbeat(void);
