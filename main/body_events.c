#include "body_events.h"

#include <stdbool.h>
#include <string.h>
#include "body_service.h"
#include "presence.h"
#include "visuals.h"

static body_events_send_json_fn_t s_send_json;

void body_events_init(body_events_send_json_fn_t send_json)
{
    s_send_json = send_json;
}

static void send_body_event(cJSON *root)
{
    if (root != NULL && s_send_json != NULL) {
        s_send_json(root);
    }
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

static bool local_feedback_enabled(void)
{
    presence_snapshot_t snapshot;
    presence_get_snapshot(&snapshot);
    return snapshot.connection < CONNECTION_TCP_CONNECTED;
}

static void apply_local_intent(const char *intent)
{
    if (!local_feedback_enabled()) {
        return;
    }
    if (intent == NULL) {
        return;
    }
    if (strcmp(intent, "summon") == 0 || strcmp(intent, "wake") == 0) {
        presence_set_state(PRESENCE_LISTENING, NULL);
        visuals_apply(PRESENCE_LISTENING, NULL, false);
    } else if (strcmp(intent, "interrupt") == 0) {
        presence_set_state(PRESENCE_ONLINE_IDLE, "normal");
        visuals_apply(PRESENCE_ONLINE_IDLE, "normal", false);
    } else if (strcmp(intent, "sleep_toggle") == 0) {
        presence_snapshot_t snapshot;
        presence_get_snapshot(&snapshot);
        presence_state_t next = snapshot.presence == PRESENCE_SLEEPING ? PRESENCE_ONLINE_IDLE : PRESENCE_SLEEPING;
        presence_set_state(next, NULL);
        visuals_apply(next, NULL, false);
    } else if (strcmp(intent, "touch") == 0) {
        presence_set_state(PRESENCE_ONLINE_IDLE, "happy");
        visuals_apply(PRESENCE_ONLINE_IDLE, "happy", false);
    } else if (strcmp(intent, "comfort") == 0) {
        presence_set_state(PRESENCE_SPEAKING, "love");
        visuals_apply(PRESENCE_SPEAKING, "love", true);
        body_motion_gesture("tilt");
    }
}

void protocol_emit_body_input(const char *input, const char *action, const char *source,
                              int x, int y, int intensity, const char *intent)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
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
    send_body_event(root);
    cJSON_Delete(root);
}

void protocol_emit_button(const char *pin, const char *action)
{
    const char *intent = button_intent(pin, action);
    apply_local_intent(intent);
    protocol_emit_body_input("button", action, pin, -1, -1, action != NULL && strcmp(action, "press") == 0 ? 100 : 0, intent);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "event", "button");
    cJSON_AddStringToObject(root, "pin", pin);
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddStringToObject(root, "intent", intent);
    presence_add_json(root);
    send_body_event(root);
    cJSON_Delete(root);
}

void protocol_emit_touch(int x, int y)
{
    protocol_emit_body_input("touch", "contact", "touchscreen", x, y, 30, "attention");
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "event", "touch");
    cJSON_AddNumberToObject(root, "x", x);
    cJSON_AddNumberToObject(root, "y", y);
    cJSON_AddStringToObject(root, "intent", "attention");
    presence_add_json(root);
    send_body_event(root);
    cJSON_Delete(root);
}

static void apply_pressure_feedback(const char *action)
{
    if (!local_feedback_enabled()) {
        return;
    }
    if (action != NULL && strcmp(action, "press") == 0) {
        presence_set_state(PRESENCE_LISTENING, "happy");
        visuals_apply(PRESENCE_LISTENING, "happy", false);
    } else if (action != NULL && (strcmp(action, "hold") == 0 || strcmp(action, "pet") == 0)) {
        presence_set_state(PRESENCE_SPEAKING, "love");
        visuals_apply(PRESENCE_SPEAKING, "love", true);
        if (strcmp(action, "pet") == 0) {
            body_motion_gesture("tilt");
        }
    } else if (action != NULL && strcmp(action, "release") == 0) {
        presence_set_state(PRESENCE_ONLINE_IDLE, "happy");
        visuals_apply(PRESENCE_ONLINE_IDLE, "happy", false);
    }
}

void protocol_emit_pressure(const char *action, int x, int y, int intensity)
{
    protocol_emit_pressure_source(action, "touchscreen", x, y, intensity);
}

void protocol_emit_pressure_source(const char *action, const char *source, int x, int y, int intensity)
{
    apply_pressure_feedback(action);
    const char *event_source = source != NULL && source[0] != '\0' ? source : "unknown";
    const char *event_action = action != NULL && action[0] != '\0' ? action : "unknown";
    const char *intent = action != NULL && strcmp(action, "pet") == 0 ? "comfort" : "tactile_contact";
    protocol_emit_body_input("touch", event_action, event_source, x, y, intensity, intent);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "event", "pressure");
    cJSON_AddStringToObject(root, "source", event_source);
    cJSON_AddStringToObject(root, "action", event_action);
    if (x >= 0 && y >= 0) {
        cJSON_AddNumberToObject(root, "x", x);
        cJSON_AddNumberToObject(root, "y", y);
    }
    cJSON_AddNumberToObject(root, "intensity", intensity);
    cJSON_AddStringToObject(root, "intent", intent);
    presence_add_json(root);
    send_body_event(root);
    cJSON_Delete(root);
}

void protocol_emit_gesture(const char *gesture, int x, int y)
{
    const char *intent = gesture_intent(gesture);
    apply_local_intent(intent);
    body_apply_touch_gesture(gesture);
    protocol_emit_body_input("gesture", gesture, "touchscreen", x, y, 60, intent);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "event", "gesture");
    cJSON_AddStringToObject(root, "gesture", gesture);
    cJSON_AddStringToObject(root, "intent", intent);
    if (x >= 0 && y >= 0) {
        cJSON_AddNumberToObject(root, "x", x);
        cJSON_AddNumberToObject(root, "y", y);
    }
    presence_add_json(root);
    send_body_event(root);
    cJSON_Delete(root);
}
