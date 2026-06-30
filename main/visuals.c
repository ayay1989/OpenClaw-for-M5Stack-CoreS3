#include "visuals.h"

#include <string.h>
#include "body_service.h"
#include "emotions.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "leddriver.h"

static const char *TAG = "visuals";

typedef struct {
    presence_state_t state;
    char emotion[16];
    bool mouth_open;
} visual_update_t;

static QueueHandle_t s_visual_queue;

static void copy_emotion(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0) {
        return;
    }
    if (src == NULL) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

void visuals_apply_now(presence_state_t state, const char *emotion, bool mouth_open)
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
            visuals_apply_now(update.state, emotion, update.mouth_open);
        }
    }
}

esp_err_t visuals_init(void)
{
    if (s_visual_queue != NULL) {
        return ESP_OK;
    }
    s_visual_queue = xQueueCreate(1, sizeof(visual_update_t));
    if (s_visual_queue == NULL) {
        ESP_LOGW(TAG, "visual queue create failed; falling back to sync drawing");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(visual_task, "visual_task", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_visual_queue);
        s_visual_queue = NULL;
        ESP_LOGW(TAG, "visual_task create failed; falling back to sync drawing");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void visuals_apply(presence_state_t state, const char *emotion, bool mouth_open)
{
    if (s_visual_queue == NULL) {
        visuals_apply_now(state, emotion, mouth_open);
        return;
    }
    visual_update_t update = {
        .state = state,
        .mouth_open = mouth_open,
    };
    copy_emotion(update.emotion, sizeof(update.emotion), emotion);
    xQueueOverwrite(s_visual_queue, &update);
}
