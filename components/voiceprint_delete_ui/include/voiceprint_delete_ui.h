#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "voiceprint_auth.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t voiceprint_delete_ui_show_loading(void);
esp_err_t voiceprint_delete_ui_show_list(const voiceprint_speaker_t *speakers,
                                         size_t count, size_t selected);
esp_err_t voiceprint_delete_ui_show_confirmation(
    const voiceprint_speaker_t *speaker, bool confirm_selected);
esp_err_t voiceprint_delete_ui_show_result(bool success);
esp_err_t voiceprint_delete_ui_show_error(const char *message);
esp_err_t voiceprint_delete_ui_show_deleting(void);

#ifdef __cplusplus
}
#endif
