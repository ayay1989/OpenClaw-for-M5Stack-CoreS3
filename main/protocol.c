#include "protocol.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "audiodriver.h"
#include "body_service.h"
#include "emotions.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lcddriver.h"
#include "leddriver.h"
#include "mbedtls/base64.h"
#include "presence.h"

static const char *TAG = "protocol";
static const char *FIRMWARE_VERSION = "1.0.0";
static protocol_send_fn_t s_sender;
static void *s_sender_ctx;
static bool s_tactile_available;

typedef struct {
    presence_state_t state;
    char emotion[16];
    bool mouth_open;
} visual_update_t;

typedef struct {
    bool active;
    char stream_id[32];
    int sample_rate;
    int channels;
} audio_stream_state_t;

static QueueHandle_t s_visual_queue;
static audio_stream_state_t s_audio_stream;
static bool s_self_test_running;

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

static bool json_rgb_args_valid(int r, int g, int b)
{
    return r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255;
}

static const char *motion_error_message(esp_err_t err)
{
    if (err == ESP_ERR_NOT_FOUND) {
        return "motion unavailable";
    }
    if (err == ESP_ERR_TIMEOUT) {
        return "motion busy";
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return "invalid motion";
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return "motion not initialized";
    }
    return esp_err_to_name(err);
}

static const char *audio_error_message(esp_err_t err)
{
    if (err == ESP_ERR_NOT_SUPPORTED) {
        return "audio unavailable";
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return "invalid audio";
    }
    if (err == ESP_ERR_TIMEOUT) {
        return "audio busy";
    }
    return esp_err_to_name(err);
}

static void copy_small(char *dst, size_t dst_len, const char *src)
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

static esp_err_t write_pcm_chunk(const uint8_t *pcm, size_t pcm_len, int channels)
{
    if (pcm == NULL || pcm_len == 0 || (pcm_len % sizeof(int16_t)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (channels == 2) {
        return audio_write_pcm((const int16_t *)pcm, pcm_len / sizeof(int16_t), 500);
    }
    if (channels != 1) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t mono_samples = pcm_len / sizeof(int16_t);
    int16_t *stereo = malloc(mono_samples * 2 * sizeof(int16_t));
    if (stereo == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const int16_t *mono = (const int16_t *)pcm;
    for (size_t i = 0; i < mono_samples; ++i) {
        stereo[i * 2] = mono[i];
        stereo[i * 2 + 1] = mono[i];
    }
    esp_err_t err = audio_write_pcm(stereo, mono_samples * 2, 500);
    free(stereo);
    return err;
}

static void handle_audio_stream(cJSON *root, const char *action)
{
    cJSON *op_json = cJSON_GetObjectItem(root, "op");
    cJSON *stream_json = cJSON_GetObjectItem(root, "stream_id");
    if (!cJSON_IsString(op_json) || !cJSON_IsString(stream_json)) {
        send_error(action, "audio_stream requires op and stream_id");
        return;
    }
    if (!audio_is_available()) {
        send_error(action, "audio unavailable");
        return;
    }

    const char *op = op_json->valuestring;
    const char *stream_id = stream_json->valuestring;
    if (strcmp(op, "start") == 0) {
        int sample_rate = json_int_or_default(root, "sample_rate", CORES3_AUDIO_SAMPLE_RATE);
        int channels = json_int_or_default(root, "channels", 1);
        cJSON *format_json = cJSON_GetObjectItem(root, "format");
        const char *format = cJSON_IsString(format_json) ? format_json->valuestring : "pcm_s16le";
        if (sample_rate != CORES3_AUDIO_SAMPLE_RATE || (channels != 1 && channels != 2) ||
            strcmp(format, "pcm_s16le") != 0) {
            send_error(action, "audio_stream requires pcm_s16le, 24000Hz, 1 or 2 channels");
            return;
        }
        s_audio_stream.active = true;
        copy_small(s_audio_stream.stream_id, sizeof(s_audio_stream.stream_id), stream_id);
        s_audio_stream.sample_rate = sample_rate;
        s_audio_stream.channels = channels;
        send_ok(action, "started");
        return;
    }
    if (strcmp(op, "stop") == 0) {
        s_audio_stream.active = false;
        s_audio_stream.stream_id[0] = '\0';
        send_ok(action, "stopped");
        return;
    }
    if (strcmp(op, "chunk") != 0) {
        send_error(action, "unknown audio_stream op");
        return;
    }
    if (!s_audio_stream.active || strcmp(s_audio_stream.stream_id, stream_id) != 0) {
        send_error(action, "audio stream not started");
        return;
    }
    cJSON *data_json = cJSON_GetObjectItem(root, "data_b64");
    if (!cJSON_IsString(data_json)) {
        send_error(action, "audio_stream chunk requires data_b64");
        return;
    }

    size_t out_len = 0;
    const unsigned char *data = (const unsigned char *)data_json->valuestring;
    size_t data_len = strlen(data_json->valuestring);
    int rc = mbedtls_base64_decode(NULL, 0, &out_len, data, data_len);
    if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || out_len == 0 || out_len > 8192) {
        send_error(action, "invalid audio chunk");
        return;
    }
    uint8_t *decoded = malloc(out_len);
    if (decoded == NULL) {
        send_error(action, "no memory for audio chunk");
        return;
    }
    rc = mbedtls_base64_decode(decoded, out_len, &out_len, data, data_len);
    if (rc != 0) {
        free(decoded);
        send_error(action, "invalid base64 audio chunk");
        return;
    }
    esp_err_t err = write_pcm_chunk(decoded, out_len, s_audio_stream.channels);
    free(decoded);
    if (err == ESP_OK) {
        send_ok(action, "chunk");
    } else {
        send_error(action, audio_error_message(err));
    }
}

static void apply_presence_visuals_now(presence_state_t state, const char *emotion, bool mouth_open)
{
    const char *draw_emotion = emotion != NULL ? emotion : presence_default_emotion(state);
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t speed = 1;
    presence_default_led(state, &r, &g, &b, &speed);
    emotion_draw_presence(draw_emotion, mouth_open || state == PRESENCE_SPEAKING, 0, 0);
    led_set_breath(r, g, b, speed);
    body_apply_presence(state);
}

static void visual_task(void *arg)
{
    (void)arg;
    visual_update_t update;
    while (true) {
        if (xQueueReceive(s_visual_queue, &update, portMAX_DELAY) == pdTRUE) {
            const char *emotion = update.emotion[0] != '\0' ? update.emotion : NULL;
            apply_presence_visuals_now(update.state, emotion, update.mouth_open);
        }
    }
}

static void apply_presence_visuals(presence_state_t state, const char *emotion, bool mouth_open)
{
    if (s_visual_queue == NULL) {
        apply_presence_visuals_now(state, emotion, mouth_open);
        return;
    }
    visual_update_t update = {
        .state = state,
        .mouth_open = mouth_open,
    };
    copy_small(update.emotion, sizeof(update.emotion), emotion);
    xQueueOverwrite(s_visual_queue, &update);
}

static void self_test_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "body self-test started");

    apply_presence_visuals_now(PRESENCE_LISTENING, "surprised", false);
    led_set_color(255, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(700));

    apply_presence_visuals_now(PRESENCE_SPEAKING, "love", true);
    led_set_color(0, 255, 0);
    vTaskDelay(pdMS_TO_TICKS(700));

    apply_presence_visuals_now(PRESENCE_ONLINE_IDLE, "happy", false);
    led_set_color(0, 0, 255);
    vTaskDelay(pdMS_TO_TICKS(700));

    esp_err_t motion_err = body_motion_available() ? body_motion_gesture("nod") : ESP_ERR_NOT_FOUND;
    esp_err_t audio_err = audio_is_available() ? audio_beep(880, 180, 45) : ESP_ERR_NOT_SUPPORTED;
    led_set_breath(0, 100, 255, 3);
    presence_set_state(PRESENCE_ONLINE_IDLE, "happy");
    apply_presence_visuals_now(PRESENCE_ONLINE_IDLE, "happy", false);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "self_test");
    cJSON_AddBoolToObject(root, "display", true);
    cJSON_AddBoolToObject(root, "led", true);
    cJSON_AddBoolToObject(root, "motion_available", body_motion_available());
    cJSON_AddStringToObject(root, "motion_result", motion_err == ESP_OK ? "ok" : motion_error_message(motion_err));
    cJSON_AddBoolToObject(root, "audio_out_available", audio_is_available());
    cJSON_AddStringToObject(root, "audio_result", audio_err == ESP_OK ? "ok" : audio_error_message(audio_err));
    presence_add_json(root);
    send_json(root);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "body self-test finished, motion=%s audio=%s",
             motion_err == ESP_OK ? "ok" : motion_error_message(motion_err),
             audio_err == ESP_OK ? "ok" : audio_error_message(audio_err));
    s_self_test_running = false;
    vTaskDelete(NULL);
}

esp_err_t protocol_start_self_test(void)
{
    if (s_self_test_running) {
        return ESP_ERR_INVALID_STATE;
    }
    s_self_test_running = true;
    if (xTaskCreate(self_test_task, "self_test", 4096, NULL, 4, NULL) != pdPASS) {
        s_self_test_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static const char *gesture_intent(const char *gesture)
{
    if (gesture == NULL) {
        return "touch";
    }
    if (strcmp(gesture, "double_tap") == 0) {
        return "summon";
    }
    if (strcmp(gesture, "long_press") == 0) {
        return "sleep_toggle";
    }
    if (strncmp(gesture, "swipe_", 6) == 0) {
        return "browse_mood";
    }
    return "touch";
}

static const char *button_intent(const char *pin, const char *action)
{
    if (pin == NULL || action == NULL || strcmp(action, "press") != 0) {
        return "button";
    }
    if (strcmp(pin, "A") == 0) {
        return "wake";
    }
    if (strcmp(pin, "B") == 0) {
        return "interrupt";
    }
    if (strcmp(pin, "C") == 0) {
        return "safe_action";
    }
    return "button";
}

static void apply_local_intent(const char *intent)
{
    if (intent == NULL) {
        return;
    }
    if (strcmp(intent, "summon") == 0 || strcmp(intent, "wake") == 0) {
        presence_set_state(PRESENCE_LISTENING, NULL);
        apply_presence_visuals(PRESENCE_LISTENING, NULL, false);
    } else if (strcmp(intent, "interrupt") == 0) {
        presence_set_state(PRESENCE_ONLINE_IDLE, "normal");
        apply_presence_visuals(PRESENCE_ONLINE_IDLE, "normal", false);
    } else if (strcmp(intent, "sleep_toggle") == 0) {
        presence_snapshot_t snapshot;
        presence_get_snapshot(&snapshot);
        presence_state_t next = snapshot.presence == PRESENCE_SLEEPING ? PRESENCE_ONLINE_IDLE : PRESENCE_SLEEPING;
        presence_set_state(next, NULL);
        apply_presence_visuals(next, NULL, false);
    } else if (strcmp(intent, "touch") == 0) {
        presence_set_state(PRESENCE_ONLINE_IDLE, "happy");
        apply_presence_visuals(PRESENCE_ONLINE_IDLE, "happy", false);
    }
}

static void mcp_add_id(cJSON *payload, cJSON *id)
{
    if (id != NULL) {
        cJSON_AddItemToObject(payload, "id", cJSON_Duplicate(id, true));
    }
}

static void mcp_send_payload(cJSON *payload)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        cJSON_Delete(payload);
        return;
    }
    cJSON_AddStringToObject(root, "type", "mcp");
    cJSON_AddItemToObject(root, "payload", payload);
    send_json(root);
    cJSON_Delete(root);
}

static void mcp_send_error(cJSON *id, int code, const char *message)
{
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "jsonrpc", "2.0");
    mcp_add_id(payload, id);
    cJSON *error = cJSON_CreateObject();
    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    cJSON_AddItemToObject(payload, "error", error);
    mcp_send_payload(payload);
}

static void mcp_send_result(cJSON *id, cJSON *result)
{
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "jsonrpc", "2.0");
    mcp_add_id(payload, id);
    cJSON_AddItemToObject(payload, "result", result);
    mcp_send_payload(payload);
}

static cJSON *mcp_tool_schema_object(void)
{
    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON_AddItemToObject(schema, "properties", cJSON_CreateObject());
    return schema;
}

static cJSON *mcp_tool_text_result(const char *text)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", text);
    cJSON_AddItemToArray(content, item);
    cJSON_AddItemToObject(result, "content", content);
    cJSON_AddBoolToObject(result, "isError", false);
    return result;
}

static void mcp_add_tool(cJSON *tools, const char *name, const char *description, cJSON *schema)
{
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "name", name);
    cJSON_AddStringToObject(tool, "description", description);
    cJSON_AddItemToObject(tool, "inputSchema", schema);
    cJSON_AddItemToArray(tools, tool);
}

static cJSON *mcp_schema_emotion_set(void)
{
    cJSON *schema = mcp_tool_schema_object();
    cJSON *props = cJSON_GetObjectItem(schema, "properties");
    cJSON *value = cJSON_CreateObject();
    cJSON_AddStringToObject(value, "type", "string");
    cJSON_AddStringToObject(value, "description", "Emotion name: happy, normal, sad, angry, surprised, sleepy, shy, love");
    cJSON_AddItemToObject(props, "value", value);
    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("value"));
    cJSON_AddItemToObject(schema, "required", required);
    return schema;
}

static cJSON *mcp_schema_rgb(bool include_speed)
{
    cJSON *schema = mcp_tool_schema_object();
    cJSON *props = cJSON_GetObjectItem(schema, "properties");
    const char *names[] = {"r", "g", "b", "speed"};
    int count = include_speed ? 4 : 3;
    for (int i = 0; i < count; ++i) {
        cJSON *field = cJSON_CreateObject();
        cJSON_AddStringToObject(field, "type", "integer");
        cJSON_AddNumberToObject(field, "minimum", i == 3 ? 1 : 0);
        cJSON_AddNumberToObject(field, "maximum", i == 3 ? 10 : 255);
        cJSON_AddItemToObject(props, names[i], field);
    }
    cJSON *required = cJSON_CreateArray();
    for (int i = 0; i < count; ++i) {
        cJSON_AddItemToArray(required, cJSON_CreateString(names[i]));
    }
    cJSON_AddItemToObject(schema, "required", required);
    return schema;
}

static cJSON *mcp_schema_presence_set(void)
{
    cJSON *schema = mcp_tool_schema_object();
    cJSON *props = cJSON_GetObjectItem(schema, "properties");
    cJSON *state = cJSON_CreateObject();
    cJSON_AddStringToObject(state, "type", "string");
    cJSON_AddStringToObject(state, "description", "Presence state: online_idle, listening, thinking, speaking, sleeping, offline_local, error");
    cJSON_AddItemToObject(props, "state", state);
    cJSON *emotion = cJSON_CreateObject();
    cJSON_AddStringToObject(emotion, "type", "string");
    cJSON_AddStringToObject(emotion, "description", "Optional emotion override.");
    cJSON_AddItemToObject(props, "emotion", emotion);
    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("state"));
    cJSON_AddItemToObject(schema, "required", required);
    return schema;
}

static cJSON *mcp_schema_motion_look_at(void)
{
    cJSON *schema = mcp_tool_schema_object();
    cJSON *props = cJSON_GetObjectItem(schema, "properties");
    cJSON *yaw = cJSON_CreateObject();
    cJSON_AddStringToObject(yaw, "type", "integer");
    cJSON_AddNumberToObject(yaw, "minimum", -45);
    cJSON_AddNumberToObject(yaw, "maximum", 45);
    cJSON_AddItemToObject(props, "yaw", yaw);
    cJSON *pitch = cJSON_CreateObject();
    cJSON_AddStringToObject(pitch, "type", "integer");
    cJSON_AddNumberToObject(pitch, "minimum", 5);
    cJSON_AddNumberToObject(pitch, "maximum", 60);
    cJSON_AddItemToObject(props, "pitch", pitch);
    cJSON *duration = cJSON_CreateObject();
    cJSON_AddStringToObject(duration, "type", "integer");
    cJSON_AddNumberToObject(duration, "minimum", 50);
    cJSON_AddNumberToObject(duration, "maximum", 3000);
    cJSON_AddItemToObject(props, "duration_ms", duration);
    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("yaw"));
    cJSON_AddItemToArray(required, cJSON_CreateString("pitch"));
    cJSON_AddItemToObject(schema, "required", required);
    return schema;
}

static cJSON *mcp_schema_audio_beep(void)
{
    cJSON *schema = mcp_tool_schema_object();
    cJSON *props = cJSON_GetObjectItem(schema, "properties");
    const char *names[] = {"frequency_hz", "duration_ms", "volume"};
    int min[] = {80, 20, 0};
    int max[] = {4000, 2000, 100};
    for (int i = 0; i < 3; ++i) {
        cJSON *field = cJSON_CreateObject();
        cJSON_AddStringToObject(field, "type", "integer");
        cJSON_AddNumberToObject(field, "minimum", min[i]);
        cJSON_AddNumberToObject(field, "maximum", max[i]);
        cJSON_AddItemToObject(props, names[i], field);
    }
    return schema;
}

static void mcp_handle_initialize(cJSON *id)
{
    cJSON *result = cJSON_CreateObject();
    if (result == NULL) {
        return;
    }
    cJSON_AddStringToObject(result, "protocolVersion", "2024-11-05");
    cJSON *capabilities = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateObject();
    cJSON *server_info = cJSON_CreateObject();
    if (capabilities == NULL || tools == NULL || server_info == NULL) {
        cJSON_Delete(result);
        cJSON_Delete(capabilities);
        cJSON_Delete(tools);
        cJSON_Delete(server_info);
        return;
    }
    cJSON_AddItemToObject(capabilities, "tools", tools);
    cJSON_AddItemToObject(result, "capabilities", capabilities);
    cJSON_AddStringToObject(server_info, "name", "openclaw-stackchan-cores3");
    cJSON_AddStringToObject(server_info, "version", FIRMWARE_VERSION);
    cJSON_AddItemToObject(result, "serverInfo", server_info);
    mcp_send_result(id, result);
}

static void mcp_handle_tools_list(cJSON *id)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();
    mcp_add_tool(tools, "self.device.ping", "Check whether the CoreS3 firmware is alive.", mcp_tool_schema_object());
    mcp_add_tool(tools, "self.device.get_status", "Read uptime and WiFi RSSI.", mcp_tool_schema_object());
    mcp_add_tool(tools, "self.emotion.set", "Set the fullscreen Stackchan-style face emotion.", mcp_schema_emotion_set());
    mcp_add_tool(tools, "self.presence.set", "Set the OpenClaw resident presence state.", mcp_schema_presence_set());
    mcp_add_tool(tools, "self.led.set_color", "Set the external SK6812/NeoPixel color.", mcp_schema_rgb(false));
    mcp_add_tool(tools, "self.led.breath", "Set the external SK6812/NeoPixel breathing effect.", mcp_schema_rgb(true));
    if (body_motion_available()) {
        mcp_add_tool(tools, "self.motion.look_at", "Move Stackchan head to yaw/pitch angles.", mcp_schema_motion_look_at());
        mcp_add_tool(tools, "self.motion.center", "Move Stackchan head to the safe center position.", mcp_tool_schema_object());
        mcp_add_tool(tools, "self.motion.nod", "Run a short nod gesture.", mcp_tool_schema_object());
        mcp_add_tool(tools, "self.motion.shake", "Run a short shake gesture.", mcp_tool_schema_object());
        mcp_add_tool(tools, "self.motion.tilt", "Run a short tilt gesture.", mcp_tool_schema_object());
    }
    if (audio_is_available()) {
        mcp_add_tool(tools, "self.audio.beep", "Play a short speaker beep.", mcp_schema_audio_beep());
    }
    cJSON_AddItemToObject(result, "tools", tools);
    cJSON_AddStringToObject(result, "nextCursor", "");
    mcp_send_result(id, result);
}

static void mcp_handle_tool_call(cJSON *id, cJSON *params)
{
    cJSON *name_json = cJSON_GetObjectItem(params, "name");
    cJSON *args = cJSON_GetObjectItem(params, "arguments");
    if (!cJSON_IsString(name_json)) {
        mcp_send_error(id, -32602, "Missing tool name");
        return;
    }
    if (args == NULL) {
        args = cJSON_CreateObject();
    }

    const char *name = name_json->valuestring;
    if (strcmp(name, "self.device.ping") == 0) {
        mcp_send_result(id, mcp_tool_text_result("pong"));
    } else if (strcmp(name, "self.device.get_status") == 0) {
        wifi_ap_record_t ap = {0};
        int rssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            rssi = ap.rssi;
        }
        presence_snapshot_t snapshot;
        presence_get_snapshot(&snapshot);
        char status[256];
        snprintf(status, sizeof(status),
                 "{\"uptime\":%lld,\"wifi_rssi\":%d,\"presence_state\":\"%s\",\"connection_state\":\"%s\",\"emotion\":\"%s\",\"motion_available\":%s,\"audio_out_available\":%s}",
                 esp_timer_get_time() / 1000000LL, rssi,
                 presence_state_to_string(snapshot.presence),
                 presence_connection_to_string(snapshot.connection),
                 snapshot.emotion,
                 body_motion_available() ? "true" : "false",
                 audio_is_available() ? "true" : "false");
        mcp_send_result(id, mcp_tool_text_result(status));
    } else if (strcmp(name, "self.emotion.set") == 0) {
        cJSON *value_json = cJSON_GetObjectItem(args, "value");
        if (!cJSON_IsString(value_json)) {
            value_json = cJSON_GetObjectItem(args, "emotion");
        }
        if (!cJSON_IsString(value_json)) {
            mcp_send_error(id, -32602, "Missing emotion value");
        } else if (!emotion_draw(value_json->valuestring)) {
            mcp_send_error(id, -32602, "Unknown emotion");
        } else {
            const emotion_bitmap_t *emotion = emotion_find(value_json->valuestring);
            if (emotion != NULL) {
                presence_set_state(PRESENCE_ONLINE_IDLE, emotion->name);
            }
            mcp_send_result(id, mcp_tool_text_result(value_json->valuestring));
        }
    } else if (strcmp(name, "self.presence.set") == 0) {
        cJSON *state_json = cJSON_GetObjectItem(args, "state");
        cJSON *emotion_json = cJSON_GetObjectItem(args, "emotion");
        presence_state_t state;
        const char *emotion = cJSON_IsString(emotion_json) ? emotion_json->valuestring : NULL;
        if (!cJSON_IsString(state_json) || !presence_parse_state(state_json->valuestring, &state)) {
            mcp_send_error(id, -32602, "Unknown presence state");
        } else if (emotion != NULL && emotion_find(emotion) == NULL) {
            mcp_send_error(id, -32602, "Unknown emotion");
        } else {
            presence_set_state(state, emotion);
            apply_presence_visuals(state, emotion, false);
            mcp_send_result(id, mcp_tool_text_result(presence_state_to_string(state)));
        }
    } else if (strcmp(name, "self.led.set_color") == 0) {
        int r = json_int_or_default(args, "r", -1);
        int g = json_int_or_default(args, "g", -1);
        int b = json_int_or_default(args, "b", -1);
        if (!json_rgb_args_valid(r, g, b)) {
            mcp_send_error(id, -32602, "RGB values must be 0..255");
        } else {
            led_set_color((uint8_t)r, (uint8_t)g, (uint8_t)b);
            mcp_send_result(id, mcp_tool_text_result("ok"));
        }
    } else if (strcmp(name, "self.led.breath") == 0) {
        int r = json_int_or_default(args, "r", -1);
        int g = json_int_or_default(args, "g", -1);
        int b = json_int_or_default(args, "b", -1);
        int speed = json_int_or_default(args, "speed", 3);
        if (!json_rgb_args_valid(r, g, b) || speed < 1 || speed > 10) {
            mcp_send_error(id, -32602, "RGB values must be 0..255 and speed must be 1..10");
        } else {
            led_set_breath((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)speed);
            mcp_send_result(id, mcp_tool_text_result("ok"));
        }
    } else if (strcmp(name, "self.motion.look_at") == 0) {
        int yaw = json_int_or_default(args, "yaw", 1000);
        int pitch = json_int_or_default(args, "pitch", 1000);
        int duration = json_int_or_default(args, "duration_ms", 350);
        if (yaw < -45 || yaw > 45 || pitch < 5 || pitch > 60 || duration < 50 || duration > 3000) {
            mcp_send_error(id, -32602, "yaw must be -45..45, pitch 5..60, duration_ms 50..3000");
        } else {
            esp_err_t err = body_look_at(yaw, pitch, (uint32_t)duration);
            if (err == ESP_OK) {
                mcp_send_result(id, mcp_tool_text_result("queued"));
            } else {
                mcp_send_error(id, -32000, motion_error_message(err));
            }
        }
    } else if (strcmp(name, "self.motion.center") == 0 || strcmp(name, "self.motion.nod") == 0 ||
               strcmp(name, "self.motion.shake") == 0 || strcmp(name, "self.motion.tilt") == 0) {
        const char *gesture = strrchr(name, '.');
        gesture = gesture != NULL ? gesture + 1 : "center";
        esp_err_t err = body_motion_gesture(gesture);
        if (err == ESP_OK) {
            mcp_send_result(id, mcp_tool_text_result("queued"));
        } else {
            mcp_send_error(id, -32000, motion_error_message(err));
        }
    } else if (strcmp(name, "self.audio.beep") == 0) {
        int frequency = json_int_or_default(args, "frequency_hz", json_int_or_default(args, "freq", 880));
        int duration = json_int_or_default(args, "duration_ms", 120);
        int volume = json_int_or_default(args, "volume", 30);
        if (frequency < 80 || frequency > 4000 || duration < 20 || duration > 2000 || volume < 0 || volume > 100) {
            mcp_send_error(id, -32602, "frequency_hz 80..4000, duration_ms 20..2000, volume 0..100");
        } else {
            esp_err_t err = audio_beep((uint32_t)frequency, (uint32_t)duration, (uint8_t)volume);
            if (err == ESP_OK) {
                mcp_send_result(id, mcp_tool_text_result("ok"));
            } else {
                mcp_send_error(id, -32000, audio_error_message(err));
            }
        }
    } else {
        mcp_send_error(id, -32601, "Unknown tool");
    }

    if (args != cJSON_GetObjectItem(params, "arguments")) {
        cJSON_Delete(args);
    }
}

static bool protocol_handle_mcp(cJSON *root)
{
    cJSON *payload = root;
    cJSON *type_json = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type_json) && strcmp(type_json->valuestring, "mcp") == 0) {
        payload = cJSON_GetObjectItem(root, "payload");
    }
    if (!cJSON_IsObject(payload)) {
        return false;
    }

    cJSON *method_json = cJSON_GetObjectItem(payload, "method");
    if (!cJSON_IsString(method_json)) {
        return false;
    }

    cJSON *jsonrpc = cJSON_GetObjectItem(payload, "jsonrpc");
    cJSON *id = cJSON_GetObjectItem(payload, "id");
    bool has_id = cJSON_HasObjectItem(payload, "id");
    const char *method = method_json->valuestring;

    if (jsonrpc != NULL && (!cJSON_IsString(jsonrpc) || strcmp(jsonrpc->valuestring, "2.0") != 0)) {
        if (has_id) {
            mcp_send_error(id, -32600, "Invalid JSON-RPC version");
        }
        return true;
    }

    if (!has_id) {
        if (strcmp(method, "notifications/initialized") == 0 || strcmp(method, "initialized") == 0) {
            ESP_LOGI(TAG, "MCP initialized notification received");
        } else {
            ESP_LOGD(TAG, "Ignoring MCP notification method=%s", method);
        }
        return true;
    }

    if (strcmp(method, "initialize") == 0) {
        mcp_handle_initialize(id);
    } else if (strcmp(method, "tools/list") == 0) {
        mcp_handle_tools_list(id);
    } else if (strcmp(method, "tools/call") == 0) {
        cJSON *params = cJSON_GetObjectItem(payload, "params");
        if (!cJSON_IsObject(params)) {
            mcp_send_error(id, -32602, "Missing params");
        } else {
            mcp_handle_tool_call(id, params);
        }
    } else {
        mcp_send_error(id, -32601, "Unknown method");
    }
    return true;
}

static bool protocol_handle_envelope(cJSON *root, const char *source)
{
    cJSON *type_json = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type_json)) {
        return false;
    }

    const char *type = type_json->valuestring;

    if (strcmp(type, "hello") == 0 || strcmp(type, "hello_ack") == 0) {
        cJSON *session_json = cJSON_GetObjectItem(root, "session_id");
        if (cJSON_IsString(session_json)) {
            presence_set_session(session_json->valuestring);
        }
        cJSON *resident = cJSON_GetObjectItem(root, "resident_id");
        if (!cJSON_IsString(resident)) {
            cJSON *resident_obj = cJSON_GetObjectItem(root, "resident");
            resident = cJSON_GetObjectItem(resident_obj, "resident_id");
        }
        if (cJSON_IsString(resident)) {
            presence_set_resident(resident->valuestring);
        }
        bool ready = false;
        if (source != NULL && strcmp(source, "tcp") == 0 &&
            (strcmp(type, "hello_ack") == 0 || cJSON_IsString(session_json) || cJSON_IsString(resident))) {
            presence_set_connection(CONNECTION_OPENCLAW_READY);
            presence_snapshot_t snapshot;
            presence_get_snapshot(&snapshot);
            if (snapshot.presence == PRESENCE_CONNECTING || snapshot.presence == PRESENCE_BOOTING) {
                presence_set_state(PRESENCE_ONLINE_IDLE, "happy");
                apply_presence_visuals(PRESENCE_ONLINE_IDLE, "happy", false);
            }
            ready = true;
        }
        send_ok("hello", ready ? "openclaw_ready" : "hello_seen");
        return true;
    }

    if (strcmp(type, "memory_context") == 0) {
        cJSON *session_json = cJSON_GetObjectItem(root, "session_id");
        cJSON *resident = cJSON_GetObjectItem(root, "resident_id");
        cJSON *ttl = cJSON_GetObjectItem(root, "ttl_ms");
        if (cJSON_IsString(session_json)) {
            presence_set_session(session_json->valuestring);
        }
        if (cJSON_IsString(resident)) {
            presence_set_resident(resident->valuestring);
        }
        uint32_t ttl_ms = cJSON_IsNumber(ttl) && ttl->valueint > 0 ? (uint32_t)ttl->valueint : 3600000U;
        presence_set_memory_context(true, ttl_ms);
        send_ok("memory_context", "loaded");
        return true;
    }

    if (strcmp(type, "presence") == 0) {
        cJSON *session_json = cJSON_GetObjectItem(root, "session_id");
        cJSON *state_json = cJSON_GetObjectItem(root, "state");
        cJSON *emotion_json = cJSON_GetObjectItem(root, "emotion");
        cJSON *mouth_json = cJSON_GetObjectItem(root, "mouth");
        presence_state_t state;
        const char *emotion = cJSON_IsString(emotion_json) ? emotion_json->valuestring : NULL;
        bool mouth_open = cJSON_IsBool(mouth_json) ? cJSON_IsTrue(mouth_json) : false;
        if (!cJSON_IsString(state_json) || !presence_parse_state(state_json->valuestring, &state)) {
            send_error("presence", "unknown presence state");
        } else if (emotion != NULL && emotion_find(emotion) == NULL) {
            send_error("presence", "unknown emotion");
        } else {
            if (cJSON_IsString(session_json)) {
                presence_set_session(session_json->valuestring);
            }
            presence_set_state(state, emotion);
            apply_presence_visuals(state, emotion, mouth_open);
            send_ok("presence", presence_state_to_string(state));
        }
        return true;
    }

    return false;
}

void protocol_init(protocol_send_fn_t sender, void *sender_ctx)
{
    s_sender = sender;
    s_sender_ctx = sender_ctx;
    if (s_visual_queue == NULL) {
        s_visual_queue = xQueueCreate(1, sizeof(visual_update_t));
        if (s_visual_queue != NULL) {
            if (xTaskCreate(visual_task, "visual_task", 4096, NULL, 5, NULL) != pdPASS) {
                vQueueDelete(s_visual_queue);
                s_visual_queue = NULL;
                ESP_LOGW(TAG, "visual_task create failed; falling back to sync drawing");
            }
        }
    }
}

void protocol_handle_line(const char *line, const char *source)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }

    cJSON *root = cJSON_Parse(line);
    if (root == NULL) {
        ESP_LOGW(TAG, "bad JSON from %s, len=%u", source, (unsigned)strlen(line));
        send_error("parse", "invalid json");
        return;
    }

    if (protocol_handle_mcp(root)) {
        cJSON_Delete(root);
        return;
    }

    if (protocol_handle_envelope(root, source)) {
        cJSON_Delete(root);
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
            if (emotion == NULL || !emotion_draw(value_json->valuestring)) {
                send_error(action, "unknown emotion");
            } else {
                presence_set_state(PRESENCE_ONLINE_IDLE, emotion->name);
                send_ok(action, emotion->name);
            }
        }
    } else if (strcmp(action, "presence") == 0) {
        cJSON *state_json = cJSON_GetObjectItem(root, "state");
        cJSON *emotion_json = cJSON_GetObjectItem(root, "emotion");
        cJSON *mouth_json = cJSON_GetObjectItem(root, "mouth");
        presence_state_t state;
        const char *emotion = cJSON_IsString(emotion_json) ? emotion_json->valuestring : NULL;
        bool mouth_open = cJSON_IsBool(mouth_json) ? cJSON_IsTrue(mouth_json) : false;
        if (!cJSON_IsString(state_json) || !presence_parse_state(state_json->valuestring, &state)) {
            send_error(action, "unknown presence state");
        } else if (emotion != NULL && emotion_find(emotion) == NULL) {
            send_error(action, "unknown emotion");
        } else {
            presence_set_state(state, emotion);
            apply_presence_visuals(state, emotion, mouth_open);
            send_ok(action, presence_state_to_string(state));
        }
    } else if (strcmp(action, "sleep") == 0) {
        cJSON *enabled_json = cJSON_GetObjectItem(root, "enabled");
        bool enabled = enabled_json == NULL || cJSON_IsTrue(enabled_json);
        presence_state_t state = enabled ? PRESENCE_SLEEPING : PRESENCE_ONLINE_IDLE;
        presence_set_state(state, NULL);
        apply_presence_visuals(state, NULL, false);
        send_ok(action, presence_state_to_string(state));
    } else if (strcmp(action, "memory_cue") == 0) {
        cJSON *emotion_json = cJSON_GetObjectItem(root, "emotion");
        cJSON *ttl_json = cJSON_GetObjectItem(root, "ttl_ms");
        const char *emotion = cJSON_IsString(emotion_json) ? emotion_json->valuestring : "love";
        if (emotion_find(emotion) == NULL) {
            send_error(action, "unknown emotion");
        } else {
            uint32_t ttl_ms = cJSON_IsNumber(ttl_json) && ttl_json->valueint > 0 ? (uint32_t)ttl_json->valueint : 3000U;
            presence_set_memory_context(true, ttl_ms);
            presence_set_state(PRESENCE_SPEAKING, emotion);
            apply_presence_visuals(PRESENCE_SPEAKING, emotion, true);
            send_ok(action, emotion);
        }
    } else if (strcmp(action, "self_test") == 0) {
        esp_err_t err = protocol_start_self_test();
        if (err == ESP_OK) {
            send_ok(action, "started");
        } else if (err == ESP_ERR_INVALID_STATE) {
            send_error(action, "self test already running");
        } else {
            send_error(action, esp_err_to_name(err));
        }
    } else if (strcmp(action, "led") == 0) {
        int r = json_int_or_default(root, "r", 0);
        int g = json_int_or_default(root, "g", 0);
        int b = json_int_or_default(root, "b", 0);
        if (!json_rgb_args_valid(r, g, b)) {
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
    } else if (strcmp(action, "look") == 0) {
        int yaw = json_int_or_default(root, "yaw", 1000);
        int pitch = json_int_or_default(root, "pitch", 1000);
        int duration = json_int_or_default(root, "duration_ms", 350);
        if (yaw < -45 || yaw > 45 || pitch < 5 || pitch > 60 || duration < 50 || duration > 3000) {
            send_error(action, "yaw must be -45..45, pitch 5..60, duration_ms 50..3000");
        } else {
            esp_err_t err = body_look_at(yaw, pitch, (uint32_t)duration);
            if (err == ESP_OK) {
                send_ok(action, "queued");
            } else {
                send_error(action, motion_error_message(err));
            }
        }
    } else if (strcmp(action, "motion") == 0) {
        cJSON *gesture_json = cJSON_GetObjectItem(root, "gesture");
        if (!cJSON_IsString(gesture_json)) {
            send_error(action, "missing gesture");
        } else {
            esp_err_t err = body_motion_gesture(gesture_json->valuestring);
            if (err == ESP_OK) {
                send_ok(action, gesture_json->valuestring);
            } else {
                send_error(action, motion_error_message(err));
            }
        }
    } else if (strcmp(action, "beep") == 0) {
        int frequency = json_int_or_default(root, "frequency_hz", json_int_or_default(root, "freq", 880));
        int duration = json_int_or_default(root, "duration_ms", 120);
        int volume = json_int_or_default(root, "volume", 30);
        if (frequency < 80 || frequency > 4000 || duration < 20 || duration > 2000 || volume < 0 || volume > 100) {
            send_error(action, "frequency_hz 80..4000, duration_ms 20..2000, volume 0..100");
        } else {
            esp_err_t err = audio_beep((uint32_t)frequency, (uint32_t)duration, (uint8_t)volume);
            if (err == ESP_OK) {
                send_ok(action, "ok");
            } else {
                send_error(action, audio_error_message(err));
            }
        }
    } else if (strcmp(action, "audio_stream") == 0) {
        handle_audio_stream(root, action);
    } else if (strcmp(action, "interrupt") == 0) {
        presence_set_state(PRESENCE_ONLINE_IDLE, "normal");
        apply_presence_visuals(PRESENCE_ONLINE_IDLE, "normal", false);
        send_ok(action, "stopped");
    } else if (strcmp(action, "ping") == 0) {
        send_ok(action, "pong");
    } else {
        send_error(action, "unknown action");
    }

    cJSON_Delete(root);
}

void protocol_set_tactile_available(bool available)
{
    s_tactile_available = available;
}

void protocol_emit_hello(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddStringToObject(root, "name", "openclaw-stackchan-cores3");
    cJSON_AddStringToObject(root, "version", FIRMWARE_VERSION);
    cJSON_AddStringToObject(root, "protocol", "openclaw-stackchan");
    cJSON_AddNumberToObject(root, "protocol_version", 1);
    cJSON_AddStringToObject(root, "transport", "tcp-json");

    cJSON *features = cJSON_CreateObject();
    cJSON *emotions = cJSON_CreateArray();
    cJSON *device = cJSON_CreateObject();
    cJSON *resident = cJSON_CreateObject();
    if (features == NULL || emotions == NULL || device == NULL || resident == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(features);
        cJSON_Delete(emotions);
        cJSON_Delete(device);
        cJSON_Delete(resident);
        return;
    }
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddBoolToObject(features, "emotion", true);
    cJSON_AddBoolToObject(features, "led", true);
    cJSON_AddBoolToObject(features, "touch", s_tactile_available);
    cJSON_AddBoolToObject(features, "gesture", s_tactile_available);
    cJSON_AddBoolToObject(features, "pressure", s_tactile_available);
    cJSON_AddBoolToObject(features, "body_input", s_tactile_available);
    cJSON_AddBoolToObject(features, "tactile", s_tactile_available);
    cJSON_AddBoolToObject(features, "presence", true);
    cJSON_AddBoolToObject(features, "memory_context", true);
    cJSON_AddBoolToObject(features, "motion", body_motion_available());
    cJSON_AddBoolToObject(features, "servo", body_motion_available());
    cJSON_AddBoolToObject(features, "audio_in", false);
    cJSON_AddBoolToObject(features, "audio_out", audio_is_available());
    cJSON_AddBoolToObject(features, "audio_stream_out", audio_is_available());
    cJSON_AddBoolToObject(features, "wake_word", false);
    cJSON_AddBoolToObject(features, "echo_cancellation", false);
    cJSON_AddBoolToObject(features, "websocket", false);
    cJSON_AddBoolToObject(features, "mqtt", false);
    cJSON_AddBoolToObject(features, "ota", false);
    cJSON_AddBoolToObject(features, "multi_device", false);
    cJSON_AddItemToObject(root, "features", features);

    uint8_t mac[6] = {0};
    char device_id[24] = "cores3";
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(device_id, sizeof(device_id), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    cJSON_AddStringToObject(device, "device_id", device_id);
    cJSON_AddStringToObject(device, "client_id", device_id);
    cJSON_AddStringToObject(device, "name", "openclaw-stackchan-cores3");
    cJSON_AddStringToObject(device, "model", "m5stack-cores3");
    cJSON_AddStringToObject(device, "firmware", FIRMWARE_VERSION);
    cJSON_AddItemToObject(root, "device", device);

    presence_snapshot_t snapshot;
    presence_get_snapshot(&snapshot);
    cJSON_AddBoolToObject(resident, "supported", true);
    cJSON_AddBoolToObject(resident, "memory_context", true);
    if (snapshot.resident_id[0] != '\0') {
        cJSON_AddStringToObject(resident, "resident_id", snapshot.resident_id);
    }
    cJSON_AddItemToObject(root, "resident", resident);
    if (snapshot.session_id[0] != '\0') {
        cJSON_AddStringToObject(root, "session_id", snapshot.session_id);
    }
    presence_add_json(root);
    if (audio_is_available()) {
        cJSON *audio_params = cJSON_CreateObject();
        cJSON_AddNumberToObject(audio_params, "sample_rate", CORES3_AUDIO_SAMPLE_RATE);
        cJSON_AddStringToObject(audio_params, "format", "pcm_s16le");
        cJSON_AddNumberToObject(audio_params, "channels", 2);
        cJSON_AddItemToObject(root, "audio_params", audio_params);
    } else {
        cJSON_AddNullToObject(root, "audio_params");
    }

    for (size_t i = 0; i < g_emotion_count; ++i) {
        cJSON_AddItemToArray(emotions, cJSON_CreateString(g_emotions[i].name));
    }
    cJSON_AddItemToObject(root, "emotions", emotions);

    send_json(root);
    cJSON_Delete(root);
}

void protocol_emit_button(const char *pin, const char *action)
{
    const char *intent = button_intent(pin, action);
    apply_local_intent(intent);
    protocol_emit_body_input("button", action, pin, -1, -1, action != NULL && strcmp(action, "press") == 0 ? 100 : 0, intent);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "button");
    cJSON_AddStringToObject(root, "pin", pin);
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddStringToObject(root, "intent", intent);
    presence_add_json(root);
    send_json(root);
    cJSON_Delete(root);
}

void protocol_emit_body_input(const char *input, const char *action, const char *source,
                              int x, int y, int intensity, const char *intent)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "body_input");
    cJSON_AddStringToObject(root, "input", input != NULL ? input : "unknown");
    cJSON_AddStringToObject(root, "action", action != NULL ? action : "unknown");
    if (source != NULL && source[0] != '\0') {
        cJSON_AddStringToObject(root, "source", source);
    }
    if (x >= 0 && y >= 0) {
        cJSON_AddNumberToObject(root, "x", x);
        cJSON_AddNumberToObject(root, "y", y);
    }
    if (intensity >= 0) {
        cJSON_AddNumberToObject(root, "intensity", intensity);
    }
    if (intent != NULL && intent[0] != '\0') {
        cJSON_AddStringToObject(root, "intent", intent);
    }
    presence_add_json(root);
    send_json(root);
    cJSON_Delete(root);
}

void protocol_emit_touch(int x, int y)
{
    protocol_emit_body_input("touch", "contact", "touchscreen", x, y, 30, "attention");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "touch");
    cJSON_AddNumberToObject(root, "x", x);
    cJSON_AddNumberToObject(root, "y", y);
    cJSON_AddStringToObject(root, "intent", "attention");
    presence_add_json(root);
    send_json(root);
    cJSON_Delete(root);
}

void protocol_emit_pressure(const char *action, int x, int y, int intensity)
{
    if (action != NULL && strcmp(action, "press") == 0) {
        presence_set_state(PRESENCE_LISTENING, "happy");
        apply_presence_visuals(PRESENCE_LISTENING, "happy", false);
    } else if (action != NULL && strcmp(action, "hold") == 0) {
        presence_set_state(PRESENCE_SPEAKING, "love");
        apply_presence_visuals(PRESENCE_SPEAKING, "love", true);
    } else if (action != NULL && strcmp(action, "release") == 0) {
        presence_set_state(PRESENCE_ONLINE_IDLE, "happy");
        apply_presence_visuals(PRESENCE_ONLINE_IDLE, "happy", false);
    }
    protocol_emit_body_input("touch", action, "touchscreen", x, y, intensity, "tactile_contact");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "pressure");
    cJSON_AddStringToObject(root, "source", "touchscreen");
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddNumberToObject(root, "x", x);
    cJSON_AddNumberToObject(root, "y", y);
    cJSON_AddNumberToObject(root, "intensity", intensity);
    cJSON_AddStringToObject(root, "intent", "tactile_contact");
    presence_add_json(root);
    send_json(root);
    cJSON_Delete(root);
}

void protocol_emit_gesture(const char *gesture, int x, int y)
{
    const char *intent = gesture_intent(gesture);
    apply_local_intent(intent);
    body_apply_touch_gesture(gesture);
    protocol_emit_body_input("gesture", gesture, "touchscreen", x, y, 60, intent);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "gesture");
    cJSON_AddStringToObject(root, "gesture", gesture);
    cJSON_AddStringToObject(root, "intent", intent);
    if (x >= 0 && y >= 0) {
        cJSON_AddNumberToObject(root, "x", x);
        cJSON_AddNumberToObject(root, "y", y);
    }
    presence_add_json(root);
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
    cJSON_AddBoolToObject(root, "motion_available", body_motion_available());
    cJSON_AddBoolToObject(root, "audio_out_available", audio_is_available());
    presence_add_json(root);
    send_json(root);
    cJSON_Delete(root);
}
