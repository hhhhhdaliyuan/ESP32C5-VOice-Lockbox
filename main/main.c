#include "esp_err.h"
#include "es8311.h"
#include "gc9a01.h"
#include "led_control.h"
#include "servo.h"

#define SERVO_GPIO  6       /* MG90S 信号线接 GPIO6 */

void app_main(void)
{
    ESP_ERROR_CHECK(gc9a01_init());
    ESP_ERROR_CHECK(servo_init(SERVO_GPIO, 500, 2500));
    ESP_ERROR_CHECK(es8311_init());
    ESP_ERROR_CHECK(led_control_init());
}
