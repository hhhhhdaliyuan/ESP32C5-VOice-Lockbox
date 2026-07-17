#include "audio_i2s.h"

#include <stdbool.h>

#include "esp_log.h"

static const char *TAG = "audio_i2s";

/* I2S0 的两个硬件通道由本组件独占，Codec 层只通过公开接口访问。 */
static i2s_chan_handle_t s_tx_handle;
static i2s_chan_handle_t s_rx_handle;
static bool s_initialized;

esp_err_t audio_i2s_deinit(void)
{
    esp_err_t ret = ESP_OK;

    if (s_tx_handle) {
        /* disable 即使通道尚未 enable 也不会影响后续删除。 */
        i2s_channel_disable(s_tx_handle);
        esp_err_t delete_ret = i2s_del_channel(s_tx_handle);
        if (ret == ESP_OK && delete_ret != ESP_OK) {
            ret = delete_ret;
        }
        s_tx_handle = NULL;
    }
    if (s_rx_handle) {
        i2s_channel_disable(s_rx_handle);
        esp_err_t delete_ret = i2s_del_channel(s_rx_handle);
        if (ret == ESP_OK && delete_ret != ESP_OK) {
            ret = delete_ret;
        }
        s_rx_handle = NULL;
    }

    s_initialized = false;
    return ret;
}

esp_err_t audio_i2s_init(const audio_i2s_config_t *config)
{
    if (!config || config->sample_rate_hz == 0 || config->mclk_multiple == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_initialized) {
        return ESP_OK;
    }

    /* ESP32 提供时钟，ES8311 工作在 slave 模式。 */
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(config->port, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&channel_config, &s_tx_handle, &s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create I2S channels failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /*
     * ES8311 的数据总线使用 Philips I2S、立体声时隙。
     * 即使上层 PCM 对外是单声道，硬件仍按左右两个 slot 传输。
     */
    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(config->data_bit_width,
                                                         I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = config->mclk_gpio,
            .bclk = config->bclk_gpio,
            .ws = config->ws_gpio,
            .dout = config->dout_gpio,
            .din = config->din_gpio,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    /* 16 kHz 时使用 384 倍频 MCLK，满足 ES8311 时钟配置。 */
    std_config.clk_cfg.mclk_multiple = config->mclk_multiple;

    /* 两个通道必须采用完全相同的时钟、slot 和 GPIO 配置。 */
    ret = i2s_channel_init_std_mode(s_tx_handle, &std_config);
    if (ret == ESP_OK) {
        ret = i2s_channel_init_std_mode(s_rx_handle, &std_config);
    }
    if (ret == ESP_OK) {
        ret = i2s_channel_enable(s_tx_handle);
    }
    if (ret == ESP_OK) {
        ret = i2s_channel_enable(s_rx_handle);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "initialize I2S failed: %s", esp_err_to_name(ret));
        audio_i2s_deinit();
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "I2S%d ready: MCLK=%d BCLK=%d WS=%d DOUT=%d DIN=%d",
             config->port, config->mclk_gpio, config->bclk_gpio, config->ws_gpio,
             config->dout_gpio, config->din_gpio);
    return ESP_OK;
}

esp_err_t audio_i2s_read(void *buffer, size_t buffer_size, size_t *bytes_read, TickType_t timeout)
{
    if (!buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 此层仅转发原始立体声 I2S 数据，不进行 PCM 格式转换。 */
    return i2s_channel_read(s_rx_handle, buffer, buffer_size, bytes_read, timeout);
}

esp_err_t audio_i2s_write(const void *buffer, size_t buffer_size, size_t *bytes_written, TickType_t timeout)
{
    if (!buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 此层仅转发原始立体声 I2S 数据，不进行 PCM 格式转换。 */
    return i2s_channel_write(s_tx_handle, buffer, buffer_size, bytes_written, timeout);
}

esp_err_t audio_i2s_get_handles(i2s_chan_handle_t *tx_handle, i2s_chan_handle_t *rx_handle)
{
    if (!tx_handle || !rx_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    *tx_handle = s_tx_handle;
    *rx_handle = s_rx_handle;
    return ESP_OK;
}
