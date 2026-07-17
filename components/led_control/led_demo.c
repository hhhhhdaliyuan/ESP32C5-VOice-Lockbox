/**
 * @file led_demo.c
 * @brief LED 灯光演示实现 — 各场景编排
 *
 * 演示流程（上电后自动循环）：
 *   1. 待机 — 绿灯常亮
 *   2. 开盒 — 多色循环 + 舵机旋转
 *   3. 解锁成功 — 绿灯闪烁 3 次后常亮
 *   4. 闭盒 — 多色循环 + 舵机回转
 *   5. 闭盒完毕 — 绿黄同时闪烁
 *   6. 解锁失败 — 红灯快闪
 */

#include "led_demo.h"

#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_control.h"
#include "servo.h"

static const char *TAG = "led_demo";

/* 演示各阶段延时（毫秒） */
#define DEMO_IDLE_MS        3000
#define DEMO_UNLOCK_MS      2000
#define DEMO_CLOSED_MS      3000

static void demo_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));  /* 等外设就绪 */

    ESP_LOGI(TAG, "=== 演示开始 ===");

    while (1) {
        /* ---- 1. 待机：绿灯常亮 ---- */
        ESP_LOGI(TAG, "阶段1 — 待机（绿灯常亮）");
        led_control_set_pattern(LED_PATTERN_IDLE);
        vTaskDelay(pdMS_TO_TICKS(DEMO_IDLE_MS));

        /* ---- 2. 开盒：多色循环 + 舵机打开 ---- */
        ESP_LOGI(TAG, "阶段2 — 开盒（多色循环 + 舵机旋转）");
        led_control_set_pattern(LED_PATTERN_OPENING);
        servo_open(20);   /* 舵机从当前角度步进到 150° */
        /* 开盒后继续保持多色循环一会儿 */
        vTaskDelay(pdMS_TO_TICKS(1000));

        /* ---- 3. 解锁成功：绿灯闪烁 3 次后常亮 ---- */
        ESP_LOGI(TAG, "阶段3 — 解锁成功（绿灯闪烁）");
        led_control_set_pattern(LED_PATTERN_UNLOCK_SUCCESS);
        vTaskDelay(pdMS_TO_TICKS(DEMO_UNLOCK_MS));

        /* ---- 4. 闭盒：多色循环 + 舵机关闭 ---- */
        ESP_LOGI(TAG, "阶段4 — 闭盒（多色循环 + 舵机回转）");
        led_control_set_pattern(LED_PATTERN_OPENING);
        servo_close(20);  /* 舵机从当前角度步进到 0° */
        vTaskDelay(pdMS_TO_TICKS(500));

        /* ---- 5. 闭盒完毕：绿黄同时闪烁 ---- */
        ESP_LOGI(TAG, "阶段5 — 闭盒完毕（绿黄闪烁）");
        led_control_set_pattern(LED_PATTERN_CLOSED);
        vTaskDelay(pdMS_TO_TICKS(DEMO_CLOSED_MS));

        /* ---- 6. 解锁失败：红灯快闪 ---- */
        ESP_LOGI(TAG, "阶段6 — 解锁失败（红灯快闪）");
        led_control_set_pattern(LED_PATTERN_UNLOCK_FAIL);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ---------- 公开 API ---------- */

esp_err_t led_demo_start(void)
{
    BaseType_t ret = xTaskCreate(demo_task, "led_demo", 4096, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LED demo started");
    return ESP_OK;
}
