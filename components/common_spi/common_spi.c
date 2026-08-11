#include "common_spi.h"

#include <stdbool.h>

#include "soc/soc_caps.h"

#define COMMON_SPI_UNUSED_GPIO (-1)

typedef struct {
    bool initialized;
    common_spi_bus_config_t config;
    /* 记录已分配的硬件 CS，防止一个 GPIO 被两个设备重复注册。 */
    bool cs_in_use[SOC_GPIO_PIN_COUNT];
} common_spi_bus_state_t;

/* 每个 SPI host 仅允许一份总线配置，设备通过各自 CS 共用这条总线。 */
static common_spi_bus_state_t s_bus_states[SPI_HOST_MAX];

static bool common_spi_host_valid(spi_host_device_t host)
{
    /* SPI1 连接片内 Flash/PSRAM，SPI 主机模式从 SPI2 起可供应用使用。 */
    return host >= SPI2_HOST && host < SPI_HOST_MAX;
}

static bool common_spi_bus_pins_equal(const common_spi_bus_config_t *left,
                                      const common_spi_bus_config_t *right)
{
    /* max_transfer_bytes 是 DMA 缓冲上限而非物理连线，因此不参与总线兼容性判断。 */
    return left->host == right->host && left->mosi_io_num == right->mosi_io_num &&
           left->miso_io_num == right->miso_io_num && left->sclk_io_num == right->sclk_io_num;
}

esp_err_t common_spi_bus_init(const common_spi_bus_config_t *config)
{
    if (!config || !common_spi_host_valid(config->host) || config->mosi_io_num < 0 ||
        config->sclk_io_num < 0 || config->max_transfer_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    common_spi_bus_state_t *state = &s_bus_states[config->host];
    if (state->initialized) {
        /* 引脚改变会破坏既有设备；已分配的 DMA 缓冲上限可安全服务更小请求。 */
        if (!common_spi_bus_pins_equal(&state->config, config)) {
            return ESP_ERR_INVALID_STATE;
        }
        return config->max_transfer_bytes <= state->config.max_transfer_bytes ? ESP_OK :
                                                                                ESP_ERR_INVALID_SIZE;
    }

    spi_bus_config_t bus_config = {
        .mosi_io_num = config->mosi_io_num,
        .miso_io_num = config->miso_io_num,
        .sclk_io_num = config->sclk_io_num,
        .quadwp_io_num = COMMON_SPI_UNUSED_GPIO,
        .quadhd_io_num = COMMON_SPI_UNUSED_GPIO,
        .data4_io_num = COMMON_SPI_UNUSED_GPIO,
        .data5_io_num = COMMON_SPI_UNUSED_GPIO,
        .data6_io_num = COMMON_SPI_UNUSED_GPIO,
        .data7_io_num = COMMON_SPI_UNUSED_GPIO,
        .max_transfer_sz = config->max_transfer_bytes,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    /*
     * The ESP32-C5 display path uses the hardware FIFO rather than SPI DMA.
     * Transfers larger than the FIFO are split by common_spi_write().
     */
    esp_err_t ret = spi_bus_initialize(config->host, &bus_config, SPI_DMA_DISABLED);
    if (ret != ESP_OK) {
        return ret;
    }

    state->config = *config;
    state->initialized = true;
    return ESP_OK;
}

esp_err_t common_spi_device_add(spi_host_device_t host, const common_spi_device_config_t *config,
                                common_spi_device_t *out_device)
{
    if (!config || !out_device || !common_spi_host_valid(host) || config->cs_io_num < 0 ||
        config->cs_io_num >= SOC_GPIO_PIN_COUNT || config->clock_speed_hz <= 0 || config->mode > 3 ||
        config->max_transfer_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 每个 CS 只允许绑定一个设备句柄，避免两个设备同时被硬件选中。 */
    if (!s_bus_states[host].initialized || out_device->handle || out_device->mutex ||
        s_bus_states[host].cs_in_use[config->cs_io_num]) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config->max_transfer_bytes > s_bus_states[host].config.max_transfer_bytes) {
        /* SPI 总线创建后无法扩大 DMA 缓冲，设备上限不得超过该总线的上限。 */
        return ESP_ERR_INVALID_SIZE;
    }

    spi_device_interface_config_t device_config = {
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .clock_speed_hz = config->clock_speed_hz,
        .mode = config->mode,
        .spics_io_num = config->cs_io_num,
        .queue_size = 1,
    };
    esp_err_t ret = spi_bus_add_device(host, &device_config, &out_device->handle);
    if (ret != ESP_OK) {
        return ret;
    }

    out_device->mutex = xSemaphoreCreateMutex();
    if (!out_device->mutex) {
        /* 设备句柄若没有对应锁就不能安全使用，立即回收刚添加的设备。 */
        spi_bus_remove_device(out_device->handle);
        out_device->handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    out_device->max_transfer_bytes = config->max_transfer_bytes;
    /* 仅在设备和互斥锁均创建成功后才占用 CS，失败时可由调用方安全重试。 */
    s_bus_states[host].cs_in_use[config->cs_io_num] = true;
    return ESP_OK;
}

esp_err_t common_spi_write(const common_spi_device_t *device, const void *data, size_t len)
{
    if (!device || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!device->handle || !device->mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    /* polling 传输期间独占该设备，返回前才允许下一任务复用其句柄。 */
    if (xSemaphoreTake(device->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    const uint8_t *cursor = data;
    size_t remaining = len;
    esp_err_t ret = ESP_OK;
    while (remaining > 0) {
        size_t chunk_len = remaining > device->max_transfer_bytes ?
                               device->max_transfer_bytes : remaining;
        spi_transaction_t transaction = {
            /* ESP-IDF uses bits; this interface keeps bytes for its callers. */
            .length = chunk_len * 8,
            .tx_buffer = cursor,
        };
        ret = spi_device_polling_transmit(device->handle, &transaction);
        if (ret != ESP_OK) {
            break;
        }
        cursor += chunk_len;
        remaining -= chunk_len;
    }

    xSemaphoreGive(device->mutex);
    return ret;
}
