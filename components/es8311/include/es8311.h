#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ES8311_SAMPLE_RATE_HZ          16000
#define ES8311_BITS_PER_SAMPLE         16
#define ES8311_PCM_CHANNELS            1
#define ES8311_PCM_FRAME_MS            20
#define ES8311_PCM_FRAME_BYTES         (ES8311_SAMPLE_RATE_HZ * ES8311_PCM_FRAME_MS / 1000 * \
                                        (ES8311_BITS_PER_SAMPLE / 8))

/**
 * @brief 初始化 I2S、I2C 与 ES8311 Codec
 *
 * 重复调用不会重复创建硬件资源。
 *
 * @return esp_err_t ESP_OK 成功；其他值为 I2S、I2C 或 Codec 初始化错误
 */
esp_err_t es8311_init(void);

/**
 * @brief 读取一帧单声道 PCM
 *
 * PCM 格式为 16 kHz、16-bit little-endian、20 ms，固定 640 B。
 *
 * @param buffer 接收缓冲区，大小至少为 ES8311_PCM_FRAME_BYTES
 * @param buffer_size 接收缓冲区大小，单位为字节
 * @param bytes_read 实际读取字节数输出，成功时固定为 640
 * @return esp_err_t ESP_OK 成功；其他值为参数、状态或 I2S 读取错误
 */
esp_err_t es8311_read_pcm(void *buffer, size_t buffer_size, size_t *bytes_read);

/**
 * @brief 播放一帧单声道 PCM
 *
 * 输入格式为 16 kHz、16-bit little-endian、20 ms、640 B。
 * 实现会将单声道样本复制到左右 I2S slot。
 *
 * @param buffer 待播放 PCM 缓冲区，大小至少为 ES8311_PCM_FRAME_BYTES
 * @param buffer_size 待播放 PCM 数据大小，单位为字节
 * @return esp_err_t ESP_OK 成功；其他值为参数、状态或 I2S 写入错误
 */
esp_err_t es8311_write_pcm(const void *buffer, size_t buffer_size);

/**
 * @brief 释放 Codec、I2C 与 I2S 资源
 *
 * @return esp_err_t ESP_OK 成功；其他值为 I2S 通道释放错误
 */
esp_err_t es8311_deinit(void);

/**
 * @brief 执行 ES8311 录放自检
 *
 * ES8311_SELF_TEST_ENABLE 为 1 时循环执行“录制 2 秒 -> 回放 2 秒”；
 * 为 0 时不分配缓冲区，直接返回 ESP_OK。
 *
 * @return esp_err_t ESP_OK 成功；其他值为初始化状态、内存或 I2S 读写错误
 */
esp_err_t es8311_run_self_test(void);

#ifdef __cplusplus
}
#endif
