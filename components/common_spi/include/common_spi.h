/**
 * @file common_spi.h
 * @brief 可由多个 SPI 设备共享的同步发送接口。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 共享 SPI 总线的硬件配置。
 *
 * 同一 host 的 SCLK、MOSI 和 MISO 只能配置一次；后续初始化可申请不超过已创建总线
 * 上限的最大传输长度。设备仅新增各自的 CS、时钟频率和 SPI Mode。
 */
typedef struct {
    spi_host_device_t host;
    int mosi_io_num;
    int miso_io_num;
    int sclk_io_num;
    size_t max_transfer_bytes;
} common_spi_bus_config_t;

/** @brief 单个 SPI 从设备的配置。 */
typedef struct {
    int cs_io_num;
    int clock_speed_hz;
    uint8_t mode;
    size_t max_transfer_bytes;
} common_spi_device_config_t;

/**
 * @brief 由 common_spi_device_add() 初始化的设备句柄。
 *
 * 调用方可将其作为静态对象保存，但不得直接修改成员。common_spi_write() 会串行化
 * 同一设备的同步传输。
 */
typedef struct {
    spi_device_handle_t handle;
    size_t max_transfer_bytes;
    /** @brief 组件内部用于串行化 common_spi_write() 的设备互斥锁。 */
    SemaphoreHandle_t mutex;
} common_spi_device_t;

/**
 * @brief 初始化一条可共享的 SPI 主机总线。
 *
 * 以相同引脚配置重复调用会直接返回 ESP_OK；后续申请的最大传输长度不得超过已创建
 * 总线的上限。引脚配置不一致时返回 ESP_ERR_INVALID_STATE，避免多个设备对同一组总线
 * 引脚产生冲突。
 *
 * @param config 非空总线配置；MOSI、SCLK 和 max_transfer_bytes 必须有效。
 * @return ESP_OK 成功；ESP_ERR_INVALID_ARG 表示配置无效；ESP_ERR_INVALID_STATE
 *         表示该 host 已按不同引脚配置初始化；ESP_ERR_INVALID_SIZE 表示申请的传输上限
 *         超过已创建总线的上限；其他值为 ESP-IDF SPI 驱动错误码。
 */
esp_err_t common_spi_bus_init(const common_spi_bus_config_t *config);

/**
 * @brief 在已初始化的 SPI 总线上添加一个从设备。
 *
 * 每个设备拥有独立 CS，因此可与同一总线上的其他设备安全复用 SCLK/MOSI/MISO。
 *
 * @param host 已由 common_spi_bus_init() 初始化的 SPI 主机。
 * @param config 非空设备配置；CS、频率、Mode 与单次传输上限由该设备独立决定。
 * @param out_device 非空输出对象，调用前其 handle 和 mutex 必须为空。
 * @return ESP_OK 成功；ESP_ERR_INVALID_ARG 表示参数无效；ESP_ERR_INVALID_STATE
 *         表示总线未初始化、输出对象已被使用或 CS 已被其他设备占用；ESP_ERR_INVALID_SIZE
 *         表示设备传输上限超过总线限制；其他值为 ESP-IDF SPI 驱动错误码。
 */
esp_err_t common_spi_device_add(spi_host_device_t host, const common_spi_device_config_t *config,
                                common_spi_device_t *out_device);

/**
 * @brief 同步发送一段原始 SPI 字节数据。
 *
 * 函数返回时发送已完成，调用方可立即复用 data 缓冲区。同一设备的多个任务可并发调用，
 * 组件会在一次完整 polling 传输期间独占该设备句柄；该函数不可从 ISR 调用。
 *
 * @param device 由 common_spi_device_add() 成功初始化的设备。
 * @param data 非空待发送数据指针。
 * @param len 发送长度；超过硬件单次传输上限时会自动分片发送。
 * @return ESP_OK 成功；ESP_ERR_INVALID_ARG 表示参数无效；ESP_ERR_INVALID_SIZE
 *         表示长度超过设备上限；ESP_ERR_INVALID_STATE 表示设备未初始化；其他值为
 *         ESP-IDF SPI 驱动错误码。
 */
esp_err_t common_spi_write(const common_spi_device_t *device, const void *data, size_t len);

#ifdef __cplusplus
}
#endif
