#include "kws_wakeup.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_transport_ws.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "es8311.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "voiceprint_auth.h"

static const char *TAG = "kws_wakeup";

#define KWS_WIFI_CONNECTED_BIT       BIT0
#define KWS_WS_STREAMING_BIT         BIT1
#define KWS_ENROLL_RECORDING_BIT     BIT2
#define KWS_VOICEPRINT_BUSY_BIT      BIT3
#define KWS_WS_EVENT_QUEUE_SIZE      8
#define KWS_PCM_QUEUE_SIZE           100
#define KWS_VOICEPRINT_QUEUE_SIZE    2
#define KWS_TEXT_MESSAGE_MAX_BYTES   2048
#define KWS_TASK_STACK_SIZE          6144
#define KWS_TASK_PRIORITY            5
#define KWS_AUDIO_TASK_PRIORITY      6
#define KWS_VOICEPRINT_TASK_STACK_SIZE 6144
#define KWS_VOICEPRINT_TASK_PRIORITY 5
#define KWS_WS_SEND_TIMEOUT_MS       1000
#define KWS_WS_CLOSE_TIMEOUT_MS      500
#define KWS_WIFI_WAIT_LOG_MS         5000
#define KWS_STREAM_HEALTH_LOG_MS     2000
#define KWS_STREAM_HEALTH_GRACE_MS   5000
#define KWS_PCM_BACKLOG_WARN_FRAMES  (KWS_PCM_QUEUE_SIZE / 2)
#define KWS_PCM_BACKLOG_MAX_FRAMES   ((KWS_PCM_QUEUE_SIZE * 9) / 10)
#define KWS_STREAM_UNHEALTHY_CHECKS  3
#define KWS_UPLOAD_BATCH_FRAMES      10
#define KWS_UPLOAD_BATCH_BYTES       (ES8311_PCM_FRAME_BYTES * KWS_UPLOAD_BATCH_FRAMES)
#define KWS_VOICEPRINT_VERIFY_SECONDS 3
#define KWS_VOICEPRINT_ENROLL_MIN_SECONDS 3
#define KWS_VOICEPRINT_ENROLL_MAX_SECONDS 15
#define KWS_VOICEPRINT_VERIFY_BYTES \
    ((size_t)ES8311_SAMPLE_RATE_HZ * (ES8311_BITS_PER_SAMPLE / 8) \
     * KWS_VOICEPRINT_VERIFY_SECONDS)
#define KWS_VOICEPRINT_ENROLL_MIN_BYTES \
    ((size_t)ES8311_SAMPLE_RATE_HZ * (ES8311_BITS_PER_SAMPLE / 8) \
     * KWS_VOICEPRINT_ENROLL_MIN_SECONDS)
#define KWS_VOICEPRINT_ENROLL_MAX_BYTES \
    ((size_t)ES8311_SAMPLE_RATE_HZ * (ES8311_BITS_PER_SAMPLE / 8) \
     * KWS_VOICEPRINT_ENROLL_MAX_SECONDS)
#define KWS_BUTTON_DEBOUNCE_FRAMES 2

#if CONFIG_KWS_STEREO_DIAGNOSTICS
#define KWS_AUDIO_TASK_STACK_SIZE    6144
#define KWS_CAPTURE_TASK_STACK_SIZE  6144
#define KWS_CAPTURE_TASK_PRIORITY    2
#define KWS_COMMAND_TASK_STACK_SIZE  3072
#define KWS_COMMAND_TASK_PRIORITY    3
#define KWS_CAPTURE_B64_INPUT_BYTES  384
#define KWS_CAPTURE_B64_OUTPUT_BYTES 512
#define KWS_CAPTURE_CLIP_THRESHOLD   32760
#else
#define KWS_AUDIO_TASK_STACK_SIZE    4096
#endif

typedef enum {
    KWS_WS_EVENT_CONNECTED = 0,
    KWS_WS_EVENT_DETECTED,
    KWS_WS_EVENT_STOPPED,
    KWS_WS_EVENT_DISCONNECTED,
    KWS_WS_EVENT_ERROR,
} kws_ws_event_type_t;

typedef struct {
    kws_ws_event_type_t type;
    char keyword[KWS_WAKEUP_KEYWORD_MAX_BYTES];
    char reason[48];
    char message[128];
    float confidence;
} kws_ws_event_t;

typedef struct {
    char data[KWS_TEXT_MESSAGE_MAX_BYTES];
    size_t length;
    size_t frame_offset;
    bool active;
} kws_text_message_t;

typedef struct {
    uint8_t data[ES8311_PCM_FRAME_BYTES];
    size_t length;
    uint32_t sequence;
} kws_pcm_frame_t;

typedef enum {
    KWS_VOICEPRINT_REQUEST_ENROLL = 0,
    KWS_VOICEPRINT_REQUEST_VERIFY,
} kws_voiceprint_request_type_t;

typedef struct {
    kws_voiceprint_request_type_t type;
    uint8_t *pcm;
    size_t pcm_len;
    bool free_pcm;
    kws_ws_event_t wake_candidate;
} kws_voiceprint_request_t;

typedef struct {
    kws_voiceprint_request_type_t type;
    esp_err_t result;
    kws_ws_event_t wake_candidate;
    union {
        voiceprint_enroll_result_t enroll;
        voiceprint_verify_result_t verify;
    } data;
} kws_voiceprint_event_t;

#if CONFIG_KWS_STEREO_DIAGNOSTICS
typedef enum {
    KWS_CAPTURE_IDLE = 0,
    KWS_CAPTURE_WAIT_PROMPT,
    KWS_CAPTURE_RUNNING,
    KWS_CAPTURE_READY,
    KWS_CAPTURE_EXPORTING,
    KWS_CAPTURE_DONE,
} kws_capture_state_t;
#endif

static EventGroupHandle_t s_state_bits;
static QueueHandle_t s_ws_event_queue;
static QueueHandle_t s_wakeup_event_queue;
static QueueHandle_t s_pcm_queue;
static QueueHandle_t s_voiceprint_request_queue;
static QueueHandle_t s_voiceprint_event_queue;
static SemaphoreHandle_t s_recent_pcm_mutex;
static TaskHandle_t s_kws_task;
static TaskHandle_t s_audio_task;
static TaskHandle_t s_voiceprint_task;
#if CONFIG_KWS_STEREO_DIAGNOSTICS
static TaskHandle_t s_capture_task;
static TaskHandle_t s_command_task;
#endif
static esp_websocket_client_handle_t s_ws_client;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static kws_text_message_t s_text_message;
static volatile uint32_t s_pcm_captured_frames;
static volatile uint32_t s_pcm_dropped_frames;
static uint8_t s_upload_batch[KWS_UPLOAD_BATCH_BYTES];
static uint8_t *s_recent_pcm;
static size_t s_recent_pcm_write;
static size_t s_recent_pcm_filled;
static uint8_t *s_enroll_pcm;
static size_t s_enroll_pcm_filled;
static volatile bool s_voiceprint_enrolled;
static int s_enroll_button_stable_level = 1;
static int s_enroll_button_candidate_level = 1;
static uint8_t s_enroll_button_candidate_frames;
#if CONFIG_KWS_STEREO_DIAGNOSTICS
static uint8_t *s_capture_buffer;
static size_t s_capture_capacity_bytes;
static volatile size_t s_capture_fill_bytes;
static volatile kws_capture_state_t s_capture_state;
static volatile bool s_capture_requested;
static TickType_t s_capture_prompt_tick;
static portMUX_TYPE s_capture_lock = portMUX_INITIALIZER_UNLOCKED;
#endif

#if CONFIG_KWS_STEREO_DIAGNOSTICS
static void kws_capture_arm(void)
{
    bool armed = false;
    TickType_t prompt_tick =
        xTaskGetTickCount() + pdMS_TO_TICKS(CONFIG_KWS_STEREO_CAPTURE_DELAY_MS);
    portENTER_CRITICAL(&s_capture_lock);
    if (s_capture_buffer && s_capture_state == KWS_CAPTURE_IDLE) {
        s_capture_fill_bytes = 0;
        s_capture_prompt_tick = prompt_tick;
        s_capture_requested = false;
        s_capture_state = KWS_CAPTURE_WAIT_PROMPT;
        armed = true;
    }
    portEXIT_CRITICAL(&s_capture_lock);

    if (armed) {
        ESP_LOGI(TAG,
                 "stereo capture armed: send CAPTURE or wait %d ms, duration=%d s",
                 CONFIG_KWS_STEREO_CAPTURE_DELAY_MS,
                 CONFIG_KWS_STEREO_CAPTURE_SECONDS);
    }
}

static void kws_capture_request(void)
{
    bool accepted = false;
    portENTER_CRITICAL(&s_capture_lock);
    if (s_capture_state == KWS_CAPTURE_WAIT_PROMPT) {
        s_capture_requested = true;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_capture_lock);

    if (accepted) {
        ESP_LOGI(TAG, "CAPTURE command accepted");
    } else {
        ESP_LOGW(TAG, "CAPTURE command ignored; diagnostic is not armed");
    }
}

static void kws_capture_cancel_waiting(void)
{
    portENTER_CRITICAL(&s_capture_lock);
    if (s_capture_state == KWS_CAPTURE_WAIT_PROMPT) {
        s_capture_state = KWS_CAPTURE_IDLE;
        s_capture_fill_bytes = 0;
        s_capture_requested = false;
    }
    portEXIT_CRITICAL(&s_capture_lock);
}
#endif

static void kws_text_message_reset(void)
{
    memset(&s_text_message, 0, sizeof(s_text_message));
}

static void kws_ws_post_event(const kws_ws_event_t *event)
{
    if (!s_ws_event_queue || !event) {
        return;
    }
    if (xQueueSend(s_ws_event_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "WebSocket event queue full, dropping type=%d", event->type);
    }
}

static void kws_handle_server_message(const char *data, size_t length)
{
    cJSON *root = cJSON_ParseWithLength(data, length);
    if (!root) {
        ESP_LOGW(TAG, "invalid KWS JSON: %.*s", (int)(length > 96 ? 96 : length), data);
        return;
    }

    const cJSON *type_json = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type_json) || !type_json->valuestring) {
        cJSON_Delete(root);
        return;
    }

    const char *type = type_json->valuestring;
    if (strcmp(type, "session_started") == 0) {
        xEventGroupSetBits(s_state_bits, KWS_WS_STREAMING_BIT);
        ESP_LOGI(TAG, "KWS session started, 200 ms PCM upload enabled");
#if CONFIG_KWS_STEREO_DIAGNOSTICS
        kws_capture_arm();
#endif
    } else if (strcmp(type, "listening") == 0) {
        const cJSON *audio_seconds =
            cJSON_GetObjectItemCaseSensitive(root, "audio_seconds");
        const cJSON *elapsed_seconds =
            cJSON_GetObjectItemCaseSensitive(root, "elapsed_seconds");
        const cJSON *chunks_received =
            cJSON_GetObjectItemCaseSensitive(root, "chunks_received");
        const cJSON *level_db =
            cJSON_GetObjectItemCaseSensitive(root, "level_db");
        if (cJSON_IsNumber(audio_seconds)
            && cJSON_IsNumber(elapsed_seconds)
            && cJSON_IsNumber(chunks_received)
            && cJSON_IsNumber(level_db)) {
            ESP_LOGI(TAG,
                     "KWS listening: audio=%.1fs elapsed=%.1fs chunks=%d level=%.1fdBFS",
                     audio_seconds->valuedouble,
                     elapsed_seconds->valuedouble,
                     chunks_received->valueint,
                     level_db->valuedouble);
        } else {
            ESP_LOGI(TAG, "KWS backend is listening");
        }
    } else if (strcmp(type, "detected") == 0) {
        kws_ws_event_t event = { .type = KWS_WS_EVENT_DETECTED };
        const cJSON *keyword = cJSON_GetObjectItemCaseSensitive(root, "keyword");
        const cJSON *confidence = cJSON_GetObjectItemCaseSensitive(root, "confidence");
        if (cJSON_IsString(keyword) && keyword->valuestring) {
            strlcpy(event.keyword, keyword->valuestring, sizeof(event.keyword));
        }
        if (cJSON_IsNumber(confidence)) {
            event.confidence = (float)confidence->valuedouble;
        }
        xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
        kws_ws_post_event(&event);
    } else if (strcmp(type, "stopped") == 0) {
        kws_ws_event_t event = { .type = KWS_WS_EVENT_STOPPED };
        const cJSON *reason = cJSON_GetObjectItemCaseSensitive(root, "reason");
        if (cJSON_IsString(reason) && reason->valuestring) {
            strlcpy(event.reason, reason->valuestring, sizeof(event.reason));
        }
        xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
        kws_ws_post_event(&event);
    } else if (strcmp(type, "error") == 0) {
        kws_ws_event_t event = { .type = KWS_WS_EVENT_ERROR };
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
        const cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
        if (cJSON_IsString(code) && code->valuestring) {
            strlcpy(event.reason, code->valuestring, sizeof(event.reason));
        }
        if (cJSON_IsString(message) && message->valuestring) {
            strlcpy(event.message, message->valuestring, sizeof(event.message));
        }
        xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
        kws_ws_post_event(&event);
    }

    cJSON_Delete(root);
}

static void kws_handle_text_frame(const esp_websocket_event_data_t *event)
{
    if (!event || event->payload_offset < 0 || event->payload_len < 0 || event->data_len < 0) {
        kws_text_message_reset();
        return;
    }

    if (event->payload_offset == 0) {
        s_text_message.frame_offset = 0;
        if (event->op_code == WS_TRANSPORT_OPCODES_TEXT) {
            s_text_message.length = 0;
            s_text_message.active = true;
        } else if (event->op_code != WS_TRANSPORT_OPCODES_CONT || !s_text_message.active) {
            return;
        }
    }

    if (!s_text_message.active
        || (size_t)event->payload_offset != s_text_message.frame_offset
        || s_text_message.length + (size_t)event->data_len
               >= sizeof(s_text_message.data)) {
        ESP_LOGW(TAG, "discarding oversized or discontinuous KWS text message");
        kws_text_message_reset();
        return;
    }

    if (event->data_len > 0 && event->data_ptr) {
        memcpy(s_text_message.data + s_text_message.length, event->data_ptr,
               (size_t)event->data_len);
        s_text_message.length += (size_t)event->data_len;
        s_text_message.frame_offset += (size_t)event->data_len;
    }
    s_text_message.data[s_text_message.length] = '\0';

    if (s_text_message.frame_offset == (size_t)event->payload_len) {
        s_text_message.frame_offset = 0;
        if (event->fin) {
            kws_handle_server_message(s_text_message.data, s_text_message.length);
            kws_text_message_reset();
        }
    }
}

static void kws_websocket_event_handler(void *handler_args, esp_event_base_t base,
                                        int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_websocket_event_data_t *event = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "KWS WebSocket connected");
        kws_ws_post_event(&(kws_ws_event_t) { .type = KWS_WS_EVENT_CONNECTED });
        break;

    case WEBSOCKET_EVENT_DATA:
        if (!event) {
            break;
        }
        if (event->op_code == WS_TRANSPORT_OPCODES_TEXT
            || event->op_code == WS_TRANSPORT_OPCODES_CONT
            || (event->payload_offset > 0 && s_text_message.active)) {
            kws_handle_text_frame(event);
        } else if (event->op_code == WS_TRANSPORT_OPCODES_CLOSE) {
            ESP_LOGW(TAG, "KWS WebSocket close frame received");
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED: {
        xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
        kws_ws_event_t ws_event = { .type = KWS_WS_EVENT_DISCONNECTED };
        if (event) {
            snprintf(ws_event.message, sizeof(ws_event.message),
                     "http=%d errno=%d",
                     event->error_handle.esp_ws_handshake_status_code,
                     event->error_handle.esp_transport_sock_errno);
        }
        kws_ws_post_event(&ws_event);
        break;
    }

    case WEBSOCKET_EVENT_ERROR: {
        xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
        kws_ws_event_t ws_event = { .type = KWS_WS_EVENT_ERROR };
        strlcpy(ws_event.reason, "websocket_error", sizeof(ws_event.reason));
        if (event) {
            snprintf(ws_event.message, sizeof(ws_event.message),
                     "type=%d http=%d errno=%d",
                     event->error_handle.error_type,
                     event->error_handle.esp_ws_handshake_status_code,
                     event->error_handle.esp_transport_sock_errno);
        }
        kws_ws_post_event(&ws_event);
        break;
    }

    case WEBSOCKET_EVENT_CLOSED:
        xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
        ESP_LOGW(TAG, "KWS WebSocket closed by peer");
        kws_ws_post_event(&(kws_ws_event_t) { .type = KWS_WS_EVENT_DISCONNECTED });
        break;

    default:
        break;
    }
}

static esp_err_t kws_send_start_message(void)
{
    if (!s_ws_client || !esp_websocket_client_is_connected(s_ws_client)) {
        return ESP_ERR_INVALID_STATE;
    }

    char session_id[32];
    snprintf(session_id, sizeof(session_id), "%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "start");
    cJSON_AddStringToObject(root, "product_id", CONFIG_KWS_PRODUCT_ID);
    cJSON_AddStringToObject(root, "device_id", CONFIG_KWS_DEVICE_ID);
    cJSON_AddStringToObject(root, "session_id", session_id);
    cJSON_AddNumberToObject(root, "sample_rate", ES8311_SAMPLE_RATE_HZ);
    cJSON_AddStringToObject(root, "format", "pcm_s16le");
    if (CONFIG_KWS_AUTH_TOKEN[0] != '\0') {
        cJSON_AddStringToObject(root, "token", CONFIG_KWS_AUTH_TOKEN);
    }

    char *message = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!message) {
        return ESP_ERR_NO_MEM;
    }

    xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
    int sent = esp_websocket_client_send_text(s_ws_client, message, (int)strlen(message),
                                               pdMS_TO_TICKS(KWS_WS_SEND_TIMEOUT_MS));
    free(message);
    if (sent <= 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "KWS start sent: product=%s device=%s session=%s",
             CONFIG_KWS_PRODUCT_ID, CONFIG_KWS_DEVICE_ID, session_id);
    return ESP_OK;
}

static esp_err_t kws_websocket_open(void)
{
    xQueueReset(s_ws_event_queue);
    xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
    kws_text_message_reset();

    const esp_websocket_client_config_t config = {
        .uri = CONFIG_KWS_SERVER_URI,
        .disable_auto_reconnect = true,
        .task_prio = KWS_TASK_PRIORITY,
        .task_stack = 4096,
        .buffer_size = 8192,
        .keep_alive_enable = true,
        .keep_alive_idle = 10,
        .keep_alive_interval = 5,
        .keep_alive_count = 3,
        .network_timeout_ms = 5000,
    };

    s_ws_client = esp_websocket_client_init(&config);
    if (!s_ws_client) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                                   kws_websocket_event_handler, NULL);
    if (ret == ESP_OK) {
        ret = esp_websocket_client_start(s_ws_client);
    }
    if (ret != ESP_OK) {
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "connecting to %s", CONFIG_KWS_SERVER_URI);
    return ESP_OK;
}

static void kws_websocket_close(void)
{
    xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
#if CONFIG_KWS_STEREO_DIAGNOSTICS
    kws_capture_cancel_waiting();
#endif
    if (!s_ws_client) {
        return;
    }

    if (esp_websocket_client_is_connected(s_ws_client)) {
        esp_websocket_client_close(s_ws_client, pdMS_TO_TICKS(KWS_WS_CLOSE_TIMEOUT_MS));
    }
    esp_websocket_client_stop(s_ws_client);
    esp_websocket_client_destroy(s_ws_client);
    s_ws_client = NULL;
}

static kws_wakeup_keyword_t kws_classify_keyword(const char *keyword)
{
    if (strcmp(keyword, "你好盘宝") == 0) {
        return KWS_WAKEUP_KEYWORD_PANBAO;
    }
    if (strcmp(keyword, "你好鹰老师") == 0) {
        return KWS_WAKEUP_KEYWORD_YING_LAOSHI;
    }
    return KWS_WAKEUP_KEYWORD_UNKNOWN;
}

static void kws_recent_pcm_write(const uint8_t *pcm, size_t bytes)
{
    if (!s_recent_pcm || !s_recent_pcm_mutex || !pcm || bytes == 0
        || bytes > KWS_VOICEPRINT_VERIFY_BYTES) {
        return;
    }
    if (xSemaphoreTake(s_recent_pcm_mutex, pdMS_TO_TICKS(ES8311_PCM_FRAME_MS))
        != pdTRUE) {
        ESP_LOGW(TAG, "recent PCM mutex timeout");
        return;
    }

    size_t first = KWS_VOICEPRINT_VERIFY_BYTES - s_recent_pcm_write;
    if (first > bytes) {
        first = bytes;
    }
    memcpy(s_recent_pcm + s_recent_pcm_write, pcm, first);
    if (bytes > first) {
        memcpy(s_recent_pcm, pcm + first, bytes - first);
    }
    s_recent_pcm_write =
        (s_recent_pcm_write + bytes) % KWS_VOICEPRINT_VERIFY_BYTES;
    if (s_recent_pcm_filled < KWS_VOICEPRINT_VERIFY_BYTES) {
        size_t filled = s_recent_pcm_filled + bytes;
        s_recent_pcm_filled =
            filled < KWS_VOICEPRINT_VERIFY_BYTES
                ? filled : KWS_VOICEPRINT_VERIFY_BYTES;
    }
    xSemaphoreGive(s_recent_pcm_mutex);
}

static esp_err_t kws_recent_pcm_snapshot(uint8_t *output, size_t bytes)
{
    if (!output || bytes != KWS_VOICEPRINT_VERIFY_BYTES
        || !s_recent_pcm || !s_recent_pcm_mutex) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_recent_pcm_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_recent_pcm_filled < bytes) {
        xSemaphoreGive(s_recent_pcm_mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t start =
        (s_recent_pcm_write + KWS_VOICEPRINT_VERIFY_BYTES - bytes)
        % KWS_VOICEPRINT_VERIFY_BYTES;
    size_t first = KWS_VOICEPRINT_VERIFY_BYTES - start;
    if (first > bytes) {
        first = bytes;
    }
    memcpy(output, s_recent_pcm + start, first);
    if (bytes > first) {
        memcpy(output + first, s_recent_pcm, bytes - first);
    }
    xSemaphoreGive(s_recent_pcm_mutex);
    return ESP_OK;
}

static esp_err_t kws_submit_voiceprint_request(
    const kws_voiceprint_request_t *request)
{
    if (!request || !s_voiceprint_request_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    xEventGroupSetBits(s_state_bits, KWS_VOICEPRINT_BUSY_BIT);
    if (xQueueSend(s_voiceprint_request_queue, request, 0) != pdTRUE) {
        xEventGroupClearBits(s_state_bits, KWS_VOICEPRINT_BUSY_BIT);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void kws_finish_enrollment(bool reached_limit)
{
    if ((xEventGroupGetBits(s_state_bits) & KWS_ENROLL_RECORDING_BIT) == 0) {
        return;
    }

    size_t pcm_len = s_enroll_pcm_filled;
    xEventGroupClearBits(s_state_bits, KWS_ENROLL_RECORDING_BIT);
    uint32_t duration_ms =
        (uint32_t)(pcm_len * 1000U
                   / (ES8311_SAMPLE_RATE_HZ
                      * (ES8311_BITS_PER_SAMPLE / 8)));
    ESP_LOGI(TAG, "voiceprint enroll recording stopped: %lu ms, %u bytes%s",
             (unsigned long)duration_ms, (unsigned)pcm_len,
             reached_limit ? " (maximum reached)" : "");

    if (pcm_len < KWS_VOICEPRINT_ENROLL_MIN_BYTES) {
        ESP_LOGW(TAG,
                 "=== VOICEPRINT ENROLL TOO SHORT: hold GPIO%d for at least %d seconds ===",
                 BOARD_ENROLL_BUTTON_PIN,
                 KWS_VOICEPRINT_ENROLL_MIN_SECONDS);
        return;
    }

    kws_voiceprint_request_t request = {
        .type = KWS_VOICEPRINT_REQUEST_ENROLL,
        .pcm = s_enroll_pcm,
        .pcm_len = pcm_len,
        .free_pcm = false,
    };
    esp_err_t ret = kws_submit_voiceprint_request(&request);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "voiceprint enroll submit failed: %s",
                 esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG,
             "=== VOICEPRINT ENROLL UPLOADING: speaker=%s duration=%lu ms ===",
             voiceprint_auth_speaker_id(), (unsigned long)duration_ms);
}

static void kws_start_enrollment(void)
{
    EventBits_t bits = xEventGroupGetBits(s_state_bits);
    if ((bits & (KWS_ENROLL_RECORDING_BIT | KWS_VOICEPRINT_BUSY_BIT)) != 0) {
        ESP_LOGW(TAG, "voiceprint enroll button ignored while auth is busy");
        return;
    }

    s_enroll_pcm_filled = 0;
    xEventGroupSetBits(s_state_bits, KWS_ENROLL_RECORDING_BIT);
    xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
    xQueueReset(s_pcm_queue);
    ESP_LOGW(TAG,
             "=== VOICEPRINT ENROLL START: hold GPIO%d and speak freely; release after at least %d seconds ===",
             BOARD_ENROLL_BUTTON_PIN, KWS_VOICEPRINT_ENROLL_MIN_SECONDS);
}

static void kws_service_enroll_button(void)
{
    int raw_level = gpio_get_level(BOARD_ENROLL_BUTTON_PIN);
    if (raw_level != s_enroll_button_candidate_level) {
        s_enroll_button_candidate_level = raw_level;
        s_enroll_button_candidate_frames = 1;
        return;
    }
    if (s_enroll_button_candidate_frames < KWS_BUTTON_DEBOUNCE_FRAMES) {
        s_enroll_button_candidate_frames++;
    }
    if (s_enroll_button_candidate_frames < KWS_BUTTON_DEBOUNCE_FRAMES
        || raw_level == s_enroll_button_stable_level) {
        return;
    }

    s_enroll_button_stable_level = raw_level;
    if (raw_level == BOARD_BUTTON_ACTIVE_LEVEL) {
        kws_start_enrollment();
    } else {
        kws_finish_enrollment(false);
    }
}

static void kws_voiceprint_collect_audio(const uint8_t *pcm, size_t bytes)
{
    kws_recent_pcm_write(pcm, bytes);
    kws_service_enroll_button();

    if ((xEventGroupGetBits(s_state_bits) & KWS_ENROLL_RECORDING_BIT) == 0) {
        return;
    }

    size_t remaining =
        KWS_VOICEPRINT_ENROLL_MAX_BYTES - s_enroll_pcm_filled;
    size_t copy_bytes = bytes < remaining ? bytes : remaining;
    memcpy(s_enroll_pcm + s_enroll_pcm_filled, pcm, copy_bytes);
    s_enroll_pcm_filled += copy_bytes;
    if (s_enroll_pcm_filled >= KWS_VOICEPRINT_ENROLL_MAX_BYTES) {
        kws_finish_enrollment(true);
    }
}

static void kws_voiceprint_worker_task(void *arg)
{
    (void)arg;
    while (true) {
        kws_voiceprint_request_t request;
        if (xQueueReceive(s_voiceprint_request_queue, &request, portMAX_DELAY)
            != pdTRUE) {
            continue;
        }

        kws_voiceprint_event_t event = {
            .type = request.type,
            .wake_candidate = request.wake_candidate,
        };
        if (request.type == KWS_VOICEPRINT_REQUEST_ENROLL) {
            event.result =
                voiceprint_auth_enroll(request.pcm, request.pcm_len,
                                       &event.data.enroll);
        } else {
            event.result =
                voiceprint_auth_verify(request.pcm, request.pcm_len,
                                       &event.data.verify);
        }

        if (request.free_pcm) {
            free(request.pcm);
        }
        xQueueSend(s_voiceprint_event_queue, &event, portMAX_DELAY);
    }
}

static void kws_publish_wakeup(
    const kws_ws_event_t *ws_event,
    const voiceprint_verify_result_t *voiceprint)
{
    kws_wakeup_event_t event = {
        .type = kws_classify_keyword(ws_event->keyword),
        .confidence = ws_event->confidence,
        .voiceprint_score = voiceprint->score,
        .voiceprint_threshold = voiceprint->threshold,
    };
    strlcpy(event.keyword, ws_event->keyword, sizeof(event.keyword));
    strlcpy(event.speaker_id, voiceprint->speaker_id,
            sizeof(event.speaker_id));
    xQueueOverwrite(s_wakeup_event_queue, &event);

    ESP_LOGI(TAG,
             "=== KWS + VOICEPRINT WAKEUP: %s confidence=%.3f speaker=%s score=%.3f threshold=%.3f ===",
             event.keyword, event.confidence, event.speaker_id,
             event.voiceprint_score, event.voiceprint_threshold);
}

static void kws_process_voiceprint_events(void)
{
    kws_voiceprint_event_t event;
    while (xQueueReceive(s_voiceprint_event_queue, &event, 0) == pdTRUE) {
        if (event.type == KWS_VOICEPRINT_REQUEST_ENROLL) {
            bool response_ok =
                event.result == ESP_OK && event.data.enroll.ok
                && strcmp(event.data.enroll.speaker_id,
                          voiceprint_auth_speaker_id()) == 0;
            if (!response_ok) {
                ESP_LOGE(TAG,
                         "=== VOICEPRINT ENROLL FAILED: ret=%s response_id=%s ===",
                         event.result == ESP_OK
                             ? "invalid_response"
                             : esp_err_to_name(event.result),
                         event.data.enroll.speaker_id);
            } else {
                esp_err_t persist_ret = voiceprint_auth_set_enrolled(true);
                if (persist_ret == ESP_OK) {
                    s_voiceprint_enrolled = true;
                    ESP_LOGI(
                        TAG,
                        "=== VOICEPRINT ENROLL SUCCESS: speaker=%s enrollment_count=%u; KWS listening will start ===",
                        event.data.enroll.speaker_id,
                        (unsigned)event.data.enroll.enrollment_count);
                } else {
                    ESP_LOGE(TAG,
                             "voiceprint enrollment NVS persist failed: %s",
                             esp_err_to_name(persist_ret));
                }
            }
        } else {
            const voiceprint_verify_result_t *verify = &event.data.verify;
            bool speaker_ok =
                strcmp(verify->speaker_id,
                       voiceprint_auth_speaker_id()) == 0;
            if (event.result == ESP_OK && verify->ok && verify->matched
                && speaker_ok) {
                ESP_LOGI(TAG,
                         "=== VOICEPRINT VERIFIED: speaker=%s score=%.3f threshold=%.3f ===",
                         verify->speaker_id, verify->score,
                         verify->threshold);
                kws_publish_wakeup(&event.wake_candidate, verify);
            } else if (event.result == ESP_OK && verify->ok) {
                ESP_LOGW(TAG,
                         "=== WAKEUP REJECTED: voiceprint mismatch speaker=%s score=%.3f threshold=%.3f ===",
                         verify->speaker_id, verify->score,
                         verify->threshold);
            } else {
                ESP_LOGE(TAG,
                         "=== WAKEUP REJECTED: voiceprint request failed: %s ===",
                         event.result == ESP_OK
                             ? "invalid_response"
                             : esp_err_to_name(event.result));
            }
        }
        xEventGroupClearBits(s_state_bits, KWS_VOICEPRINT_BUSY_BIT);
    }
}

static esp_err_t kws_submit_verification(const kws_ws_event_t *candidate)
{
    if (!s_voiceprint_enrolled) {
        ESP_LOGW(TAG,
                 "KWS candidate ignored because no voiceprint is enrolled");
        return ESP_ERR_INVALID_STATE;
    }
    if ((xEventGroupGetBits(s_state_bits) & KWS_VOICEPRINT_BUSY_BIT) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t *pcm =
        heap_caps_malloc(KWS_VOICEPRINT_VERIFY_BYTES,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret =
        kws_recent_pcm_snapshot(pcm, KWS_VOICEPRINT_VERIFY_BYTES);
    if (ret != ESP_OK) {
        free(pcm);
        return ret;
    }

    kws_voiceprint_request_t request = {
        .type = KWS_VOICEPRINT_REQUEST_VERIFY,
        .pcm = pcm,
        .pcm_len = KWS_VOICEPRINT_VERIFY_BYTES,
        .free_pcm = true,
        .wake_candidate = *candidate,
    };
    ret = kws_submit_voiceprint_request(&request);
    if (ret != ESP_OK) {
        free(pcm);
        return ret;
    }

    ESP_LOGI(TAG,
             "=== KWS CANDIDATE: %s confidence=%.3f; verifying %d seconds of voice audio ===",
             candidate->keyword, candidate->confidence,
             KWS_VOICEPRINT_VERIFY_SECONDS);
    return ESP_OK;
}

static bool kws_process_ws_events(void)
{
    kws_ws_event_t event;
    while (xQueueReceive(s_ws_event_queue, &event, 0) == pdTRUE) {
        switch (event.type) {
        case KWS_WS_EVENT_CONNECTED:
            if (kws_send_start_message() != ESP_OK) {
                ESP_LOGE(TAG, "failed to send KWS start message");
                return false;
            }
            break;

        case KWS_WS_EVENT_DETECTED:
            if (kws_submit_verification(&event) != ESP_OK) {
                ESP_LOGE(TAG, "voiceprint verification submit failed");
            }
            return false;

        case KWS_WS_EVENT_STOPPED:
            ESP_LOGW(TAG, "KWS session stopped: %s", event.reason);
            return false;

        case KWS_WS_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "KWS disconnected: %s", event.message);
            return false;

        case KWS_WS_EVENT_ERROR:
            ESP_LOGE(TAG, "KWS error: %s %s", event.reason, event.message);
            return false;
        }
    }
    return true;
}

static bool kws_send_pcm_frame(const uint8_t *pcm, size_t bytes)
{
    int sent = esp_websocket_client_send_bin(
        s_ws_client, (const char *)pcm, (int)bytes,
        pdMS_TO_TICKS(KWS_WS_SEND_TIMEOUT_MS));
    if (sent != (int)bytes) {
        ESP_LOGE(TAG, "PCM upload failed: sent=%d expected=%u",
                 sent, (unsigned)bytes);
        xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
        return false;
    }
    return true;
}

#if CONFIG_KWS_STEREO_DIAGNOSTICS
static void kws_capture_log_channel_stats(const int16_t *stereo,
                                          size_t stereo_frames,
                                          size_t channel,
                                          const char *name)
{
    int16_t min_sample = INT16_MAX;
    int16_t max_sample = INT16_MIN;
    int16_t previous = 0;
    int64_t sum = 0;
    uint64_t sum_squares = 0;
    uint32_t clipping = 0;
    uint32_t zero_crossings = 0;
    int32_t peak = 0;

    for (size_t frame = 0; frame < stereo_frames; ++frame) {
        int16_t sample =
            stereo[frame * ES8311_STEREO_CHANNELS + channel];
        if (sample < min_sample) {
            min_sample = sample;
        }
        if (sample > max_sample) {
            max_sample = sample;
        }
        int32_t magnitude = sample == INT16_MIN ? 32768
                                                : (sample < 0 ? -sample : sample);
        if (magnitude > peak) {
            peak = magnitude;
        }
        if (magnitude >= KWS_CAPTURE_CLIP_THRESHOLD) {
            clipping++;
        }
        if (frame > 0
            && ((previous < 0 && sample >= 0)
                || (previous >= 0 && sample < 0))) {
            zero_crossings++;
        }
        previous = sample;
        sum += sample;
        sum_squares += (uint64_t)((int64_t)sample * sample);
    }

    double mean = stereo_frames > 0 ? (double)sum / stereo_frames : 0.0;
    double raw_power =
        stereo_frames > 0 ? (double)sum_squares / stereo_frames : 0.0;
    double ac_power = raw_power - mean * mean;
    if (ac_power < 0.0) {
        ac_power = 0.0;
    }
    double raw_rms = sqrt(raw_power);
    double ac_rms = sqrt(ac_power);
    double ac_db = ac_rms > 0.0 ? 20.0 * log10(ac_rms / 32768.0) : -160.0;
    double peak_db = peak > 0 ? 20.0 * log10((double)peak / 32768.0) : -160.0;
    double zcr_percent =
        stereo_frames > 1
            ? (double)zero_crossings * 100.0 / (stereo_frames - 1)
            : 0.0;

    ESP_LOGI(TAG,
             "%s: min=%d max=%d mean=%.1f raw_rms=%.1f "
             "ac_rms=%.1f(%+.1fdBFS) peak=%ld(%+.1fdBFS) "
             "clip=%lu zcr=%lu(%.1f%%)",
             name, min_sample, max_sample, mean, raw_rms, ac_rms, ac_db,
             (long)peak, peak_db, (unsigned long)clipping,
             (unsigned long)zero_crossings, zcr_percent);
}

static void kws_capture_export_task(void *arg)
{
    (void)arg;
    uint8_t encoded[KWS_CAPTURE_B64_OUTPUT_BYTES + 1];

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_capture_buffer || s_capture_state != KWS_CAPTURE_READY) {
            continue;
        }

        s_capture_state = KWS_CAPTURE_EXPORTING;
        const size_t bytes = s_capture_fill_bytes;
        const size_t stereo_frames =
            bytes / (ES8311_STEREO_CHANNELS * sizeof(int16_t));
        const int16_t *stereo = (const int16_t *)s_capture_buffer;

        ESP_LOGI(TAG, "stereo capture complete: %u bytes, %u frames",
                 (unsigned)bytes, (unsigned)stereo_frames);
        kws_capture_log_channel_stats(stereo, stereo_frames, 0, "MIC LEFT ");
        kws_capture_log_channel_stats(stereo, stereo_frames, 1, "MIC RIGHT");
        ESP_LOGI(TAG, "Base64 export started; KWS audio capture remains active");

        printf("KWS_STEREO_B64_BEGIN bytes=%u sample_rate=%u channels=%u "
               "bits=%u chunk_bytes=%u\n",
               (unsigned)bytes, ES8311_SAMPLE_RATE_HZ,
               ES8311_STEREO_CHANNELS, ES8311_BITS_PER_SAMPLE,
               KWS_CAPTURE_B64_INPUT_BYTES);

        size_t offset = 0;
        uint32_t chunk_index = 0;
        bool export_ok = true;
        while (offset < bytes) {
            size_t input_bytes = bytes - offset;
            if (input_bytes > KWS_CAPTURE_B64_INPUT_BYTES) {
                input_bytes = KWS_CAPTURE_B64_INPUT_BYTES;
            }

            size_t encoded_bytes = 0;
            int ret = mbedtls_base64_encode(
                encoded, sizeof(encoded), &encoded_bytes,
                s_capture_buffer + offset, input_bytes);
            if (ret != 0) {
                ESP_LOGE(TAG, "Base64 encode failed at offset=%u: -0x%04x",
                         (unsigned)offset, (unsigned)-ret);
                export_ok = false;
                break;
            }
            encoded[encoded_bytes] = '\0';
            printf("KWS_STEREO_B64 %05lu %s\n",
                   (unsigned long)chunk_index, (char *)encoded);
            fflush(stdout);
            offset += input_bytes;
            chunk_index++;
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        printf("KWS_STEREO_B64_END chunks=%lu bytes=%u status=%s\n",
               (unsigned long)chunk_index, (unsigned)offset,
               export_ok ? "ok" : "error");
        fflush(stdout);
        s_capture_state = KWS_CAPTURE_DONE;
        ESP_LOGI(TAG, "Base64 export finished: chunks=%lu status=%s",
                 (unsigned long)chunk_index, export_ok ? "ok" : "error");
    }
}

static void kws_capture_collect(const int16_t *stereo, size_t bytes)
{
    bool prompt_now = false;
    TickType_t now = xTaskGetTickCount();

    portENTER_CRITICAL(&s_capture_lock);
    if (s_capture_state == KWS_CAPTURE_WAIT_PROMPT
        && (s_capture_requested
            || (int32_t)(now - s_capture_prompt_tick) >= 0)) {
        s_capture_fill_bytes = 0;
        s_capture_requested = false;
        s_capture_state = KWS_CAPTURE_RUNNING;
        prompt_now = true;
    }
    portEXIT_CRITICAL(&s_capture_lock);

    if (prompt_now) {
        ESP_LOGW(TAG, "=== SPEAK NOW: say 你好鹰老师 ===");
    }
    if (s_capture_state != KWS_CAPTURE_RUNNING) {
        return;
    }

    size_t remaining = s_capture_capacity_bytes - s_capture_fill_bytes;
    size_t copy_bytes = bytes < remaining ? bytes : remaining;
    memcpy(s_capture_buffer + s_capture_fill_bytes, stereo, copy_bytes);
    s_capture_fill_bytes += copy_bytes;

    if (s_capture_fill_bytes >= s_capture_capacity_bytes) {
        portENTER_CRITICAL(&s_capture_lock);
        s_capture_state = KWS_CAPTURE_READY;
        portEXIT_CRITICAL(&s_capture_lock);
        ESP_LOGI(TAG, "stereo capture buffered; scheduling low-priority export");
        xTaskNotifyGive(s_capture_task);
    }
}

static void kws_command_task(void *arg)
{
    (void)arg;
    char command[24];
    size_t length = 0;

    while (true) {
        int ch = getchar();
        if (ch == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            command[length] = '\0';
            if (strcmp(command, "CAPTURE") == 0) {
                kws_capture_request();
            }
            length = 0;
            continue;
        }
        if (length + 1 < sizeof(command)) {
            command[length++] = (char)ch;
        } else {
            length = 0;
        }
    }
}
#endif

static void kws_audio_task(void *arg)
{
    (void)arg;
    kws_pcm_frame_t frame;
#if CONFIG_KWS_STEREO_DIAGNOSTICS
    int16_t stereo_frame[ES8311_STEREO_FRAME_BYTES / sizeof(int16_t)];
#endif

    while (true) {
        size_t bytes_read = 0;
#if CONFIG_KWS_STEREO_DIAGNOSTICS
        esp_err_t ret = es8311_read_stereo_pcm(
            stereo_frame, sizeof(stereo_frame), &bytes_read);
        if (ret == ESP_OK && bytes_read == sizeof(stereo_frame)) {
            kws_capture_collect(stereo_frame, bytes_read);

            const size_t mono_sample_count =
                ES8311_PCM_FRAME_BYTES / sizeof(int16_t);
            int16_t *mono = (int16_t *)frame.data;
            for (size_t sample = 0; sample < mono_sample_count; ++sample) {
                mono[sample] =
                    stereo_frame[sample * ES8311_STEREO_CHANNELS];
            }
            bytes_read = sizeof(frame.data);
        }
#else
        esp_err_t ret =
            es8311_read_pcm(frame.data, sizeof(frame.data), &bytes_read);
#endif

        if (ret == ESP_OK && bytes_read == sizeof(frame.data)) {
            kws_voiceprint_collect_audio(frame.data, bytes_read);
            if ((xEventGroupGetBits(s_state_bits) & KWS_WS_STREAMING_BIT) == 0) {
                continue;
            }
            frame.length = sizeof(frame.data);
            frame.sequence = ++s_pcm_captured_frames;
            if (xQueueSend(s_pcm_queue, &frame, 0) != pdTRUE) {
                kws_pcm_frame_t discarded;
                s_pcm_dropped_frames++;
                if (xQueueReceive(s_pcm_queue, &discarded, 0) == pdTRUE) {
                    xQueueSend(s_pcm_queue, &frame, 0);
                }
                if (s_pcm_dropped_frames == 1
                    || (s_pcm_dropped_frames % 50) == 0) {
                    ESP_LOGW(TAG, "PCM queue overflow: dropped=%lu backlog=%u",
                             (unsigned long)s_pcm_dropped_frames,
                             (unsigned)uxQueueMessagesWaiting(s_pcm_queue));
                }
            }
        } else {
            ESP_LOGW(TAG, "ES8311 read failed: %s bytes=%u",
                     esp_err_to_name(ret), (unsigned)bytes_read);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void kws_task(void *arg)
{
    (void)arg;
    TickType_t last_enroll_prompt_tick = 0;

    while (true) {
        kws_process_voiceprint_events();
        EventBits_t bits = xEventGroupWaitBits(s_state_bits, KWS_WIFI_CONNECTED_BIT,
                                               pdFALSE, pdTRUE,
                                               pdMS_TO_TICKS(KWS_WIFI_WAIT_LOG_MS));
        if ((bits & KWS_WIFI_CONNECTED_BIT) == 0) {
            ESP_LOGW(TAG, "waiting for Wi-Fi before starting KWS");
            continue;
        }

        bits = xEventGroupGetBits(s_state_bits);
        if (!s_voiceprint_enrolled
            || (bits & (KWS_ENROLL_RECORDING_BIT
                        | KWS_VOICEPRINT_BUSY_BIT)) != 0) {
            TickType_t now = xTaskGetTickCount();
            if (!s_voiceprint_enrolled
                && (last_enroll_prompt_tick == 0
                    || (now - last_enroll_prompt_tick)
                           >= pdMS_TO_TICKS(KWS_WIFI_WAIT_LOG_MS))) {
                ESP_LOGW(TAG,
                         "voiceprint not enrolled: hold GPIO%d, speak for at least %d seconds, then release",
                         BOARD_ENROLL_BUTTON_PIN,
                         KWS_VOICEPRINT_ENROLL_MIN_SECONDS);
                last_enroll_prompt_tick = now;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        esp_err_t ret = kws_websocket_open();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "KWS WebSocket start failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(CONFIG_KWS_RECONNECT_DELAY_MS));
            continue;
        }

        xQueueReset(s_pcm_queue);
        bool session_ok = true;
        bool stream_started = false;
        uint32_t sent_source_frames = 0;
        uint32_t sequence_gaps = 0;
        uint32_t last_sequence = 0;
        uint32_t dropped_at_stream_start = 0;
        uint32_t unhealthy_checks = 0;
        uint32_t batch_frames = 0;
        size_t batch_bytes = 0;
        TickType_t stream_start_tick = 0;
        TickType_t last_health_log_tick = 0;
        while (session_ok
               && (xEventGroupGetBits(s_state_bits) & KWS_WIFI_CONNECTED_BIT) != 0) {
            kws_process_voiceprint_events();
            bits = xEventGroupGetBits(s_state_bits);
            if (!s_voiceprint_enrolled
                || (bits & (KWS_ENROLL_RECORDING_BIT
                            | KWS_VOICEPRINT_BUSY_BIT)) != 0) {
                session_ok = false;
                break;
            }

            session_ok = kws_process_ws_events();
            if (!session_ok) {
                break;
            }

            if ((xEventGroupGetBits(s_state_bits) & KWS_WS_STREAMING_BIT) == 0) {
                stream_started = false;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            if (!stream_started) {
                xQueueReset(s_pcm_queue);
                stream_started = true;
                sent_source_frames = 0;
                sequence_gaps = 0;
                last_sequence = 0;
                dropped_at_stream_start = s_pcm_dropped_frames;
                unhealthy_checks = 0;
                batch_frames = 0;
                batch_bytes = 0;
                stream_start_tick = xTaskGetTickCount();
                last_health_log_tick = stream_start_tick;
                ESP_LOGI(TAG,
                         "PCM upload active: capture=20ms queue=2000ms "
                         "upload=200ms/6400B");
            }

            kws_pcm_frame_t frame;
            if (xQueueReceive(s_pcm_queue, &frame,
                              pdMS_TO_TICKS(ES8311_PCM_FRAME_MS * 2))
                == pdTRUE) {
                if (last_sequence != 0
                    && frame.sequence != last_sequence + 1) {
                    sequence_gaps += frame.sequence - last_sequence - 1;
                }
                last_sequence = frame.sequence;
                if (frame.length != ES8311_PCM_FRAME_BYTES
                    || batch_bytes + frame.length > sizeof(s_upload_batch)) {
                    ESP_LOGE(TAG, "invalid PCM frame: length=%u batch=%u",
                             (unsigned)frame.length, (unsigned)batch_bytes);
                    session_ok = false;
                } else {
                    memcpy(s_upload_batch + batch_bytes, frame.data, frame.length);
                    batch_bytes += frame.length;
                    batch_frames++;
                    if (batch_frames == KWS_UPLOAD_BATCH_FRAMES) {
                        session_ok =
                            kws_send_pcm_frame(s_upload_batch, batch_bytes);
                        if (session_ok) {
                            sent_source_frames += batch_frames;
                        }
                        batch_frames = 0;
                        batch_bytes = 0;
                    }
                }
            }

            TickType_t now = xTaskGetTickCount();
            if (stream_started
                && (now - last_health_log_tick) >= pdMS_TO_TICKS(KWS_STREAM_HEALTH_LOG_MS)) {
                uint32_t wall_ms =
                    (uint32_t)(now - stream_start_tick) * portTICK_PERIOD_MS;
                uint32_t audio_ms =
                    sent_source_frames * ES8311_PCM_FRAME_MS;
                uint32_t realtime_pct =
                    wall_ms > 0 ? (audio_ms * 100U) / wall_ms : 0;
                uint32_t session_drops =
                    s_pcm_dropped_frames - dropped_at_stream_start;
                UBaseType_t backlog = uxQueueMessagesWaiting(s_pcm_queue);
                ESP_LOGI(TAG,
                         "PCM stream health: audio=%lu.%02lus wall=%lu.%02lus "
                         "realtime=%lu%% backlog=%u dropped=%lu gaps=%lu",
                         (unsigned long)(audio_ms / 1000U),
                         (unsigned long)((audio_ms % 1000U) / 10U),
                         (unsigned long)(wall_ms / 1000U),
                         (unsigned long)((wall_ms % 1000U) / 10U),
                         (unsigned long)realtime_pct,
                         (unsigned)backlog,
                         (unsigned long)session_drops,
                         (unsigned long)sequence_gaps);
                last_health_log_tick = now;

                bool continuity_broken =
                    session_drops > 0 || sequence_gaps > 0;
                bool critically_backed_up =
                    backlog >= KWS_PCM_BACKLOG_MAX_FRAMES;
                bool past_grace = wall_ms >= KWS_STREAM_HEALTH_GRACE_MS;
                bool lagging = past_grace
                               && (realtime_pct < 90
                                   || backlog >= KWS_PCM_BACKLOG_WARN_FRAMES);

                if (continuity_broken) {
                    ESP_LOGW(TAG,
                             "restarting unhealthy PCM stream: realtime=%lu%% "
                             "backlog=%u dropped=%lu gaps=%lu",
                             (unsigned long)realtime_pct,
                             (unsigned)backlog,
                             (unsigned long)session_drops,
                             (unsigned long)sequence_gaps);
                    xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
                    session_ok = false;
                } else if (critically_backed_up || lagging) {
                    unhealthy_checks++;
                    if (unhealthy_checks >= KWS_STREAM_UNHEALTHY_CHECKS) {
                        ESP_LOGW(TAG,
                                 "PCM stream remains slow for %u checks; "
                                 "rebuilding WebSocket",
                                 KWS_STREAM_UNHEALTHY_CHECKS);
                        xEventGroupClearBits(s_state_bits, KWS_WS_STREAMING_BIT);
                        session_ok = false;
                    }
                } else {
                    unhealthy_checks = 0;
                }
            }
        }

        xQueueReset(s_pcm_queue);
        kws_websocket_close();
        bits = xEventGroupGetBits(s_state_bits);
        if ((bits & (KWS_ENROLL_RECORDING_BIT
                    | KWS_VOICEPRINT_BUSY_BIT)) != 0
            || !s_voiceprint_enrolled) {
            vTaskDelay(pdMS_TO_TICKS(20));
        } else {
            ESP_LOGI(TAG, "restarting KWS session in %d ms",
                     CONFIG_KWS_RECONNECT_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(CONFIG_KWS_RECONNECT_DELAY_MS));
        }
    }
}

static void kws_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;
        xEventGroupClearBits(s_state_bits, KWS_WIFI_CONNECTED_BIT | KWS_WS_STREAMING_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%d; reconnecting",
                 event ? event->reason : -1);
        esp_wifi_connect();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi connected, IP=" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_state_bits, KWS_WIFI_CONNECTED_BIT);
    }
}

static esp_err_t kws_wifi_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    if (!esp_netif_create_default_wifi_sta()) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&init_config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              kws_wifi_event_handler, NULL,
                                              &s_wifi_event_instance);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              kws_wifi_event_handler, NULL,
                                              &s_ip_event_instance);
    if (ret != ESP_OK) {
        return ret;
    }

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_KWS_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_KWS_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode =
        CONFIG_KWS_WIFI_PASSWORD[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret == ESP_OK) {
        ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }
    if (ret == ESP_OK) {
        ret = esp_wifi_set_ps(WIFI_PS_NONE);
    }
    if (ret == ESP_OK) {
        ret = esp_wifi_start();
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi station started: SSID=%s", CONFIG_KWS_WIFI_SSID);
    }
    return ret;
}

esp_err_t kws_wakeup_start(void)
{
    if (s_kws_task) {
        return ESP_OK;
    }

    s_state_bits = xEventGroupCreate();
    s_ws_event_queue = xQueueCreate(KWS_WS_EVENT_QUEUE_SIZE, sizeof(kws_ws_event_t));
    s_wakeup_event_queue = xQueueCreate(1, sizeof(kws_wakeup_event_t));
    s_pcm_queue = xQueueCreate(KWS_PCM_QUEUE_SIZE, sizeof(kws_pcm_frame_t));
    s_voiceprint_request_queue =
        xQueueCreate(KWS_VOICEPRINT_QUEUE_SIZE,
                     sizeof(kws_voiceprint_request_t));
    s_voiceprint_event_queue =
        xQueueCreate(KWS_VOICEPRINT_QUEUE_SIZE,
                     sizeof(kws_voiceprint_event_t));
    s_recent_pcm_mutex = xSemaphoreCreateMutex();
    if (!s_state_bits || !s_ws_event_queue || !s_wakeup_event_queue
        || !s_pcm_queue || !s_voiceprint_request_queue
        || !s_voiceprint_event_queue || !s_recent_pcm_mutex) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = kws_wifi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = voiceprint_auth_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "voiceprint auth init failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }
    s_voiceprint_enrolled = voiceprint_auth_is_enrolled();

    gpio_config_t enroll_button_config = {
        .pin_bit_mask = 1ULL << BOARD_ENROLL_BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&enroll_button_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "voiceprint enroll button init failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }
    s_enroll_button_stable_level =
        gpio_get_level(BOARD_ENROLL_BUTTON_PIN);
    s_enroll_button_candidate_level =
        s_enroll_button_stable_level;
    s_enroll_button_candidate_frames = KWS_BUTTON_DEBOUNCE_FRAMES;

    s_recent_pcm =
        heap_caps_malloc(KWS_VOICEPRINT_VERIFY_BYTES,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_enroll_pcm =
        heap_caps_malloc(KWS_VOICEPRINT_ENROLL_MAX_BYTES,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_recent_pcm || !s_enroll_pcm) {
        ESP_LOGE(TAG,
                 "voiceprint PSRAM alloc failed: recent=%u enroll=%u bytes",
                 (unsigned)KWS_VOICEPRINT_VERIFY_BYTES,
                 (unsigned)KWS_VOICEPRINT_ENROLL_MAX_BYTES);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "voiceprint buffers ready in PSRAM: recent=%u KiB enroll=%u KiB",
             (unsigned)(KWS_VOICEPRINT_VERIFY_BYTES / 1024),
             (unsigned)(KWS_VOICEPRINT_ENROLL_MAX_BYTES / 1024));
    ESP_LOGI(TAG,
             "voiceprint enroll button: GPIO%d active=%d range=%d-%d seconds speaker=%s",
             BOARD_ENROLL_BUTTON_PIN, BOARD_BUTTON_ACTIVE_LEVEL,
             KWS_VOICEPRINT_ENROLL_MIN_SECONDS,
             KWS_VOICEPRINT_ENROLL_MAX_SECONDS,
             voiceprint_auth_speaker_id());
    if (s_voiceprint_enrolled) {
        ESP_LOGI(TAG, "voiceprint enrollment marker loaded; dual-auth KWS enabled");
    } else {
        ESP_LOGW(TAG,
                 "=== VOICEPRINT REGISTRATION REQUIRED: hold GPIO%d and speak, then release ===",
                 BOARD_ENROLL_BUTTON_PIN);
    }

    ret = es8311_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "live ES8311 capture enabled");

#if CONFIG_KWS_STEREO_DIAGNOSTICS
    s_capture_capacity_bytes =
        (size_t)CONFIG_KWS_STEREO_CAPTURE_SECONDS
        * ES8311_SAMPLE_RATE_HZ
        * ES8311_STEREO_CHANNELS
        * (ES8311_BITS_PER_SAMPLE / 8);
    s_capture_buffer =
        heap_caps_malloc(s_capture_capacity_bytes,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_capture_buffer) {
        ESP_LOGE(TAG, "stereo capture alloc failed (%u KiB)",
                 (unsigned)(s_capture_capacity_bytes / 1024));
        return ESP_ERR_NO_MEM;
    }
    s_capture_state = KWS_CAPTURE_IDLE;
    ESP_LOGI(TAG, "stereo capture buffer ready in PSRAM: %u KiB",
             (unsigned)(s_capture_capacity_bytes / 1024));

    if (xTaskCreate(kws_capture_export_task, "kws_capture",
                    KWS_CAPTURE_TASK_STACK_SIZE, NULL,
                    KWS_CAPTURE_TASK_PRIORITY, &s_capture_task) != pdPASS) {
        s_capture_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(kws_command_task, "kws_command",
                    KWS_COMMAND_TASK_STACK_SIZE, NULL,
                    KWS_COMMAND_TASK_PRIORITY, &s_command_task) != pdPASS) {
        s_command_task = NULL;
        return ESP_ERR_NO_MEM;
    }
#endif

    if (xTaskCreate(kws_voiceprint_worker_task, "voiceprint",
                    KWS_VOICEPRINT_TASK_STACK_SIZE, NULL,
                    KWS_VOICEPRINT_TASK_PRIORITY,
                    &s_voiceprint_task) != pdPASS) {
        s_voiceprint_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(kws_audio_task, "kws_audio", KWS_AUDIO_TASK_STACK_SIZE, NULL,
                    KWS_AUDIO_TASK_PRIORITY, &s_audio_task) != pdPASS) {
        s_audio_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(kws_task, "kws_wakeup", KWS_TASK_STACK_SIZE, NULL,
                    KWS_TASK_PRIORITY, &s_kws_task) != pdPASS) {
        s_kws_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "KWS wakeup task started");
    return ESP_OK;
}

esp_err_t kws_wakeup_get_event(kws_wakeup_event_t *event, uint32_t timeout_ms)
{
    if (!event) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_wakeup_event_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueReceive(s_wakeup_event_queue, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE
               ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool kws_wakeup_is_listening(void)
{
    return s_state_bits
           && (xEventGroupGetBits(s_state_bits) & KWS_WS_STREAMING_BIT) != 0;
}
