#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ES8311_SAMPLE_RATE_HZ   16000
#define ES8311_BITS_PER_SAMPLE  16
#define ES8311_PCM_CHANNELS     1
#define ES8311_STEREO_CHANNELS  2
#define ES8311_PCM_FRAME_MS     20
#define ES8311_PCM_FRAME_BYTES  (ES8311_SAMPLE_RATE_HZ * ES8311_PCM_FRAME_MS / 1000 * \
                                 (ES8311_BITS_PER_SAMPLE / 8))
#define ES8311_STEREO_FRAME_BYTES (ES8311_PCM_FRAME_BYTES * ES8311_STEREO_CHANNELS)

/**
 * @brief Initialize SonKey-aligned I2S, I2C, and ES8311 capture.
 */
esp_err_t es8311_init(void);

/**
 * @brief Read one 20 ms frame of 16 kHz, mono, signed 16-bit little-endian PCM.
 */
esp_err_t es8311_read_pcm(void *buffer, size_t buffer_size, size_t *bytes_read);

/**
 * @brief Read one 20 ms frame of interleaved stereo signed 16-bit PCM.
 *
 * Samples are returned as L, R, L, R to preserve the raw I2S slot data.
 */
esp_err_t es8311_read_stereo_pcm(void *buffer, size_t buffer_size,
                                 size_t *bytes_read);

#ifdef __cplusplus
}
#endif
