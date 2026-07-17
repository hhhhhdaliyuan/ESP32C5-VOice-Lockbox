/**
 * @file led_demo.h
 * @brief LED 灯光演示/功能编排 — 供 main.c 调用
 *
 * 将灯光模式与舵机动作组合为完整的场景（开盒、闭盒、解锁成功/失败等）。
 * 上层只需调用 led_demo_start() 即可启动全自动演示。
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 LED 灯光演示（与舵机联动）
 *
 * 内部创建 FreeRTOS 任务，按顺序演示各场景：
 *   待机 → 开盒+多色 → 解锁成功 → 闭盒+多色 → 闭盒完毕 → 解锁失败
 *
 * 演示会循环进行。
 *
 * @return
 *   - ESP_OK      任务创建成功
 *   - ESP_FAIL    任务创建失败
 */
esp_err_t led_demo_start(void);

#ifdef __cplusplus
}
#endif
