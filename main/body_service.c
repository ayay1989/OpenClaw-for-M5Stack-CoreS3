#include "body_service.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "servodriver.h"

static const char *TAG = "body";

typedef enum {
    BODY_CMD_LOOK = 0,
    BODY_CMD_GESTURE,
} body_cmd_type_t;

typedef struct {
    body_cmd_type_t type;
    int yaw;
    int pitch;
    uint32_t duration_ms;
    char gesture[16];
} body_cmd_t;

static QueueHandle_t s_queue;
static bool s_initialized;

static esp_err_t enqueue_command(const body_cmd_t *cmd, bool best_effort)
{
    if (!s_initialized || s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!servo_is_available()) {
        return ESP_ERR_NOT_FOUND;
    }
    if (best_effort && uxQueueMessagesWaiting(s_queue) > 0) {
        return ESP_OK;
    }
    if (xQueueSend(s_queue, cmd, 0) != pdTRUE) {
        if (best_effort) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "motion queue busy");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void body_task(void *arg)
{
    (void)arg;
    body_cmd_t cmd;
    while (true) {
        if (xQueueReceive(s_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp_err_t err = ESP_OK;
        if (cmd.type == BODY_CMD_LOOK) {
            err = servo_move_to(cmd.yaw, cmd.pitch, cmd.duration_ms);
        } else if (cmd.type == BODY_CMD_GESTURE) {
            if (strcmp(cmd.gesture, "center") == 0) {
                err = servo_center();
            } else if (strcmp(cmd.gesture, "nod") == 0) {
                err = servo_nod();
            } else if (strcmp(cmd.gesture, "shake") == 0) {
                err = servo_shake();
            } else if (strcmp(cmd.gesture, "tilt") == 0) {
                err = servo_tilt();
            } else {
                err = ESP_ERR_INVALID_ARG;
            }
        }

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "motion command failed: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t body_service_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    s_queue = xQueueCreate(6, sizeof(body_cmd_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(body_task, "body_task", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "body service initialized; motion %s", servo_is_available() ? "available" : "unavailable");
    return ESP_OK;
}

bool body_motion_available(void)
{
    return servo_is_available();
}

esp_err_t body_look_at(int yaw_deg, int pitch_deg, uint32_t duration_ms)
{
    body_cmd_t cmd = {
        .type = BODY_CMD_LOOK,
        .yaw = yaw_deg,
        .pitch = pitch_deg,
        .duration_ms = duration_ms,
    };
    return enqueue_command(&cmd, false);
}

esp_err_t body_motion_gesture(const char *gesture)
{
    if (gesture == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(gesture, "center") != 0 && strcmp(gesture, "nod") != 0 &&
        strcmp(gesture, "shake") != 0 && strcmp(gesture, "tilt") != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    body_cmd_t cmd = {
        .type = BODY_CMD_GESTURE,
    };
    strncpy(cmd.gesture, gesture, sizeof(cmd.gesture) - 1);
    return enqueue_command(&cmd, false);
}

void body_apply_presence(presence_state_t state)
{
    if (!body_motion_available()) {
        return;
    }
    switch (state) {
    case PRESENCE_LISTENING: {
        body_cmd_t cmd = {.type = BODY_CMD_GESTURE};
        strncpy(cmd.gesture, "center", sizeof(cmd.gesture) - 1);
        (void)enqueue_command(&cmd, true);
        break;
    }
    case PRESENCE_SPEAKING: {
        body_cmd_t nod = {.type = BODY_CMD_GESTURE};
        strncpy(nod.gesture, "nod", sizeof(nod.gesture) - 1);
        (void)enqueue_command(&nod, true);
        break;
    }
    case PRESENCE_SLEEPING: {
        body_cmd_t sleep = {.type = BODY_CMD_LOOK, .yaw = 0, .pitch = 55, .duration_ms = 450};
        (void)enqueue_command(&sleep, true);
        break;
    }
    case PRESENCE_ERROR:
        break;
    default: {
        body_cmd_t center = {.type = BODY_CMD_GESTURE};
        strncpy(center.gesture, "center", sizeof(center.gesture) - 1);
        (void)enqueue_command(&center, true);
        break;
    }
    }
}

void body_apply_touch_gesture(const char *gesture)
{
    if (gesture == NULL || !body_motion_available()) {
        return;
    }
    if (strcmp(gesture, "swipe_left") == 0) {
        body_cmd_t cmd = {.type = BODY_CMD_LOOK, .yaw = -20, .pitch = SERVO_PITCH_CENTER_DEG, .duration_ms = 250};
        (void)enqueue_command(&cmd, true);
    } else if (strcmp(gesture, "swipe_right") == 0) {
        body_cmd_t cmd = {.type = BODY_CMD_LOOK, .yaw = 20, .pitch = SERVO_PITCH_CENTER_DEG, .duration_ms = 250};
        (void)enqueue_command(&cmd, true);
    } else if (strcmp(gesture, "double_tap") == 0) {
        body_cmd_t cmd = {.type = BODY_CMD_GESTURE};
        strncpy(cmd.gesture, "nod", sizeof(cmd.gesture) - 1);
        (void)enqueue_command(&cmd, true);
    } else if (strcmp(gesture, "long_press") == 0) {
        body_cmd_t cmd = {.type = BODY_CMD_LOOK, .yaw = 0, .pitch = 55, .duration_ms = 450};
        (void)enqueue_command(&cmd, true);
    }
}
