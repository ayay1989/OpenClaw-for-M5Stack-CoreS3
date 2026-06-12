#pragma once

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This firmware is for ESP32-S3 only"
#endif

#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

typedef enum {
    PRESENCE_BOOTING = 0,
    PRESENCE_CONNECTING,
    PRESENCE_ONLINE_IDLE,
    PRESENCE_LISTENING,
    PRESENCE_THINKING,
    PRESENCE_SPEAKING,
    PRESENCE_SLEEPING,
    PRESENCE_OFFLINE_LOCAL,
    PRESENCE_ERROR,
} presence_state_t;

typedef enum {
    CONNECTION_OFFLINE = 0,
    CONNECTION_WIFI_CONNECTED,
    CONNECTION_TCP_CONNECTED,
    CONNECTION_OPENCLAW_READY,
} connection_state_t;

typedef struct {
    presence_state_t presence;
    connection_state_t connection;
    char emotion[16];
    char session_id[48];
    char resident_id[32];
    bool memory_context_loaded;
    uint32_t memory_context_expires_at_ms;
} presence_snapshot_t;

esp_err_t presence_init(void);
bool presence_parse_state(const char *value, presence_state_t *out_state);
const char *presence_state_to_string(presence_state_t state);
const char *presence_connection_to_string(connection_state_t state);
const char *presence_default_emotion(presence_state_t state);
void presence_default_led(presence_state_t state, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *speed);
void presence_set_state(presence_state_t state, const char *emotion);
void presence_set_connection(connection_state_t state);
void presence_set_session(const char *session_id);
void presence_set_resident(const char *resident_id);
void presence_set_memory_context(bool loaded, uint32_t ttl_ms);
void presence_get_snapshot(presence_snapshot_t *snapshot);
void presence_add_json(cJSON *root);
