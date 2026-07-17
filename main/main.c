/**
 * @file main.c
 * @brief 宝盒主程序入口 — 初始化各模块并启动演示
 *
 * 硬件连接：
 *   - 舵机 MG90S  信号线 → GPIO6
 *   - 红  LED               → GPIO3 (另一端接 GND)
 *   - 绿  LED               → GPIO16 (另一端接 GND)
 *   - 黄  LED               → GPIO18 (另一端接 GND)
 */

#include <stdio.h>
#include "esp_log.h"
#include "servo.h"
#include "driver/ledc.h"
#include "servo.h"               /* components/servo 自动被 IDF 发现 */
#include "es8311.h"
#include "led_control.h"
#include "led_demo.h"

#define SERVO_GPIO  6       /* MG90S 信号线接 GPIO6 */

#define SERVO_GPIO  6       /* MG90S 信号线接 GPIO6 */

void app_main(void)
{
    servo_init(SERVO_GPIO, 500, 2500);  //初始化舵机，SG90 典型脉宽 500~2500 µs
    ESP_ERROR_CHECK(es8311_init());
    ESP_LOGI(TAG, "ES8311 ready for KWS PCM capture");
    ESP_LOGI(TAG, "宝盒系统启动");

    /* 初始化舵机 */
    esp_err_t ret = servo_init(SERVO_GPIO, 500, 2500);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "servo_init failed: %s", esp_err_to_name(ret));
    }

    /* 初始化 LED 控制器 */
    ret = led_control_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "led_control_init failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "宝盒系统初始化完成");
}
