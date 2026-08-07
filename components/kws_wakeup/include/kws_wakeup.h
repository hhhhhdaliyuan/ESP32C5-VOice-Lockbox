#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KWS_WAKEUP_KEYWORD_MAX_BYTES 64
#define KWS_WAKEUP_SPEAKER_ID_MAX_BYTES 48

typedef enum {
    KWS_WAKEUP_KEYWORD_UNKNOWN = 0,
    KWS_WAKEUP_KEYWORD_PANBAO,
    KWS_WAKEUP_KEYWORD_YING_LAOSHI,
} kws_wakeup_keyword_t;

typedef struct {
    kws_wakeup_keyword_t type;
    char keyword[KWS_WAKEUP_KEYWORD_MAX_BYTES];
    float confidence;
    char speaker_id[KWS_WAKEUP_SPEAKER_ID_MAX_BYTES];
    float voiceprint_score;
    float voiceprint_threshold;
} kws_wakeup_event_t;

/**
 * @brief 启动 Wi-Fi、ES8311、KWS WebSocket 和音频上传任务
 *
 * 组件先启动 Wi-Fi PHY，再初始化 ES8311，以降低启动瞬间的电源峰值。
 * 重复调用不会创建重复任务。
 *
 * @return ESP_OK 成功；其他值为资源创建或 Wi-Fi 初始化错误
 */
esp_err_t kws_wakeup_start(void);

/**
 * @brief 等待一次关键词与声纹双重验证通过的唤醒事件
 *
 * KWS 命中后会先验证最近 3 秒声纹，只有已注册用户匹配时才发布。
 * 事件队列只保留最新一次双重验证成功事件。
 *
 * @param event 事件输出
 * @param timeout_ms 等待时间，0 表示不等待
 * @return ESP_OK 收到事件；ESP_ERR_TIMEOUT 超时；其他值为参数或状态错误
 */
esp_err_t kws_wakeup_get_event(kws_wakeup_event_t *event, uint32_t timeout_ms);

/**
 * @brief 查询后端是否已经确认进入音频流式监听状态
 */
bool kws_wakeup_is_listening(void);

#ifdef __cplusplus
}
#endif
