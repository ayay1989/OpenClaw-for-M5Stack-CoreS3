#include "selftest.h"

#include <stdbool.h>
#include <stdlib.h>
#include "audiodriver.h"
#include "body_service.h"
#include "diagnostics.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "leddriver.h"
#include "presence.h"
#include "visuals.h"

static const char *TAG = "selftest";
static portMUX_TYPE s_self_test_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    selftest_send_json_fn_t send_json;
} selftest_task_args_t;

static bool s_self_test_running;

static void selftest_task(void *arg)
{
    selftest_task_args_t args = {0};
    if (arg != NULL) {
        args = *(selftest_task_args_t *)arg;
        free(arg);
    }

    ESP_LOGI(TAG, "body self-test started");
    led_write_result_t led_probe = {0};

    visuals_apply_now(PRESENCE_LISTENING, "surprised", false);
    led_probe = led_set_color(255, 0, 0);
    bool led_gpio_write_ok = led_probe.led_gpio_write_ok;
    bool py32_led_write_ok = led_probe.py32_led_write_ok;
    bool py32_led_available = led_probe.py32_led_available;
    vTaskDelay(pdMS_TO_TICKS(700));

    visuals_apply_now(PRESENCE_SPEAKING, "love", true);
    led_probe = led_set_color(0, 255, 0);
    led_gpio_write_ok = led_gpio_write_ok || led_probe.led_gpio_write_ok;
    py32_led_write_ok = py32_led_write_ok || led_probe.py32_led_write_ok;
    py32_led_available = led_probe.py32_led_available;
    vTaskDelay(pdMS_TO_TICKS(700));

    visuals_apply_now(PRESENCE_ONLINE_IDLE, "happy", false);
    led_probe = led_set_color(0, 0, 255);
    led_gpio_write_ok = led_gpio_write_ok || led_probe.led_gpio_write_ok;
    py32_led_write_ok = py32_led_write_ok || led_probe.py32_led_write_ok;
    py32_led_available = led_probe.py32_led_available;
    vTaskDelay(pdMS_TO_TICKS(700));

    esp_err_t motion_err = body_motion_available() ? body_motion_gesture("nod") : ESP_ERR_NOT_FOUND;
    esp_err_t audio_err = audio_is_available() ? audio_beep(880, 180, 45) : ESP_ERR_NOT_SUPPORTED;
    led_probe = led_set_breath(0, 100, 255, 3);
    led_gpio_write_ok = led_gpio_write_ok || led_probe.led_gpio_write_ok;
    py32_led_write_ok = py32_led_write_ok || led_probe.py32_led_write_ok;
    py32_led_available = led_probe.py32_led_available;
    presence_set_state(PRESENCE_ONLINE_IDLE, "happy");
    visuals_apply_now(PRESENCE_ONLINE_IDLE, "happy", false);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGW(TAG, "self-test result allocation failed");
        taskENTER_CRITICAL(&s_self_test_lock);
        s_self_test_running = false;
        taskEXIT_CRITICAL(&s_self_test_lock);
        vTaskDelete(NULL);
        return;
    }
    cJSON_AddStringToObject(root, "event", "self_test");
    cJSON_AddBoolToObject(root, "display", true);
    led_write_result_t led_result = {
        .led_gpio_write_ok = led_gpio_write_ok,
        .py32_led_write_ok = py32_led_write_ok,
        .py32_led_available = py32_led_available,
    };
    diagnostics_add_led_write_result(root, &led_result);
    diagnostics_add_hardware_status(root);
    cJSON_AddStringToObject(root, "motion_result", motion_err == ESP_OK ? "ok" : diagnostics_motion_error_message(motion_err));
    cJSON_AddStringToObject(root, "audio_result", audio_err == ESP_OK ? "ok" : diagnostics_audio_error_message(audio_err));
    presence_add_json(root);
    if (args.send_json != NULL) {
        args.send_json(root);
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "body self-test finished, motion=%s audio=%s",
             motion_err == ESP_OK ? "ok" : diagnostics_motion_error_message(motion_err),
             audio_err == ESP_OK ? "ok" : diagnostics_audio_error_message(audio_err));
    taskENTER_CRITICAL(&s_self_test_lock);
    s_self_test_running = false;
    taskEXIT_CRITICAL(&s_self_test_lock);
    vTaskDelete(NULL);
}

esp_err_t selftest_start(selftest_send_json_fn_t send_json)
{
    taskENTER_CRITICAL(&s_self_test_lock);
    if (s_self_test_running) {
        taskEXIT_CRITICAL(&s_self_test_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_self_test_running = true;
    taskEXIT_CRITICAL(&s_self_test_lock);

    selftest_task_args_t *args = calloc(1, sizeof(selftest_task_args_t));
    if (args == NULL) {
        taskENTER_CRITICAL(&s_self_test_lock);
        s_self_test_running = false;
        taskEXIT_CRITICAL(&s_self_test_lock);
        return ESP_ERR_NO_MEM;
    }
    args->send_json = send_json;
    if (xTaskCreate(selftest_task, "self_test", 4096, args, 4, NULL) != pdPASS) {
        taskENTER_CRITICAL(&s_self_test_lock);
        s_self_test_running = false;
        taskEXIT_CRITICAL(&s_self_test_lock);
        free(args);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
