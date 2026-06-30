#include "diagnostics.h"

#include "audiodriver.h"
#include "body_service.h"
#include "py32driver.h"
#include "servodriver.h"
#include "si12tdriver.h"

void diagnostics_add_led_write_result(cJSON *root, const led_write_result_t *result)
{
    if (root == NULL || result == NULL) {
        return;
    }
    cJSON_AddBoolToObject(root, "led", result->led_gpio_write_ok || result->py32_led_write_ok);
    cJSON_AddBoolToObject(root, "led_gpio_write_ok", result->led_gpio_write_ok);
    cJSON_AddBoolToObject(root, "py32_led_write_ok", result->py32_led_write_ok);
    cJSON_AddBoolToObject(root, "py32_led_available", result->py32_led_available);
}

void diagnostics_add_hardware_status(cJSON *root)
{
    if (root == NULL) {
        return;
    }
    cJSON_AddBoolToObject(root, "py32_available", py32_is_available());
    cJSON_AddBoolToObject(root, "led_available", led_is_available());
    cJSON_AddBoolToObject(root, "led_gpio_available", led_gpio_is_available());
    cJSON_AddBoolToObject(root, "py32_led_available", py32_led_is_available());
    cJSON_AddBoolToObject(root, "servo_vm_en_ok", servo_vm_powered() || py32_servo_power_is_enabled());
    cJSON_AddBoolToObject(root, "servo_ping_ok", servo_ping_ok());
    cJSON_AddBoolToObject(root, "servo_write_ok", servo_position_write_ok());
    cJSON_AddBoolToObject(root, "si12t_available", si12t_is_available());
    cJSON_AddBoolToObject(root, "body_touch_available", si12t_is_available());
    cJSON_AddBoolToObject(root, "motion_available", body_motion_available());
    cJSON_AddBoolToObject(root, "audio_out_available", audio_is_available());
}

const char *diagnostics_motion_error_message(esp_err_t err)
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

const char *diagnostics_audio_error_message(esp_err_t err)
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
