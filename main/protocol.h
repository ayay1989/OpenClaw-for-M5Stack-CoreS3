#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdbool.h>
#include <stddef.h>

typedef void (*protocol_send_fn_t)(const char *line, void *ctx);

void protocol_init(protocol_send_fn_t sender, void *sender_ctx);
void protocol_handle_line(const char *line, const char *source);
esp_err_t protocol_start_self_test(void);
void protocol_set_tactile_available(bool available);
void protocol_emit_hello(void);
void protocol_emit_button(const char *pin, const char *action);
void protocol_emit_touch(int x, int y);
void protocol_emit_pressure(const char *action, int x, int y, int intensity);
void protocol_emit_gesture(const char *gesture, int x, int y);
void protocol_emit_body_input(const char *input, const char *action, const char *source,
                              int x, int y, int intensity, const char *intent);
void protocol_emit_heartbeat(void);
