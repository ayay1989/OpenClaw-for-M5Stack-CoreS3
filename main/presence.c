#include "presence.h"

#include <stddef.h>
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    presence_state_t state;
    const char *name;
    const char *emotion;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t speed;
} presence_profile_t;

typedef struct {
    connection_state_t state;
    const char *name;
} connection_profile_t;

static const presence_profile_t s_presence_profiles[] = {
    {PRESENCE_BOOTING, "booting", "happy", 0, 100, 255, 3},
    {PRESENCE_CONNECTING, "connecting", "normal", 0, 80, 220, 3},
    {PRESENCE_ONLINE_IDLE, "online_idle", "normal", 0, 80, 180, 2},
    {PRESENCE_LISTENING, "listening", "surprised", 0, 200, 255, 4},
    {PRESENCE_THINKING, "thinking", "normal", 90, 40, 220, 2},
    {PRESENCE_SPEAKING, "speaking", "happy", 255, 160, 60, 5},
    {PRESENCE_SLEEPING, "sleeping", "sleepy", 0, 20, 80, 1},
    {PRESENCE_OFFLINE_LOCAL, "offline_local", "sleepy", 0, 30, 110, 1},
    {PRESENCE_ERROR, "error", "sad", 255, 20, 20, 2},
};

static const connection_profile_t s_connection_profiles[] = {
    {CONNECTION_OFFLINE, "offline"},
    {CONNECTION_WIFI_CONNECTED, "wifi_connected"},
    {CONNECTION_TCP_CONNECTED, "tcp_connected"},
    {CONNECTION_OPENCLAW_READY, "openclaw_ready"},
};

static SemaphoreHandle_t s_lock;
static presence_snapshot_t s_state = {
    .presence = PRESENCE_BOOTING,
    .connection = CONNECTION_OFFLINE,
    .emotion = "happy",
    .session_id = "",
    .resident_id = "",
    .memory_context_loaded = false,
    .memory_context_expires_at_ms = 0,
};

static const presence_profile_t *find_presence_profile(presence_state_t state)
{
    for (size_t i = 0; i < sizeof(s_presence_profiles) / sizeof(s_presence_profiles[0]); ++i) {
        if (s_presence_profiles[i].state == state) {
            return &s_presence_profiles[i];
        }
    }
    return &s_presence_profiles[0];
}

static const connection_profile_t *find_connection_profile(connection_state_t state)
{
    for (size_t i = 0; i < sizeof(s_connection_profiles) / sizeof(s_connection_profiles[0]); ++i) {
        if (s_connection_profiles[i].state == state) {
            return &s_connection_profiles[i];
        }
    }
    return &s_connection_profiles[0];
}

static void copy_truncated(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

esp_err_t presence_init(void)
{
    if (s_lock != NULL) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool presence_parse_state(const char *value, presence_state_t *out_state)
{
    if (value == NULL || out_state == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(s_presence_profiles) / sizeof(s_presence_profiles[0]); ++i) {
        if (strcmp(value, s_presence_profiles[i].name) == 0) {
            *out_state = s_presence_profiles[i].state;
            return true;
        }
    }
    if (strcmp(value, "idle") == 0) {
        *out_state = PRESENCE_ONLINE_IDLE;
        return true;
    }
    return false;
}

const char *presence_state_to_string(presence_state_t state)
{
    return find_presence_profile(state)->name;
}

const char *presence_connection_to_string(connection_state_t state)
{
    return find_connection_profile(state)->name;
}

const char *presence_default_emotion(presence_state_t state)
{
    return find_presence_profile(state)->emotion;
}

void presence_default_led(presence_state_t state, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *speed)
{
    const presence_profile_t *profile = find_presence_profile(state);
    if (r != NULL) {
        *r = profile->r;
    }
    if (g != NULL) {
        *g = profile->g;
    }
    if (b != NULL) {
        *b = profile->b;
    }
    if (speed != NULL) {
        *speed = profile->speed;
    }
}

void presence_set_state(presence_state_t state, const char *emotion)
{
    const presence_profile_t *profile = find_presence_profile(state);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.presence = profile->state;
    copy_truncated(s_state.emotion, sizeof(s_state.emotion), emotion != NULL ? emotion : profile->emotion);
    xSemaphoreGive(s_lock);
}

void presence_set_connection(connection_state_t state)
{
    const connection_profile_t *profile = find_connection_profile(state);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.connection = profile->state;
    xSemaphoreGive(s_lock);
}

void presence_set_session(const char *session_id)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    copy_truncated(s_state.session_id, sizeof(s_state.session_id), session_id);
    xSemaphoreGive(s_lock);
}

void presence_set_resident(const char *resident_id)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    copy_truncated(s_state.resident_id, sizeof(s_state.resident_id), resident_id);
    xSemaphoreGive(s_lock);
}

void presence_set_memory_context(bool loaded, uint32_t ttl_ms)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.memory_context_loaded = loaded;
    s_state.memory_context_expires_at_ms = loaded && ttl_ms > 0 ? now_ms + ttl_ms : 0;
    xSemaphoreGive(s_lock);
}

void presence_get_snapshot(presence_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state.memory_context_loaded &&
        s_state.memory_context_expires_at_ms != 0 &&
        (int32_t)(now_ms - s_state.memory_context_expires_at_ms) >= 0) {
        s_state.memory_context_loaded = false;
        s_state.memory_context_expires_at_ms = 0;
    }
    *snapshot = s_state;
    xSemaphoreGive(s_lock);
}

void presence_add_json(cJSON *root)
{
    if (root == NULL) {
        return;
    }
    presence_snapshot_t snapshot;
    presence_get_snapshot(&snapshot);
    cJSON_AddStringToObject(root, "presence_state", presence_state_to_string(snapshot.presence));
    cJSON_AddStringToObject(root, "connection_state", presence_connection_to_string(snapshot.connection));
    cJSON_AddStringToObject(root, "emotion", snapshot.emotion);
    if (snapshot.session_id[0] != '\0') {
        cJSON_AddStringToObject(root, "session_id", snapshot.session_id);
    }
    if (snapshot.resident_id[0] != '\0') {
        cJSON_AddStringToObject(root, "resident_id", snapshot.resident_id);
    }
    cJSON_AddBoolToObject(root, "memory_context_loaded", snapshot.memory_context_loaded);
}
