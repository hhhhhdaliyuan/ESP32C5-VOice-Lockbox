#include "esp_err.h"
#include "esp_log.h"
#include "kws_wakeup.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_ERROR_CHECK(kws_wakeup_start());
    ESP_LOGI(TAG, "live ES8311 KWS + voiceprint service started");

    while (true) {
        kws_wakeup_event_t event;
        esp_err_t ret = kws_wakeup_get_event(&event, 1000);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG,
                     "dual-auth wakeup: %s confidence=%.3f speaker=%s score=%.3f threshold=%.3f",
                     event.keyword, event.confidence, event.speaker_id,
                     event.voiceprint_score, event.voiceprint_threshold);
        } else if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "KWS event wait failed: %s", esp_err_to_name(ret));
        }
    }
}
