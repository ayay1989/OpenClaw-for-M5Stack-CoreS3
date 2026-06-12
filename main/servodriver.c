#include "servodriver.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "py32driver.h"
#include "scservo_bus.h"

static const char *TAG = "servo";

#define SERVO_YAW_ID 1
#define SERVO_PITCH_ID 2
#define SERVO_DEFAULT_MOVE_MS 350
#define SERVO_GESTURE_STEP_MS 260
#define SERVO_GESTURE_PAUSE_MS 80
#define SERVO_DEFAULT_SPEED 0

static bool s_initialized;
static bool s_available;
static int s_yaw_deg = SERVO_YAW_CENTER_DEG;
static int s_pitch_deg = SERVO_PITCH_CENTER_DEG;

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static uint16_t yaw_to_position(int yaw_deg)
{
    return (uint16_t)(460 + yaw_deg * 16 / 5);
}

static uint16_t pitch_to_position(int pitch_deg)
{
    return (uint16_t)(620 + pitch_deg * 16 / 5);
}

static esp_err_t move_checked(int yaw_deg, int pitch_deg, uint32_t duration_ms)
{
    if (!s_initialized || !s_available) {
        return ESP_ERR_NOT_FOUND;
    }

    yaw_deg = clamp_int(yaw_deg, SERVO_YAW_MIN_DEG, SERVO_YAW_MAX_DEG);
    pitch_deg = clamp_int(pitch_deg, SERVO_PITCH_MIN_DEG, SERVO_PITCH_MAX_DEG);
    uint16_t move_ms = duration_ms > UINT16_MAX ? UINT16_MAX : (uint16_t)duration_ms;
    esp_err_t yaw_err = scservo_bus_write_position(SERVO_YAW_ID, yaw_to_position(yaw_deg),
                                                   move_ms, SERVO_DEFAULT_SPEED);
    esp_err_t pitch_err = scservo_bus_write_position(SERVO_PITCH_ID, pitch_to_position(pitch_deg),
                                                     move_ms, SERVO_DEFAULT_SPEED);

    if (yaw_err == ESP_OK && pitch_err == ESP_OK) {
        s_yaw_deg = yaw_deg;
        s_pitch_deg = pitch_deg;
        return ESP_OK;
    }

    if (!scservo_bus_is_available()) {
        s_available = false;
        ESP_LOGW(TAG, "SCServo bus disabled after repeated failures");
    }
    return yaw_err != ESP_OK ? yaw_err : pitch_err;
}

static void gesture_pause(void)
{
    vTaskDelay(pdMS_TO_TICKS(SERVO_GESTURE_PAUSE_MS));
}

esp_err_t servo_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = scservo_bus_init();
    s_initialized = err == ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "servo bus init failed, continuing without servos: %s", esp_err_to_name(err));
        return err;
    }

    if (!py32_is_available()) {
        ESP_LOGW(TAG, "PY32 not available; servo VM_EN power is disabled");
        s_available = false;
        return ESP_OK;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(py32_set_servo_power(true));
    vTaskDelay(pdMS_TO_TICKS(80));

    esp_err_t yaw = scservo_bus_ping(SERVO_YAW_ID);
    esp_err_t pitch = scservo_bus_ping(SERVO_PITCH_ID);
    s_available = (yaw == ESP_OK && pitch == ESP_OK);
    if (!s_available) {
        ESP_LOGW(TAG, "servos not detected, yaw=%s pitch=%s",
                 esp_err_to_name(yaw), esp_err_to_name(pitch));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "servos detected yaw_id=%d pitch_id=%d", SERVO_YAW_ID, SERVO_PITCH_ID);
    ESP_ERROR_CHECK_WITHOUT_ABORT(servo_enable(true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(servo_center());
    return ESP_OK;
}

bool servo_is_available(void)
{
    return s_initialized && s_available && scservo_bus_is_available();
}

esp_err_t servo_enable(bool enable)
{
    if (!s_initialized || !s_available) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t yaw = scservo_bus_enable_torque(SERVO_YAW_ID, enable);
    esp_err_t pitch = scservo_bus_enable_torque(SERVO_PITCH_ID, enable);
    if (yaw == ESP_OK && pitch == ESP_OK) {
        return ESP_OK;
    }

    if (!scservo_bus_is_available()) {
        s_available = false;
    }
    return yaw != ESP_OK ? yaw : pitch;
}

esp_err_t servo_center(void)
{
    return servo_move_to(SERVO_YAW_CENTER_DEG, SERVO_PITCH_CENTER_DEG, SERVO_DEFAULT_MOVE_MS);
}

esp_err_t servo_move_to(int yaw_deg, int pitch_deg, uint32_t duration_ms)
{
    return move_checked(yaw_deg, pitch_deg, duration_ms == 0 ? SERVO_DEFAULT_MOVE_MS : duration_ms);
}

esp_err_t servo_nod(void)
{
    int yaw = s_yaw_deg;
    esp_err_t err = servo_move_to(yaw, 45, SERVO_GESTURE_STEP_MS);
    if (err != ESP_OK) {
        return err;
    }
    gesture_pause();
    err = servo_move_to(yaw, 15, SERVO_GESTURE_STEP_MS);
    if (err != ESP_OK) {
        return err;
    }
    gesture_pause();
    return servo_move_to(yaw, SERVO_PITCH_CENTER_DEG, SERVO_GESTURE_STEP_MS);
}

esp_err_t servo_shake(void)
{
    int pitch = s_pitch_deg;
    esp_err_t err = servo_move_to(-25, pitch, SERVO_GESTURE_STEP_MS);
    if (err != ESP_OK) {
        return err;
    }
    gesture_pause();
    err = servo_move_to(25, pitch, SERVO_GESTURE_STEP_MS);
    if (err != ESP_OK) {
        return err;
    }
    gesture_pause();
    return servo_move_to(SERVO_YAW_CENTER_DEG, pitch, SERVO_GESTURE_STEP_MS);
}

esp_err_t servo_tilt(void)
{
    esp_err_t err = servo_move_to(-20, 42, SERVO_GESTURE_STEP_MS);
    if (err != ESP_OK) {
        return err;
    }
    gesture_pause();
    return servo_center();
}
