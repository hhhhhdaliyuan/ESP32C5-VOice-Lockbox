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

#include "esp_err.h"
#include "es8311.h"
#include "led_control.h"
#include "servo.h"

#define SERVO_GPIO  6       /* MG90S 信号线接 GPIO6 */

void app_main(void)
{
    ESP_ERROR_CHECK(servo_init(SERVO_GPIO, 500, 2500));
    ESP_ERROR_CHECK(es8311_init());
    ESP_ERROR_CHECK(led_control_init());
}
