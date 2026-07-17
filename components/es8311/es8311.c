#include "es8311.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "audio_i2s.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "es8311_config.h"

static const char *TAG = "es8311";

/* 硬件 I2S 固定为双 slot；对外 PCM API 保持单声道。 */
#define ES8311_I2S_CHANNELS            2
#define ES8311_STEREO_FRAME_BYTES       (ES8311_PCM_FRAME_BYTES * ES8311_I2S_CHANNELS)
#define ES8311_SELF_TEST_FRAME_COUNT    (ES8311_SELF_TEST_SECONDS * 1000 / ES8311_PCM_FRAME_MS)
#define ES8311_SELF_TEST_BUFFER_BYTES   (ES8311_SELF_TEST_FRAME_COUNT * ES8311_PCM_FRAME_BYTES)

/* esp_codec_dev 创建的对象具有依赖关系，必须按 cleanup 中的逆序释放。 */
static i2c_master_bus_handle_t s_i2c_bus;
static const audio_codec_ctrl_if_t *s_ctrl_if;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static const audio_codec_if_t *s_codec_if;
static esp_codec_dev_handle_t s_codec;
static bool s_initialized;

static void es8311_codec_cleanup(void)
{
    /* 先释放最外层 device，再释放它持有的 Codec、GPIO、I2S、I2C 接口。 */
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

static esp_err_t es8311_codec_init(void)
{
    i2s_chan_handle_t tx_handle;
    i2s_chan_handle_t rx_handle;
    ESP_RETURN_ON_ERROR(audio_i2s_get_handles(&tx_handle, &rx_handle), TAG,
                        "get I2S handles failed");

    /* I2C 只传输 ES8311 寄存器配置，PCM 数据走已初始化的 I2S 通道。 */
    i2c_master_bus_config_t i2c_config = {
        .i2c_port = ES8311_I2C_PORT,
        .sda_io_num = ES8311_I2C_SDA_GPIO,
        .scl_io_num = ES8311_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&i2c_config, &s_i2c_bus);
    if (ret != ESP_OK) {
        return ret;
    }

    audio_codec_i2c_cfg_t i2c_ctrl_config = {
        .port = ES8311_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_ctrl_config);
    if (!s_ctrl_if) {
        ret = ESP_FAIL;
        goto fail;
    }

    /* 将 audio_i2s 创建的 TX/RX 句柄交给 esp_codec_dev，避免重复申请 I2S 通道。 */
    audio_codec_i2s_cfg_t i2s_data_config = {
        .port = ES8311_I2S_PORT,
        .rx_handle = rx_handle,
        .tx_handle = tx_handle,
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

    /* 板上无 PA 使能脚；MCLK 由 ESP32 输出，因此 Codec 设为 slave。 */
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

    /* 此处描述 I2S 物理格式，必须与 audio_i2s 的立体声 slot 配置一致。 */
    esp_codec_dev_sample_info_t sample_config = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = ES8311_I2S_CHANNELS,
        .channel_mask = 0x03,
        .sample_rate = ES8311_SAMPLE_RATE_HZ,
        .mclk_multiple = ES8311_MCLK_MULTIPLE,
    };
    if (esp_codec_dev_open(s_codec, &sample_config) != ESP_CODEC_DEV_OK) {
        ret = ESP_FAIL;
        goto fail;
    }

    /* 使用 SonKey 已验证的输入增益和输出音量。 */
    esp_codec_dev_set_out_vol(s_codec, 60);
    esp_codec_dev_set_in_gain(s_codec, 30.0f);
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

    const audio_i2s_config_t i2s_config = {
        .port = ES8311_I2S_PORT,
        .mclk_gpio = ES8311_I2S_MCLK_GPIO,
        .bclk_gpio = ES8311_I2S_BCLK_GPIO,
        .ws_gpio = ES8311_I2S_WS_GPIO,
        .dout_gpio = ES8311_I2S_DOUT_GPIO,
        .din_gpio = ES8311_I2S_DIN_GPIO,
        .sample_rate_hz = ES8311_SAMPLE_RATE_HZ,
        .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
        .mclk_multiple = ES8311_MCLK_MULTIPLE,
    };
    esp_err_t ret = audio_i2s_init(&i2s_config);
    if (ret != ESP_OK) {
        return ret;
    }

    /* I2C 设备上电就绪可能滞后，初始化失败时按配置重试。 */
    for (int attempt = 1; attempt <= ES8311_CODEC_INIT_RETRIES; ++attempt) {
        ret = es8311_codec_init();
        if (ret == ESP_OK) {
            s_initialized = true;
            ESP_LOGI(TAG, "codec ready: 16kHz mono PCM, mic gain 30dB");
            return ESP_OK;
        }

        ESP_LOGW(TAG, "codec init attempt %d/%d failed: %s", attempt,
                 ES8311_CODEC_INIT_RETRIES, esp_err_to_name(ret));
        es8311_codec_cleanup();
        if (attempt < ES8311_CODEC_INIT_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(ES8311_CODEC_RETRY_DELAY_MS));
        }
    }

    audio_i2s_deinit();
    return ret;
}

esp_err_t es8311_read_pcm(void *buffer, size_t buffer_size, size_t *bytes_read)
{
    if (!buffer || buffer_size < ES8311_PCM_FRAME_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 先收完整的左右双 slot，再保留左声道作为上层单声道 PCM。 */
    int16_t stereo_frame[ES8311_STEREO_FRAME_BYTES / sizeof(int16_t)];
    size_t stereo_bytes_read = 0;
    esp_err_t ret = audio_i2s_read(stereo_frame, sizeof(stereo_frame), &stereo_bytes_read,
                                   pdMS_TO_TICKS(ES8311_IO_TIMEOUT_MS));
    if (ret != ESP_OK) {
        if (bytes_read) {
            *bytes_read = 0;
        }
        return ret;
    }
    if (stereo_bytes_read != sizeof(stereo_frame)) {
        if (bytes_read) {
            *bytes_read = 0;
        }
        return ESP_ERR_TIMEOUT;
    }

    int16_t *mono_frame = buffer;
    for (size_t i = 0; i < ES8311_PCM_FRAME_BYTES / sizeof(int16_t); ++i) {
        mono_frame[i] = stereo_frame[i * ES8311_I2S_CHANNELS];
    }

    if (bytes_read) {
        *bytes_read = ES8311_PCM_FRAME_BYTES;
    }
    return ESP_OK;
}

esp_err_t es8311_write_pcm(const void *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < ES8311_PCM_FRAME_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 播放时把单声道样本复制至左右 slot，匹配底层立体声 I2S 格式。 */
    const int16_t *mono_frame = buffer;
    int16_t stereo_frame[ES8311_STEREO_FRAME_BYTES / sizeof(int16_t)];
    for (size_t i = 0; i < ES8311_PCM_FRAME_BYTES / sizeof(int16_t); ++i) {
        stereo_frame[i * ES8311_I2S_CHANNELS] = mono_frame[i];
        stereo_frame[i * ES8311_I2S_CHANNELS + 1] = mono_frame[i];
    }

    size_t stereo_bytes_written = 0;
    esp_err_t ret = audio_i2s_write(stereo_frame, sizeof(stereo_frame), &stereo_bytes_written,
                                    pdMS_TO_TICKS(ES8311_IO_TIMEOUT_MS));
    if (ret != ESP_OK) {
        return ret;
    }
    return stereo_bytes_written == sizeof(stereo_frame) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t es8311_deinit(void)
{
    es8311_codec_cleanup();
    s_initialized = false;
    return audio_i2s_deinit();
}

esp_err_t es8311_run_self_test(void)
{
#if ES8311_SELF_TEST_ENABLE
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 自检缓冲放在 PSRAM，避免 2 秒 PCM 占用内部 RAM。 */
    uint8_t *recording = heap_caps_malloc(ES8311_SELF_TEST_BUFFER_BYTES,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!recording) {
        return ESP_ERR_NO_MEM;
    }

    /* 连续循环，便于板端持续确认收音与播放链路均正常。 */
    while (true) {
        ESP_LOGI(TAG, "self-test recording %d seconds", ES8311_SELF_TEST_SECONDS);
        for (size_t frame = 0; frame < ES8311_SELF_TEST_FRAME_COUNT; ++frame) {
            size_t bytes_read = 0;
            esp_err_t ret = es8311_read_pcm(recording + frame * ES8311_PCM_FRAME_BYTES,
                                             ES8311_PCM_FRAME_BYTES, &bytes_read);
            if (ret != ESP_OK || bytes_read != ES8311_PCM_FRAME_BYTES) {
                free(recording);
                return ret == ESP_OK ? ESP_ERR_TIMEOUT : ret;
            }
        }

        ESP_LOGI(TAG, "self-test playing %d seconds", ES8311_SELF_TEST_SECONDS);
        for (size_t frame = 0; frame < ES8311_SELF_TEST_FRAME_COUNT; ++frame) {
            esp_err_t ret = es8311_write_pcm(recording + frame * ES8311_PCM_FRAME_BYTES,
                                              ES8311_PCM_FRAME_BYTES);
            if (ret != ESP_OK) {
                free(recording);
                return ret;
            }
        }
    }
#else
    ESP_LOGI(TAG, "self-test disabled");
    return ESP_OK;
#endif
}
