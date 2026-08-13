#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VOICEPRINT_AUTH_SPEAKER_ID_MAX_BYTES 48
#define VOICEPRINT_AUTH_DISPLAY_NAME_MAX_BYTES 48
#define VOICEPRINT_AUTH_TIMESTAMP_MAX_BYTES 32
#define VOICEPRINT_AUTH_MAX_SPEAKERS 12

typedef struct {
    bool ok;
    char speaker_id[VOICEPRINT_AUTH_SPEAKER_ID_MAX_BYTES];
    uint8_t enrollment_count;
} voiceprint_enroll_result_t;

typedef struct {
    bool ok;
    bool matched;
    char speaker_id[VOICEPRINT_AUTH_SPEAKER_ID_MAX_BYTES];
    float score;
    float threshold;
} voiceprint_verify_result_t;

typedef struct {
    char speaker_id[VOICEPRINT_AUTH_SPEAKER_ID_MAX_BYTES];
    char display_name[VOICEPRINT_AUTH_DISPLAY_NAME_MAX_BYTES];
    char created_at[VOICEPRINT_AUTH_TIMESTAMP_MAX_BYTES];
    uint8_t enrollment_count;
} voiceprint_speaker_t;

/**
 * @brief Load the local enrollment marker from NVS.
 *
 * nvs_flash_init() must have completed before this function is called.
 */
esp_err_t voiceprint_auth_init(void);

bool voiceprint_auth_is_enrolled(void);
const char *voiceprint_auth_speaker_id(void);

esp_err_t voiceprint_auth_set_enrolled(bool enrolled);

/**
 * @brief Load the registered voiceprint list from the service.
 *
 * The caller supplies storage for up to @p capacity entries. The list is
 * truncated to that capacity and @p count reports the number copied.
 */
esp_err_t voiceprint_auth_list_speakers(voiceprint_speaker_t *speakers,
                                        size_t capacity, size_t *count);

/**
 * @brief Delete a registered voiceprint from the service.
 *
 * Deleting this device's configured speaker ID also clears its local NVS
 * enrollment marker, immediately disabling dual-auth wakeups on the device.
 */
esp_err_t voiceprint_auth_delete_speaker(const char *speaker_id);

esp_err_t voiceprint_auth_enroll(const uint8_t *pcm, size_t pcm_len,
                                 voiceprint_enroll_result_t *result);

esp_err_t voiceprint_auth_verify(const uint8_t *pcm, size_t pcm_len,
                                 voiceprint_verify_result_t *result);

#ifdef __cplusplus
}
#endif
