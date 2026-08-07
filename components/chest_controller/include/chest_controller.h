#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化宝盒业务外设并启动唤醒动作任务
 *
 * 当前动作任务在关键词与声纹双重验证成功后慢速打开盒盖。
 * 重复调用不会创建重复任务。
 *
 * @return ESP_OK 成功；其他值为外设、KWS 或任务创建错误
 */
esp_err_t chest_controller_start(void);

#ifdef __cplusplus
}
#endif
