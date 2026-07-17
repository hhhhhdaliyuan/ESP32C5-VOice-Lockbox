#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "servo.h"               /* components/servo 自动被 IDF 发现 */
#include "es8311.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_ERROR_CHECK(es8311_init());
    ESP_LOGI(TAG, "ES8311 ready for KWS PCM capture");
}
