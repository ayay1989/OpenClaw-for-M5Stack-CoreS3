#include "protocol.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "emotions.h"
#include "lcddriver.h"
#include "leddriver.h"

static const char *TAG = "protocol";
static protocol_send_fn_t s_sender;
static void *s_sender_ctx;

static void send_json(cJSON *root)
{
    if (root == NULL || s_sender == NULL) {
        return;
    }
    char *line = cJSON_PrintUnformatted(root);
    if (line != NULL) {
        s_sender(line, s_sender_ctx);
        cJSON_free(line);
    }
}

static void send_ok(const char *action, const char *value)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "action", action);
    if (value != NULL) {
        cJSON_AddStringToObject(root, "value", value);
    }
    send_json(root);
    cJSON_Delete(root);
}

static void send_error(const char *action, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "action", action ? action : "unknown");
    cJSON_AddStringToObject(root, "message", message);
    send_json(root);
    cJSON_Delete(root);
}

static int json_int_or_default(cJSON *root, const char *name, int fallback)
{
    cJSON *item = cJSON_GetObjectItem(root, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

void protocol_init(protocol_send_fn_t sender, void *sender_ctx)
{
    s_sender = sender;
    s_sender_ctx = sender_ctx;
}

void protocol_handle_line(const char *line, const char *source)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }

    cJSON *root = cJSON_Parse(line);
    if (root == NULL) {
        ESP_LOGW(TAG, "bad JSON from %s: %s", source, line);
        send_error("parse", "invalid json");
        return;
    }

    cJSON *action_json = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action_json)) {
        send_error("unknown", "missing action");
        cJSON_Delete(root);
        return;
    }
    const char *action = action_json->valuestring;

    if (strcmp(action, "emotion") == 0) {
        cJSON *value_json = cJSON_GetObjectItem(root, "value");
        if (!cJSON_IsString(value_json)) {
            send_error(action, "missing value");
        } else {
            const emotion_bitmap_t *emotion = emotion_find(value_json->valuestring);
            if (emotion == NULL) {
                send_error(action, "unknown emotion");
            } else {
                lcd_draw_rgb565_scaled_center(emotion->pixels, EMOTION_BITMAP_WIDTH, EMOTION_BITMAP_HEIGHT, 0x0012);
                send_ok(action, emotion->name);
            }
        }
    } else if (strcmp(action, "led") == 0) {
        int r = json_int_or_default(root, "r", 0);
        int g = json_int_or_default(root, "g", 0);
        int b = json_int_or_default(root, "b", 0);
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
            send_error(action, "rgb out of range");
        } else {
            led_set_color((uint8_t)r, (uint8_t)g, (uint8_t)b);
            send_ok(action, NULL);
        }
    } else if (strcmp(action, "led_effect") == 0) {
        cJSON *effect_json = cJSON_GetObjectItem(root, "effect");
        int r = json_int_or_default(root, "r", 0);
        int g = json_int_or_default(root, "g", 0);
        int b = json_int_or_default(root, "b", 0);
        int speed = json_int_or_default(root, "speed", 3);
        if (!cJSON_IsString(effect_json)) {
            send_error(action, "missing effect");
        } else if (strcmp(effect_json->valuestring, "breath") != 0) {
            send_error(action, "unknown effect");
        } else if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 || speed < 1 || speed > 10) {
            send_error(action, "argument out of range");
        } else {
            led_set_breath((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)speed);
            send_ok(action, effect_json->valuestring);
        }
    } else if (strcmp(action, "ping") == 0) {
        send_ok(action, "pong");
    } else {
        send_error(action, "unknown action");
    }

    cJSON_Delete(root);
}

void protocol_emit_button(const char *pin, const char *action)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "button");
    cJSON_AddStringToObject(root, "pin", pin);
    cJSON_AddStringToObject(root, "action", action);
    send_json(root);
    cJSON_Delete(root);
}

void protocol_emit_touch(int x, int y)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "touch");
    cJSON_AddNumberToObject(root, "x", x);
    cJSON_AddNumberToObject(root, "y", y);
    send_json(root);
    cJSON_Delete(root);
}

void protocol_emit_heartbeat(void)
{
    wifi_ap_record_t ap = {0};
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "heartbeat");
    cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000000LL);
    cJSON_AddNumberToObject(root, "wifi_rssi", rssi);
    send_json(root);
    cJSON_Delete(root);
}
