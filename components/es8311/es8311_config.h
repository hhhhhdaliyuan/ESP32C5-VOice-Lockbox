#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"

/* 自检宏只在 es8311 组件内部生效：0 关闭，1 开启。 */
#define ES8311_SELF_TEST_ENABLE       0         /*测试宏，0关闭，1打开*/

/* ES8311 控制总线：I2C0，设备地址由 esp_codec_dev 的默认配置提供（0x30）。 */
#define ES8311_I2C_PORT               I2C_NUM_0
#define ES8311_I2C_SDA_GPIO           GPIO_NUM_17
#define ES8311_I2C_SCL_GPIO           GPIO_NUM_16

/* ES8311 数据总线：ESP32-S3 是 I2S 主机，Codec 是从机。 */
#define ES8311_I2S_PORT               I2S_NUM_0
#define ES8311_I2S_MCLK_GPIO          GPIO_NUM_20
#define ES8311_I2S_BCLK_GPIO          GPIO_NUM_4
#define ES8311_I2S_WS_GPIO            GPIO_NUM_5
#define ES8311_I2S_DOUT_GPIO          GPIO_NUM_18
#define ES8311_I2S_DIN_GPIO           GPIO_NUM_19

/* 16 kHz 采样率使用 384 倍频 MCLK；其余值为初始化和 I/O 策略。 */
#define ES8311_MCLK_MULTIPLE          384
#define ES8311_CODEC_INIT_RETRIES     3
#define ES8311_CODEC_RETRY_DELAY_MS   200
#define ES8311_IO_TIMEOUT_MS          100
#define ES8311_SELF_TEST_SECONDS      2
