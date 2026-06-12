#include "protocol.h"

#include <stdbool.h>
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

static bool json_rgb_args_valid(int r, int g, int b)
{
    return r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255;
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

static void mcp_handle_initialize(cJSON *id)
{
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "protocolVersion", "2024-11-05");
    cJSON *capabilities = cJSON_CreateObject();
    cJSON_AddItemToObject(capabilities, "tools", cJSON_CreateObject());
    cJSON_AddItemToObject(result, "capabilities", capabilities);
    cJSON *server_info = cJSON_CreateObject();
    cJSON_AddStringToObject(server_info, "name", "openclaw-stackchan-cores3");
    cJSON_AddStringToObject(server_info, "version", "0.2.0");
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
        char status[96];
        snprintf(status, sizeof(status), "{\"uptime\":%lld,\"wifi_rssi\":%d}",
                 esp_timer_get_time() / 1000000LL, rssi);
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
            mcp_send_result(id, mcp_tool_text_result(value_json->valuestring));
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

    cJSON *id = cJSON_GetObjectItem(payload, "id");
    const char *method = method_json->valuestring;
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

    if (protocol_handle_mcp(root)) {
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
                send_ok(action, emotion->name);
            }
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

void protocol_emit_gesture(const char *gesture, int x, int y)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "gesture");
    cJSON_AddStringToObject(root, "gesture", gesture);
    if (x >= 0 && y >= 0) {
        cJSON_AddNumberToObject(root, "x", x);
        cJSON_AddNumberToObject(root, "y", y);
    }
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
