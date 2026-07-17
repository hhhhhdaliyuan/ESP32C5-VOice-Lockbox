/**
 * @file servo.h
 * @brief SG90 舵机驱动 — 支持慢速开盒/闭盒
 *
 * 使用 ESP-IDF LEDC 产生 50Hz PWM 控制舵机角度。
 * 提供两级 API：
 *   - 底层：servo_set_angle() 直接设置角度
 *   - 上层：servo_open() / servo_close() 慢速旋转（步进延迟控制速度）
 *
 * 当前声纹解锁未就绪时，servo_open() 可被外部事件（按键、关键词）直接调用；
 * 未来声纹验证成功后，只需从声纹模块调用 servo_open() 即可触发开盒。
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 舵机状态：闭合 / 开启 */
typedef enum {
    SERVO_STATE_CLOSED = 0,  /**< 盒子闭合（0°） */
    SERVO_STATE_OPENED = 1,  /**< 盒子打开（150°） */
} servo_state_t;

/**
 * @brief 初始化舵机
 *
 * 配置指定 GPIO 为 LEDC PWM 输出（50 Hz），并记录 0° 和 180° 对应的脉宽。
 * SG90 典型脉宽为 500~2500 µs，可在初始化时根据实际舵机校准。
 *
 * @param gpio_pin        PWM 输出 GPIO 编号
 * @param min_pulse_us    0° 对应的脉宽（微秒），SG90 通常为 500
 * @param max_pulse_us    180° 对应的脉宽（微秒），SG90 通常为 2500
 * @return
 *   - ESP_OK      初始化成功
 *   - ESP_ERR_INVALID_ARG  参数错误
 *   - ESP_FAIL     LEDC 配置失败
 */
esp_err_t servo_init(int gpio_pin, int min_pulse_us, int max_pulse_us);

/**
 * @brief 立即设置舵机角度
 *
 * @param angle  目标角度，范围 0~180°
 */
void servo_set_angle(int angle);

/**
 * @brief 慢速开盒 — 从当前角度旋转到 150°
 *
 * 每 1° 步进一次，步进间延迟 step_delay_ms 毫秒，延迟越大旋转越慢。
 * 建议值：15~30 ms，对应 150° 旋转耗时约 1.35~2.7 秒。
 *
 * @param step_delay_ms  每度步进延迟（毫秒），必须 > 0
 */
void servo_open(int step_delay_ms);

/**
 * @brief 慢速闭盒 — 从当前角度旋转到 0°
 *
 * 同上，步进延迟控制速度。
 *
 * @param step_delay_ms  每度步进延迟（毫秒），必须 > 0
 */
void servo_close(int step_delay_ms);

/**
 * @brief 获取当前舵机状态
 *
 * @return  SERVO_STATE_CLOSED 或 SERVO_STATE_OPENED
 */
servo_state_t servo_get_state(void);

/**
 * @brief 获取当前舵机角度
 *
 * @return 当前角度值 0~180
 */
int servo_get_angle(void);

#ifdef __cplusplus
}
#endif
