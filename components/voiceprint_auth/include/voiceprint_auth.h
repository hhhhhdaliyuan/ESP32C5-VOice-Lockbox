#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VOICEPRINT_AUTH_SPEAKER_ID_MAX_BYTES 48

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

/**
 * @brief Load the local enrollment marker from NVS.
 *
 * nvs_flash_init() must have completed before this function is called.
 */
esp_err_t voiceprint_auth_init(void);

bool voiceprint_auth_is_enrolled(void);
const char *voiceprint_auth_speaker_id(void);

esp_err_t voiceprint_auth_set_enrolled(bool enrolled);

esp_err_t voiceprint_auth_enroll(const uint8_t *pcm, size_t pcm_len,
                                 voiceprint_enroll_result_t *result);

esp_err_t voiceprint_auth_verify(const uint8_t *pcm, size_t pcm_len,
                                 voiceprint_verify_result_t *result);

#ifdef __cplusplus
}
#endif
