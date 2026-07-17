#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "servo.h"
#include "driver/ledc.h"
#include "servo.h"               /* components/servo 自动被 IDF 发现 */
#include "es8311.h"

#define SERVO_GPIO  6       /* MG90S 信号线接 GPIO6 */

void app_main(void)
{
    servo_init(SERVO_GPIO, 500, 2500);  //初始化舵机，SG90 典型脉宽 500~2500 µs
    ESP_ERROR_CHECK(es8311_init());
    ESP_LOGI(TAG, "ES8311 ready for KWS PCM capture");
}
