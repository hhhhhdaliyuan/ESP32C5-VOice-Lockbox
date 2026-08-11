#include "chest_controller.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kws_wakeup.h"
#include "sdkconfig.h"

#if CONFIG_CHEST_ENABLE_DISPLAY
#include "display_status.h"
#endif

#if CONFIG_CHEST_ENABLE_SERVO
#include "board_config.h"
#include "servo.h"
#endif

static const char *TAG = "chest_controller";

#define CHEST_CONTROLLER_TASK_STACK_SIZE 4096
#define CHEST_CONTROLLER_TASK_PRIORITY 4
#define CHEST_CONTROLLER_EVENT_WAIT_MS 1000

#if CONFIG_CHEST_ENABLE_SERVO
#define CHEST_SERVO_MIN_PULSE_US 500
#define CHEST_SERVO_MAX_PULSE_US 2500
#define CHEST_SERVO_OPEN_STEP_DELAY_MS 20
#endif

static TaskHandle_t s_controller_task;

static void chest_controller_open_lid(const kws_wakeup_event_t *event)
{
#if CONFIG_CHEST_ENABLE_DISPLAY
    display_status_show_opening();
#endif

#if CONFIG_CHEST_ENABLE_SERVO
    ESP_LOGI(TAG, "opening lid for verified keyword: %s", event->keyword);
    servo_open(CHEST_SERVO_OPEN_STEP_DELAY_MS);
#else
    ESP_LOGI(TAG, "verified keyword: %s (actuator disabled)", event->keyword);
#endif

#if CONFIG_CHEST_ENABLE_DISPLAY
    display_status_show_opened();
#endif
}

static void chest_controller_handle_panbao(
    const kws_wakeup_event_t *event)
{
    chest_controller_open_lid(event);
}

static void chest_controller_handle_ying_laoshi(
    const kws_wakeup_event_t *event)
{
    chest_controller_open_lid(event);
}

static void chest_controller_handle_wakeup(const kws_wakeup_event_t *event)
{
    ESP_LOGI(TAG,
             "dual-auth wakeup: %s confidence=%.3f speaker=%s score=%.3f threshold=%.3f",
             event->keyword, event->confidence, event->speaker_id,
             event->voiceprint_score, event->voiceprint_threshold);

    if (event->voiceprint_score < event->voiceprint_threshold) {
        ESP_LOGW(TAG, "ignoring wakeup event without voiceprint approval");
        return;
    }

    switch (event->type) {
    case KWS_WAKEUP_KEYWORD_PANBAO:
        chest_controller_handle_panbao(event);
        break;
    case KWS_WAKEUP_KEYWORD_YING_LAOSHI:
        chest_controller_handle_ying_laoshi(event);
        break;
    case KWS_WAKEUP_KEYWORD_UNKNOWN:
    default:
        ESP_LOGW(TAG, "no action registered for keyword: %s", event->keyword);
        break;
    }
}

static void chest_controller_task(void *arg)
{
    (void)arg;

    while (true) {
        kws_wakeup_event_t event;
        esp_err_t ret =
            kws_wakeup_get_event(&event, CHEST_CONTROLLER_EVENT_WAIT_MS);
        if (ret == ESP_OK) {
            chest_controller_handle_wakeup(&event);
        } else if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "wakeup event wait failed: %s",
                     esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

esp_err_t chest_controller_start(void)
{
    if (s_controller_task) {
        return ESP_OK;
    }

    esp_err_t ret;

#if CONFIG_CHEST_ENABLE_DISPLAY
    ret = display_status_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "status display unavailable: %s", esp_err_to_name(ret));
    }
#endif

#if CONFIG_CHEST_ENABLE_SERVO
    ret = servo_init(BOARD_SG90_PWM_GPIO,
                     CHEST_SERVO_MIN_PULSE_US,
                     CHEST_SERVO_MAX_PULSE_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "lid servo init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "lid servo ready on GPIO%d", BOARD_SG90_PWM_GPIO);
#else
    ESP_LOGI(TAG, "phase 1 voice-only mode: lid actuator disabled");
#endif

    ret = kws_wakeup_start();
    if (ret != ESP_OK) {
#if CONFIG_CHEST_ENABLE_DISPLAY
        display_status_show_error("KWS ERROR");
#endif
        ESP_LOGE(TAG, "KWS wakeup start failed: %s", esp_err_to_name(ret));
        return ret;
    }

#if CONFIG_CHEST_ENABLE_DISPLAY
    display_status_show_listening();
#endif

    if (xTaskCreate(chest_controller_task, "chest_ctrl",
                    CHEST_CONTROLLER_TASK_STACK_SIZE, NULL,
                    CHEST_CONTROLLER_TASK_PRIORITY,
                    &s_controller_task) != pdPASS) {
        s_controller_task = NULL;
        ESP_LOGE(TAG, "controller task creation failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "business controller started");
    return ESP_OK;
}
