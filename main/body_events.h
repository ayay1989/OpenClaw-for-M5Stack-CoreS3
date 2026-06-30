#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include "cJSON.h"

typedef void (*body_events_send_json_fn_t)(cJSON *root);

void body_events_init(body_events_send_json_fn_t send_json);
void protocol_emit_button(const char *pin, const char *action);
void protocol_emit_touch(int x, int y);
void protocol_emit_pressure(const char *action, int x, int y, int intensity);
void protocol_emit_pressure_source(const char *action, const char *source, int x, int y, int intensity);
void protocol_emit_gesture(const char *gesture, int x, int y);
void protocol_emit_body_input(const char *input, const char *action, const char *source,
                              int x, int y, int intensity, const char *intent);
