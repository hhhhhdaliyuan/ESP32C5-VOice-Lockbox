#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/**
 * @brief I2S 标准模式硬件参数
 */
typedef struct {
    i2s_port_t port;
    gpio_num_t mclk_gpio;
    gpio_num_t bclk_gpio;
    gpio_num_t ws_gpio;
    gpio_num_t dout_gpio;
    gpio_num_t din_gpio;
    uint32_t sample_rate_hz;
    i2s_data_bit_width_t data_bit_width;
    uint32_t mclk_multiple;
} audio_i2s_config_t;

/**
 * @brief 创建并启用同一 I2S 端口的 TX/RX 通道
 *
 * @param config I2S 端口、GPIO 和音频格式配置
 * @return esp_err_t ESP_OK 成功；ESP_ERR_INVALID_ARG 参数无效；其他值为驱动错误
 */
esp_err_t audio_i2s_init(const audio_i2s_config_t *config);

/**
 * @brief 读取原始 I2S 数据
 *
 * 不改变声道数、采样位宽或样本内容；超时单位为 FreeRTOS tick。
 *
 * @param buffer 接收缓冲区
 * @param buffer_size 接收缓冲区大小，单位为字节
 * @param bytes_read 实际读取字节数输出，可为 NULL
 * @param timeout 读取超时时间
 * @return esp_err_t ESP_OK 成功；其他值为参数、状态或底层 I2S 错误
 */
esp_err_t audio_i2s_read(void *buffer, size_t buffer_size, size_t *bytes_read, TickType_t timeout);

/**
 * @brief 写入原始 I2S 数据
 *
 * 不改变声道数、采样位宽或样本内容；超时单位为 FreeRTOS tick。
 *
 * @param buffer 发送缓冲区
 * @param buffer_size 发送缓冲区大小，单位为字节
 * @param bytes_written 实际写入字节数输出，可为 NULL
 * @param timeout 写入超时时间
 * @return esp_err_t ESP_OK 成功；其他值为参数、状态或底层 I2S 错误
 */
esp_err_t audio_i2s_write(const void *buffer, size_t buffer_size, size_t *bytes_written, TickType_t timeout);

/**
 * @brief 获取已初始化的 TX/RX 通道句柄
 *
 * @param tx_handle TX 通道句柄输出
 * @param rx_handle RX 通道句柄输出
 * @return esp_err_t ESP_OK 成功；ESP_ERR_INVALID_STATE 表示 I2S 尚未初始化
 */
esp_err_t audio_i2s_get_handles(i2s_chan_handle_t *tx_handle, i2s_chan_handle_t *rx_handle);

/**
 * @brief 关闭并释放 TX/RX 通道
 *
 * @return esp_err_t ESP_OK 成功；其他值为通道删除错误
 */
esp_err_t audio_i2s_deinit(void);
