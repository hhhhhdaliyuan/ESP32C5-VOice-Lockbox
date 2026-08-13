#include "chest_controller.h"

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "kws_wakeup.h"
#include "sdkconfig.h"
#include "voiceprint_auth.h"
#include "voiceprint_delete_ui.h"

#if CONFIG_CHEST_ENABLE_DISPLAY
#include "display_status.h"
#endif

#if CONFIG_CHEST_ENABLE_SERVO
#include "servo.h"
#endif

static const char *TAG = "chest_controller";

#define CHEST_CONTROLLER_TASK_STACK_SIZE 4096
#define CHEST_CONTROLLER_TASK_PRIORITY 4
#define CHEST_CONTROLLER_EVENT_WAIT_MS 20
#define CHEST_BUTTON_DEBOUNCE_MS 30
#define CHEST_DELETE_BUTTON_LONG_PRESS_MS 800
#define CHEST_DELETE_RESULT_SHOW_MS 1200
#define CHEST_DELETE_WORKER_TASK_STACK_SIZE 6144
#define CHEST_DELETE_WORKER_TASK_PRIORITY 3
#define CHEST_DELETE_REQUEST_QUEUE_SIZE 2
#define CHEST_DELETE_RESULT_QUEUE_SIZE 1

#if CONFIG_CHEST_ENABLE_SERVO
#define CHEST_SERVO_MIN_PULSE_US 500
#define CHEST_SERVO_MAX_PULSE_US 2500
#define CHEST_SERVO_OPEN_STEP_DELAY_MS 20
#define CHEST_SERVO_CLOSE_STEP_DELAY_MS 20
#endif

typedef enum {
    CHEST_DELETE_UI_NORMAL = 0,
    CHEST_DELETE_UI_LOADING_LIST,
    CHEST_DELETE_UI_LIST,
    CHEST_DELETE_UI_CONFIRM,
    CHEST_DELETE_UI_DELETING,
    CHEST_DELETE_UI_RESULT,
} chest_delete_ui_state_t;

typedef enum {
    CHEST_DELETE_REQUEST_LIST = 0,
    CHEST_DELETE_REQUEST_DELETE,
} chest_delete_request_type_t;

typedef struct {
    chest_delete_request_type_t type;
    voiceprint_speaker_t speaker;
} chest_delete_request_t;

typedef struct {
    chest_delete_request_type_t type;
    esp_err_t result;
    voiceprint_speaker_t speakers[VOICEPRINT_AUTH_MAX_SPEAKERS];
    size_t count;
} chest_delete_result_t;

typedef struct {
    bool stable_pressed;
    bool candidate_pressed;
    TickType_t candidate_tick;
    TickType_t pressed_tick;
} chest_button_state_t;

static TaskHandle_t s_controller_task;
static TaskHandle_t s_delete_worker_task;
static QueueHandle_t s_delete_request_queue;
static QueueHandle_t s_delete_result_queue;
static chest_delete_ui_state_t s_delete_ui_state = CHEST_DELETE_UI_NORMAL;
static voiceprint_speaker_t s_delete_speakers[VOICEPRINT_AUTH_MAX_SPEAKERS];
static size_t s_delete_speaker_count;
static size_t s_delete_selected;
static bool s_delete_confirm_selected = true;
static bool s_delete_result_refresh_list;
static TickType_t s_delete_result_until;

static bool chest_controller_button_pressed(gpio_num_t pin)
{
    return gpio_get_level(pin) == BOARD_BUTTON_ACTIVE_LEVEL;
}

static esp_err_t chest_controller_init_button(gpio_num_t pin, const char *name)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "%s ready on GPIO%d", name, pin);
    }
    return ret;
}

static void chest_controller_restore_status(void)
{
#if CONFIG_CHEST_ENABLE_DISPLAY
#if CONFIG_CHEST_ENABLE_SERVO
    if (servo_get_state() == SERVO_STATE_OPENED) {
        display_status_show_opened();
        return;
    }
#endif
    display_status_show_listening();
#endif
}

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

#if CONFIG_CHEST_ENABLE_SERVO
static void chest_controller_close_lid(void)
{
    if (servo_get_state() != SERVO_STATE_OPENED) {
        ESP_LOGI(TAG, "close button ignored because lid is already closed");
        return;
    }

#if CONFIG_CHEST_ENABLE_DISPLAY
    display_status_show_closing();
#endif

    ESP_LOGI(TAG, "closing lid from local button");
    servo_close(CHEST_SERVO_CLOSE_STEP_DELAY_MS);

#if CONFIG_CHEST_ENABLE_DISPLAY
    display_status_show_closed();
#endif
}
#endif

static bool chest_controller_delete_ui_active(void)
{
    return s_delete_ui_state != CHEST_DELETE_UI_NORMAL;
}

static void chest_controller_show_delete_result(bool success, bool refresh_list)
{
    esp_err_t ret = voiceprint_delete_ui_show_result(success);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "delete result screen failed: %s", esp_err_to_name(ret));
    }
    s_delete_result_refresh_list = success && refresh_list;
    s_delete_result_until = xTaskGetTickCount()
                            + pdMS_TO_TICKS(CHEST_DELETE_RESULT_SHOW_MS);
    s_delete_ui_state = CHEST_DELETE_UI_RESULT;
}

static void chest_controller_show_delete_error(const char *message)
{
    esp_err_t ret = voiceprint_delete_ui_show_error(message);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "delete error screen failed: %s", esp_err_to_name(ret));
    }
    s_delete_result_refresh_list = false;
    s_delete_result_until = xTaskGetTickCount()
                            + pdMS_TO_TICKS(CHEST_DELETE_RESULT_SHOW_MS);
    s_delete_ui_state = CHEST_DELETE_UI_RESULT;
}

static void chest_controller_request_speaker_list(void)
{
    chest_delete_request_t request = {
        .type = CHEST_DELETE_REQUEST_LIST,
    };

    s_delete_ui_state = CHEST_DELETE_UI_LOADING_LIST;
    esp_err_t ret = voiceprint_delete_ui_show_loading();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "list loading screen failed: %s", esp_err_to_name(ret));
    }
    if (!s_delete_request_queue
        || xQueueSend(s_delete_request_queue, &request, 0) != pdTRUE) {
        ESP_LOGW(TAG, "voiceprint list request queue unavailable");
        chest_controller_show_delete_error("LIST REQUEST FAILED");
    }
}

static void chest_controller_request_speaker_delete(void)
{
    if (s_delete_selected >= s_delete_speaker_count) {
        chest_controller_show_delete_error("NO VOICE SELECTED");
        return;
    }

    chest_delete_request_t request = {
        .type = CHEST_DELETE_REQUEST_DELETE,
        .speaker = s_delete_speakers[s_delete_selected],
    };
    s_delete_ui_state = CHEST_DELETE_UI_DELETING;
    esp_err_t ret = voiceprint_delete_ui_show_deleting();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "deleting screen failed: %s", esp_err_to_name(ret));
    }
    if (!s_delete_request_queue
        || xQueueSend(s_delete_request_queue, &request, 0) != pdTRUE) {
        ESP_LOGW(TAG, "voiceprint delete request queue unavailable");
        chest_controller_show_delete_error("DELETE REQUEST FAILED");
    }
}

static void chest_controller_handle_delete_result(const chest_delete_result_t *result)
{
    if (!result) {
        return;
    }

    if (result->type == CHEST_DELETE_REQUEST_LIST) {
        if (result->result != ESP_OK) {
            ESP_LOGW(TAG, "voiceprint list failed: %s",
                     esp_err_to_name(result->result));
            chest_controller_show_delete_error("LIST LOAD FAILED");
            return;
        }

        s_delete_speaker_count = result->count;
        for (size_t index = 0; index < s_delete_speaker_count; index++) {
            s_delete_speakers[index] = result->speakers[index];
        }
        s_delete_selected = 0;
        s_delete_confirm_selected = true;
        s_delete_ui_state = CHEST_DELETE_UI_LIST;
        esp_err_t ret = voiceprint_delete_ui_show_list(
            s_delete_speakers, s_delete_speaker_count, s_delete_selected);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "voiceprint list screen failed: %s",
                     esp_err_to_name(ret));
        }
        return;
    }

    if (result->type == CHEST_DELETE_REQUEST_DELETE) {
        if (result->result != ESP_OK) {
            ESP_LOGW(TAG, "voiceprint delete failed: %s",
                     esp_err_to_name(result->result));
        }
        chest_controller_show_delete_result(result->result == ESP_OK, true);
    }
}

static void chest_controller_delete_worker_task(void *arg)
{
    (void)arg;

    while (true) {
        chest_delete_request_t request;
        if (xQueueReceive(s_delete_request_queue, &request, portMAX_DELAY)
            != pdTRUE) {
            continue;
        }

        chest_delete_result_t result = {
            .type = request.type,
            .result = ESP_FAIL,
        };
        if (request.type == CHEST_DELETE_REQUEST_LIST) {
            result.result = voiceprint_auth_list_speakers(
                result.speakers, VOICEPRINT_AUTH_MAX_SPEAKERS, &result.count);
        } else if (request.type == CHEST_DELETE_REQUEST_DELETE) {
            result.result = voiceprint_auth_delete_speaker(
                request.speaker.speaker_id);
            if (result.result == ESP_OK) {
                kws_wakeup_refresh_voiceprint_enrollment();
            }
        }

        if (xQueueOverwrite(s_delete_result_queue, &result) != pdPASS) {
            ESP_LOGW(TAG, "voiceprint delete result queue failed");
        }
    }
}

static void chest_controller_handle_delete_button_action(bool long_press)
{
    switch (s_delete_ui_state) {
    case CHEST_DELETE_UI_NORMAL:
        chest_controller_request_speaker_list();
        break;

    case CHEST_DELETE_UI_LIST:
        if (long_press && s_delete_selected == s_delete_speaker_count) {
            s_delete_ui_state = CHEST_DELETE_UI_NORMAL;
            chest_controller_restore_status();
        } else if (long_press) {
            s_delete_confirm_selected = true;
            s_delete_ui_state = CHEST_DELETE_UI_CONFIRM;
            esp_err_t ret = voiceprint_delete_ui_show_confirmation(
                &s_delete_speakers[s_delete_selected], s_delete_confirm_selected);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "delete confirmation screen failed: %s",
                         esp_err_to_name(ret));
            }
        } else {
            s_delete_selected = (s_delete_selected + 1U)
                                % (s_delete_speaker_count + 1U);
            esp_err_t ret = voiceprint_delete_ui_show_list(
                s_delete_speakers, s_delete_speaker_count, s_delete_selected);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "voiceprint list refresh failed: %s",
                         esp_err_to_name(ret));
            }
        }
        break;

    case CHEST_DELETE_UI_CONFIRM:
        if (!long_press) {
            s_delete_confirm_selected = !s_delete_confirm_selected;
            esp_err_t ret = voiceprint_delete_ui_show_confirmation(
                &s_delete_speakers[s_delete_selected], s_delete_confirm_selected);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "delete selection refresh failed: %s",
                         esp_err_to_name(ret));
            }
        } else if (s_delete_confirm_selected) {
            chest_controller_request_speaker_delete();
        } else {
            s_delete_ui_state = CHEST_DELETE_UI_LIST;
            esp_err_t ret = voiceprint_delete_ui_show_list(
                s_delete_speakers, s_delete_speaker_count, s_delete_selected);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "voiceprint list restore failed: %s",
                         esp_err_to_name(ret));
            }
        }
        break;

    case CHEST_DELETE_UI_LOADING_LIST:
    case CHEST_DELETE_UI_DELETING:
    case CHEST_DELETE_UI_RESULT:
    default:
        break;
    }
}

static void chest_controller_scan_delete_button(chest_button_state_t *state)
{
    bool raw_pressed = chest_controller_button_pressed(BOARD_DELETE_BUTTON_PIN);
    TickType_t now = xTaskGetTickCount();

    if (raw_pressed != state->candidate_pressed) {
        state->candidate_pressed = raw_pressed;
        state->candidate_tick = now;
        return;
    }
    if (raw_pressed == state->stable_pressed
        || (now - state->candidate_tick) < pdMS_TO_TICKS(CHEST_BUTTON_DEBOUNCE_MS)) {
        return;
    }

    state->stable_pressed = raw_pressed;
    if (raw_pressed) {
        state->pressed_tick = now;
        return;
    }

    uint32_t press_ms = (uint32_t)(now - state->pressed_tick)
                        * portTICK_PERIOD_MS;
    chest_controller_handle_delete_button_action(
        press_ms >= CHEST_DELETE_BUTTON_LONG_PRESS_MS);
}

static void chest_controller_handle_panbao(const kws_wakeup_event_t *event)
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

#if CONFIG_CHEST_ENABLE_SERVO
    bool close_button_was_pressed =
        chest_controller_button_pressed(BOARD_CLOSE_BUTTON_PIN);
#endif
    chest_button_state_t delete_button = {
        .stable_pressed = chest_controller_button_pressed(BOARD_DELETE_BUTTON_PIN),
        .candidate_pressed = chest_controller_button_pressed(BOARD_DELETE_BUTTON_PIN),
        .candidate_tick = xTaskGetTickCount(),
    };

    while (true) {
        kws_wakeup_event_t event;
        esp_err_t ret =
            kws_wakeup_get_event(&event, CHEST_CONTROLLER_EVENT_WAIT_MS);
        if (ret == ESP_OK) {
            if (!chest_controller_delete_ui_active()) {
                chest_controller_handle_wakeup(&event);
            }
        } else if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "wakeup event wait failed: %s",
                     esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        chest_delete_result_t delete_result;
        if (s_delete_result_queue
            && xQueueReceive(s_delete_result_queue, &delete_result, 0) == pdTRUE) {
            chest_controller_handle_delete_result(&delete_result);
        }

        if (s_delete_ui_state == CHEST_DELETE_UI_RESULT
            && (int32_t)(xTaskGetTickCount() - s_delete_result_until) >= 0) {
            if (s_delete_result_refresh_list) {
                chest_controller_request_speaker_list();
            } else {
                s_delete_ui_state = CHEST_DELETE_UI_NORMAL;
                chest_controller_restore_status();
            }
        }

#if CONFIG_CHEST_ENABLE_SERVO
        bool close_button_is_pressed =
            chest_controller_button_pressed(BOARD_CLOSE_BUTTON_PIN);
        if (close_button_is_pressed && !close_button_was_pressed
            && !chest_controller_delete_ui_active()) {
            vTaskDelay(pdMS_TO_TICKS(CHEST_BUTTON_DEBOUNCE_MS));
            if (chest_controller_button_pressed(BOARD_CLOSE_BUTTON_PIN)
                && !chest_controller_delete_ui_active()) {
                chest_controller_close_lid();
            }
        }
        close_button_was_pressed =
            chest_controller_button_pressed(BOARD_CLOSE_BUTTON_PIN);
#endif

        chest_controller_scan_delete_button(&delete_button);
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

    ret = chest_controller_init_button(BOARD_CLOSE_BUTTON_PIN, "close button");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "close button init failed: %s", esp_err_to_name(ret));
        return ret;
    }
#else
    ESP_LOGI(TAG, "phase 1 voice-only mode: lid actuator disabled");
#endif

    ret = chest_controller_init_button(BOARD_DELETE_BUTTON_PIN, "delete button");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "delete button init failed: %s", esp_err_to_name(ret));
        return ret;
    }

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

    s_delete_request_queue = xQueueCreate(CHEST_DELETE_REQUEST_QUEUE_SIZE,
                                          sizeof(chest_delete_request_t));
    s_delete_result_queue = xQueueCreate(CHEST_DELETE_RESULT_QUEUE_SIZE,
                                         sizeof(chest_delete_result_t));
    if (!s_delete_request_queue || !s_delete_result_queue) {
        ESP_LOGE(TAG, "voiceprint delete queue creation failed");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(chest_controller_delete_worker_task, "voice_delete",
                    CHEST_DELETE_WORKER_TASK_STACK_SIZE, NULL,
                    CHEST_DELETE_WORKER_TASK_PRIORITY,
                    &s_delete_worker_task) != pdPASS) {
        s_delete_worker_task = NULL;
        ESP_LOGE(TAG, "voiceprint delete worker creation failed");
        return ESP_ERR_NO_MEM;
    }

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
