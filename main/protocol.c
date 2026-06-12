#include "protocol.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "emotions.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lcddriver.h"
#include "leddriver.h"
#include "presence.h"

static const char *TAG = "protocol";
static const char *FIRMWARE_VERSION = "0.4.0";
static protocol_send_fn_t s_sender;
static void *s_sender_ctx;

typedef struct {
    presence_state_t state;
    char emotion[16];
    bool mouth_open;
} visual_update_t;

static QueueHandle_t s_visual_queue;

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
        char status[192];
        snprintf(status, sizeof(status),
                 "{\"uptime\":%lld,\"wifi_rssi\":%d,\"presence_state\":\"%s\",\"connection_state\":\"%s\",\"emotion\":\"%s\"}",
                 esp_timer_get_time() / 1000000LL, rssi,
                 presence_state_to_string(snapshot.presence),
                 presence_connection_to_string(snapshot.connection),
                 snapshot.emotion);
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
    } else if (strcmp(action, "ping") == 0) {
        send_ok(action, "pong");
    } else {
        send_error(action, "unknown action");
    }

    cJSON_Delete(root);
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
    cJSON_AddBoolToObject(features, "touch", true);
    cJSON_AddBoolToObject(features, "gesture", true);
    cJSON_AddBoolToObject(features, "presence", true);
    cJSON_AddBoolToObject(features, "memory_context", true);
    cJSON_AddBoolToObject(features, "motion", false);
    cJSON_AddBoolToObject(features, "audio_in", false);
    cJSON_AddBoolToObject(features, "audio_out", false);
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
    cJSON_AddNullToObject(root, "audio_params");

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
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "button");
    cJSON_AddStringToObject(root, "pin", pin);
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddStringToObject(root, "intent", intent);
    presence_add_json(root);
    send_json(root);
    cJSON_Delete(root);
}

void protocol_emit_touch(int x, int y)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "touch");
    cJSON_AddNumberToObject(root, "x", x);
    cJSON_AddNumberToObject(root, "y", y);
    cJSON_AddStringToObject(root, "intent", "attention");
    presence_add_json(root);
    send_json(root);
    cJSON_Delete(root);
}

void protocol_emit_gesture(const char *gesture, int x, int y)
{
    const char *intent = gesture_intent(gesture);
    apply_local_intent(intent);
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
    presence_add_json(root);
    send_json(root);
    cJSON_Delete(root);
}
