#include "voiceprint_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "voiceprint_auth";

#define VOICEPRINT_ENROLL_PATH          "/v1/voiceprint/enroll"
#define VOICEPRINT_VERIFY_PATH          "/v1/voiceprint/verify"
#define VOICEPRINT_RESPONSE_BUFFER_SIZE 4096
#define VOICEPRINT_NVS_NAMESPACE        "voiceprint"
#define VOICEPRINT_NVS_ENROLLED_KEY     "enrolled"

typedef struct {
    char data[VOICEPRINT_RESPONSE_BUFFER_SIZE];
    size_t length;
    bool overflow;
} voiceprint_http_response_t;

static bool s_initialized;
static bool s_enrolled;

static bool voiceprint_url_encode(char *output, size_t output_size,
                                  const char *input)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;

    if (!output || output_size == 0 || !input) {
        return false;
    }

    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        bool unreserved =
            (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')
            || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_'
            || *p == '.' || *p == '~';
        if (unreserved) {
            if (used + 1 >= output_size) {
                return false;
            }
            output[used++] = (char)*p;
        } else {
            if (used + 3 >= output_size) {
                return false;
            }
            output[used++] = '%';
            output[used++] = hex[*p >> 4];
            output[used++] = hex[*p & 0x0f];
        }
    }
    output[used] = '\0';
    return true;
}

static esp_err_t voiceprint_http_event_handler(esp_http_client_event_t *event)
{
    voiceprint_http_response_t *response =
        (voiceprint_http_response_t *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || !response
        || !event->data || event->data_len <= 0) {
        return ESP_OK;
    }

    size_t available = sizeof(response->data) - response->length - 1;
    if ((size_t)event->data_len > available) {
        response->overflow = true;
        return ESP_OK;
    }

    memcpy(response->data + response->length, event->data,
           (size_t)event->data_len);
    response->length += (size_t)event->data_len;
    response->data[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t voiceprint_http_post(const char *url, const uint8_t *pcm,
                                      size_t pcm_len,
                                      voiceprint_http_response_t *response)
{
    if (!url || !pcm || pcm_len == 0 || !response) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(response, 0, sizeof(*response));
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = voiceprint_http_event_handler,
        .user_data = response,
        .timeout_ms = CONFIG_VOICEPRINT_HTTP_TIMEOUT_MS,
        .buffer_size = VOICEPRINT_RESPONSE_BUFFER_SIZE,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type",
                               "application/octet-stream");
    esp_http_client_set_post_field(client, (const char *)pcm, (int)pcm_len);

    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (response->overflow) {
        ESP_LOGE(TAG, "HTTP response exceeds %u bytes",
                 (unsigned)sizeof(response->data));
        return ESP_ERR_INVALID_SIZE;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "HTTP status=%d body=%s", status,
                 response->length ? response->data : "(empty)");
        return ESP_FAIL;
    }
    if (response->length == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t voiceprint_auth_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(VOICEPRINT_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        s_enrolled = false;
        s_initialized = true;
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t enrolled = 0;
    ret = nvs_get_u8(handle, VOICEPRINT_NVS_ENROLLED_KEY, &enrolled);
    nvs_close(handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        s_enrolled = enrolled != 0;
        s_initialized = true;
    }
    return ret;
}

bool voiceprint_auth_is_enrolled(void)
{
    return s_initialized && s_enrolled;
}

const char *voiceprint_auth_speaker_id(void)
{
    return CONFIG_VOICEPRINT_SPEAKER_ID;
}

esp_err_t voiceprint_auth_set_enrolled(bool enrolled)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t ret =
        nvs_open(VOICEPRINT_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_u8(handle, VOICEPRINT_NVS_ENROLLED_KEY,
                     enrolled ? 1 : 0);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ret == ESP_OK) {
        s_enrolled = enrolled;
    }
    return ret;
}

esp_err_t voiceprint_auth_enroll(const uint8_t *pcm, size_t pcm_len,
                                 voiceprint_enroll_result_t *result)
{
    if (!s_initialized || !pcm || pcm_len == 0 || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));

    char encoded_speaker_id[VOICEPRINT_AUTH_SPEAKER_ID_MAX_BYTES * 3];
    char encoded_display_name[sizeof(CONFIG_VOICEPRINT_DISPLAY_NAME) * 3];
    if (!voiceprint_url_encode(encoded_speaker_id,
                               sizeof(encoded_speaker_id),
                               CONFIG_VOICEPRINT_SPEAKER_ID)
        || !voiceprint_url_encode(encoded_display_name,
                                  sizeof(encoded_display_name),
                                  CONFIG_VOICEPRINT_DISPLAY_NAME)) {
        return ESP_ERR_INVALID_ARG;
    }

    char url[384];
    int written = snprintf(
        url, sizeof(url),
        "%s%s?speaker_id=%s&sample_rate=16000&display_name=%s",
        CONFIG_VOICEPRINT_SERVER_URL, VOICEPRINT_ENROLL_PATH,
        encoded_speaker_id, encoded_display_name);
    if (written < 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "enroll upload: speaker=%s pcm=%u bytes",
             CONFIG_VOICEPRINT_SPEAKER_ID, (unsigned)pcm_len);
    voiceprint_http_response_t *response = malloc(sizeof(*response));
    if (!response) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = voiceprint_http_post(url, pcm, pcm_len, response);
    if (ret != ESP_OK) {
        free(response);
        return ret;
    }

    cJSON *root = cJSON_Parse(response->data);
    free(response);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    const cJSON *speaker_id =
        cJSON_GetObjectItemCaseSensitive(root, "speaker_id");
    const cJSON *enrollment_count =
        cJSON_GetObjectItemCaseSensitive(root, "enrollment_count");
    if (!cJSON_IsTrue(ok) || !cJSON_IsString(speaker_id)
        || !speaker_id->valuestring) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    result->ok = true;
    strlcpy(result->speaker_id, speaker_id->valuestring,
            sizeof(result->speaker_id));
    if (cJSON_IsNumber(enrollment_count)
        && enrollment_count->valuedouble >= 0
        && enrollment_count->valuedouble <= UINT8_MAX) {
        result->enrollment_count =
            (uint8_t)enrollment_count->valuedouble;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t voiceprint_auth_verify(const uint8_t *pcm, size_t pcm_len,
                                 voiceprint_verify_result_t *result)
{
    if (!s_initialized || !pcm || pcm_len == 0 || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));

    char encoded_speaker_id[VOICEPRINT_AUTH_SPEAKER_ID_MAX_BYTES * 3];
    if (!voiceprint_url_encode(encoded_speaker_id,
                               sizeof(encoded_speaker_id),
                               CONFIG_VOICEPRINT_SPEAKER_ID)) {
        return ESP_ERR_INVALID_ARG;
    }

    char url[320];
    int written = snprintf(
        url, sizeof(url), "%s%s?sample_rate=16000&speaker_id=%s",
        CONFIG_VOICEPRINT_SERVER_URL, VOICEPRINT_VERIFY_PATH,
        encoded_speaker_id);
    if (written < 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "verify upload: speaker=%s pcm=%u bytes",
             CONFIG_VOICEPRINT_SPEAKER_ID, (unsigned)pcm_len);
    voiceprint_http_response_t *response = malloc(sizeof(*response));
    if (!response) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = voiceprint_http_post(url, pcm, pcm_len, response);
    if (ret != ESP_OK) {
        free(response);
        return ret;
    }

    cJSON *root = cJSON_Parse(response->data);
    free(response);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    const cJSON *matched =
        cJSON_GetObjectItemCaseSensitive(root, "matched");
    const cJSON *speaker_id =
        cJSON_GetObjectItemCaseSensitive(root, "speaker_id");
    const cJSON *score = cJSON_GetObjectItemCaseSensitive(root, "score");
    const cJSON *threshold =
        cJSON_GetObjectItemCaseSensitive(root, "threshold");

    result->ok = cJSON_IsTrue(ok);
    result->matched = cJSON_IsTrue(matched);
    if (cJSON_IsString(speaker_id) && speaker_id->valuestring) {
        strlcpy(result->speaker_id, speaker_id->valuestring,
                sizeof(result->speaker_id));
    }
    if (cJSON_IsNumber(score)) {
        result->score = (float)score->valuedouble;
    }
    if (cJSON_IsNumber(threshold)) {
        result->threshold = (float)threshold->valuedouble;
    }
    cJSON_Delete(root);
    return result->ok ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
