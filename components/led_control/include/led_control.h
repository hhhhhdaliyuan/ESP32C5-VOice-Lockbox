/**
 * @file led_control.h
 * @brief 三色 LED 控制驱动 — 支持多种灯光模式
 *
 * 红 LED → GPIO3，绿 LED → GPIO16，黄 LED → GPIO18
 * 所有 LED 另一端接 GND（GPIO 高电平点亮）
 *
 * 提供模式切换 API，内部 FreeRTOS 任务自动执行对应闪烁/循环效果。
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** LED 灯光模式 */
typedef enum {
    LED_PATTERN_OFF = 0,            /**< 全部关闭 */
    LED_PATTERN_IDLE,               /**< 待机 — 绿灯常亮 */
    LED_PATTERN_UNLOCK_SUCCESS,     /**< 解锁成功 — 绿灯闪烁 3 次后常亮 */
    LED_PATTERN_UNLOCK_FAIL,        /**< 解锁失败 — 红灯快速闪烁 */
    LED_PATTERN_OPENING,            /**< 开盒过程 — 多色循环（红→绿→黄→...） */
    LED_PATTERN_CLOSED,             /**< 闭盒完毕 — 绿 + 黄同时闪烁 */
} led_pattern_t;

/**
 * @brief 初始化三个 LED GPIO 并创建控制任务
 *
 * @return
 *   - ESP_OK      初始化成功
 *   - ESP_FAIL    任务创建失败
 */
esp_err_t led_control_init(void);

/**
 * @brief 切换灯光模式
 *
 * 模式切换即时生效。内部任务会持续执行当前模式的闪烁/循环逻辑，
 * 直到下一次 set_pattern() 被调用。
 *
 * @param pattern  目标灯光模式
 */
void led_control_set_pattern(led_pattern_t pattern);

/**
 * @brief 获取当前灯光模式
 *
 * @return 当前模式
 */
led_pattern_t led_control_get_pattern(void);

#ifdef __cplusplus
}
#endif
