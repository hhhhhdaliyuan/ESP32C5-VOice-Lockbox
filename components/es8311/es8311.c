#include "es8311.h"

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "es8311";

#define ES8311_MCLK_MULTIPLE       384
#define ES8311_INIT_RETRIES        3
#define ES8311_RETRY_DELAY_MS      200
#define ES8311_IO_TIMEOUT_MS       100

static i2s_chan_handle_t s_tx_handle;
static i2s_chan_handle_t s_rx_handle;
static i2c_master_bus_handle_t s_i2c_bus;
static const audio_codec_ctrl_if_t *s_ctrl_if;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static const audio_codec_if_t *s_codec_if;
static esp_codec_dev_handle_t s_codec;
static bool s_initialized;

static void es8311_codec_cleanup(void)
{
    if (s_codec) {
        esp_codec_dev_delete(s_codec);
        s_codec = NULL;
    }
    if (s_codec_if) {
        audio_codec_delete_codec_if(s_codec_if);
        s_codec_if = NULL;
    }
    if (s_gpio_if) {
        audio_codec_delete_gpio_if(s_gpio_if);
        s_gpio_if = NULL;
    }
    if (s_data_if) {
        audio_codec_delete_data_if(s_data_if);
        s_data_if = NULL;
    }
    if (s_ctrl_if) {
        audio_codec_delete_ctrl_if(s_ctrl_if);
        s_ctrl_if = NULL;
    }
    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
}

static void es8311_i2s_cleanup(void)
{
    if (s_tx_handle) {
        i2s_channel_disable(s_tx_handle);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
    }
    if (s_rx_handle) {
        i2s_channel_disable(s_rx_handle);
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
    }
}

static esp_err_t es8311_i2s_init(void)
{
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(BOARD_ES8311_I2S_PORT, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;
    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&channel_config, &s_tx_handle, &s_rx_handle),
        TAG, "create I2S channels failed");

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ES8311_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_ES8311_I2S_MCLK_GPIO,
            .bclk = BOARD_ES8311_I2S_BCLK_GPIO,
            .ws = BOARD_ES8311_I2S_WS_GPIO,
            .dout = BOARD_ES8311_I2S_DOUT_GPIO,
            .din = BOARD_ES8311_I2S_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    standard_config.clk_cfg.mclk_multiple = ES8311_MCLK_MULTIPLE;

    esp_err_t ret = i2s_channel_init_std_mode(s_tx_handle, &standard_config);
    if (ret == ESP_OK) {
        ret = i2s_channel_init_std_mode(s_rx_handle, &standard_config);
    }
    if (ret == ESP_OK) {
        ret = i2s_channel_enable(s_tx_handle);
    }
    if (ret == ESP_OK) {
        ret = i2s_channel_enable(s_rx_handle);
    }
    if (ret != ESP_OK) {
        es8311_i2s_cleanup();
        return ret;
    }

    ESP_LOGI(TAG,
             "I2S0 master ready: MCLK=%d BCLK=%d WS=%d DOUT=%d DIN=%d",
             BOARD_ES8311_I2S_MCLK_GPIO, BOARD_ES8311_I2S_BCLK_GPIO,
             BOARD_ES8311_I2S_WS_GPIO, BOARD_ES8311_I2S_DOUT_GPIO,
             BOARD_ES8311_I2S_DIN_GPIO);
    return ESP_OK;
}

static esp_err_t es8311_codec_init(void)
{
    i2c_master_bus_config_t i2c_config = {
        .i2c_port = BOARD_ES8311_I2C_PORT,
        .sda_io_num = BOARD_ES8311_I2C_SDA_GPIO,
        .scl_io_num = BOARD_ES8311_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&i2c_config, &s_i2c_bus);
    if (ret != ESP_OK) {
        return ret;
    }

    audio_codec_i2c_cfg_t i2c_control_config = {
        .port = BOARD_ES8311_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_control_config);
    if (!s_ctrl_if) {
        ret = ESP_FAIL;
        goto fail;
    }

    audio_codec_i2s_cfg_t i2s_data_config = {
        .port = BOARD_ES8311_I2S_PORT,
        .rx_handle = s_rx_handle,
        .tx_handle = s_tx_handle,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_data_config);
    if (!s_data_if) {
        ret = ESP_FAIL;
        goto fail;
    }

    s_gpio_if = audio_codec_new_gpio();
    if (!s_gpio_if) {
        ret = ESP_FAIL;
        goto fail;
    }

    es8311_codec_cfg_t codec_config = {
        .ctrl_if = s_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,
        .use_mclk = true,
        .pa_pin = GPIO_NUM_NC,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0f,
            .codec_dac_voltage = 3.3f,
        },
        .mclk_div = ES8311_MCLK_MULTIPLE,
    };
    s_codec_if = es8311_codec_new(&codec_config);
    if (!s_codec_if) {
        ret = ESP_FAIL;
        goto fail;
    }

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = s_codec_if,
        .data_if = s_data_if,
    };
    s_codec = esp_codec_dev_new(&device_config);
    if (!s_codec) {
        ret = ESP_FAIL;
        goto fail;
    }

    esp_codec_dev_sample_info_t sample_config = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = 2,
        .channel_mask = 0x03,
        .sample_rate = ES8311_SAMPLE_RATE_HZ,
        .mclk_multiple = ES8311_MCLK_MULTIPLE,
    };
    if (esp_codec_dev_open(s_codec, &sample_config) != ESP_CODEC_DEV_OK) {
        ret = ESP_FAIL;
        goto fail;
    }

    esp_codec_dev_set_out_vol(s_codec, 60);
    esp_codec_dev_set_in_gain(s_codec, 30.0f);
    ESP_LOGI(TAG, "codec ready: I2C addr=0x%02x SDA=%d SCL=%d gain=30dB",
             ES8311_CODEC_DEFAULT_ADDR, BOARD_ES8311_I2C_SDA_GPIO,
             BOARD_ES8311_I2C_SCL_GPIO);
    return ESP_OK;

fail:
    es8311_codec_cleanup();
    return ret;
}

esp_err_t es8311_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = es8311_i2s_init();
    if (ret != ESP_OK) {
        return ret;
    }

    for (int attempt = 1; attempt <= ES8311_INIT_RETRIES; ++attempt) {
        ret = es8311_codec_init();
        if (ret == ESP_OK) {
            s_initialized = true;
            ESP_LOGI(TAG, "live capture ready: 16kHz mono PCM, LEFT slot");
            return ESP_OK;
        }

        ESP_LOGW(TAG, "codec init failed, attempt %d/%d: %s",
                 attempt, ES8311_INIT_RETRIES, esp_err_to_name(ret));
        if (attempt < ES8311_INIT_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(ES8311_RETRY_DELAY_MS));
        }
    }

    es8311_i2s_cleanup();
    return ret;
}

esp_err_t es8311_read_stereo_pcm(void *buffer, size_t buffer_size,
                                 size_t *bytes_read)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!buffer || buffer_size < ES8311_STEREO_FRAME_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t stereo_bytes_read = 0;
    esp_err_t ret = i2s_channel_read(
        s_rx_handle, buffer, ES8311_STEREO_FRAME_BYTES, &stereo_bytes_read,
        pdMS_TO_TICKS(ES8311_IO_TIMEOUT_MS));
    if (ret != ESP_OK || stereo_bytes_read != ES8311_STEREO_FRAME_BYTES) {
        if (bytes_read) {
            *bytes_read = 0;
        }
        return ret == ESP_OK ? ESP_ERR_TIMEOUT : ret;
    }

    size_t stereo_bytes_written = 0;
    i2s_channel_write(s_tx_handle, buffer, stereo_bytes_read,
                      &stereo_bytes_written, 0);

    if (bytes_read) {
        *bytes_read = stereo_bytes_read;
    }
    return ESP_OK;
}

esp_err_t es8311_read_pcm(void *buffer, size_t buffer_size, size_t *bytes_read)
{
    if (!buffer || buffer_size < ES8311_PCM_FRAME_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }

    int16_t stereo_frame[ES8311_STEREO_FRAME_BYTES / sizeof(int16_t)];
    size_t stereo_bytes_read = 0;
    esp_err_t ret = es8311_read_stereo_pcm(
        stereo_frame, sizeof(stereo_frame), &stereo_bytes_read);
    if (ret != ESP_OK) {
        if (bytes_read) {
            *bytes_read = 0;
        }
        return ret;
    }

    int16_t *mono_frame = buffer;
    const size_t mono_sample_count =
        ES8311_PCM_FRAME_BYTES / sizeof(int16_t);
    for (size_t sample = 0; sample < mono_sample_count; ++sample) {
        mono_frame[sample] = stereo_frame[sample * ES8311_STEREO_CHANNELS];
    }

    if (bytes_read) {
        *bytes_read = ES8311_PCM_FRAME_BYTES;
    }
    return ESP_OK;
}
