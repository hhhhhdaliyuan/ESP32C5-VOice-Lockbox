#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"

/*
 * Board-wide peripheral ownership.
 *
 * Keep physical GPIO assignments in this component so later voiceprint, KWS,
 * display, LED, and actuator work uses one shared source of truth.
 */

/* ES8311 control bus and PCM data bus. */
#define BOARD_ES8311_I2C_PORT          I2C_NUM_0
#define BOARD_ES8311_I2C_SDA_GPIO      GPIO_NUM_17
#define BOARD_ES8311_I2C_SCL_GPIO      GPIO_NUM_16

#define BOARD_ES8311_I2S_PORT          I2S_NUM_0
#define BOARD_ES8311_I2S_MCLK_GPIO     GPIO_NUM_20
#define BOARD_ES8311_I2S_BCLK_GPIO     GPIO_NUM_4
#define BOARD_ES8311_I2S_WS_GPIO       GPIO_NUM_5
#define BOARD_ES8311_I2S_DOUT_GPIO     GPIO_NUM_18
#define BOARD_ES8311_I2S_DIN_GPIO      GPIO_NUM_19

/* GC9A01 display. */
#define BOARD_GC9A01_SPI_HOST          SPI2_HOST
#define BOARD_GC9A01_SPI_SCLK_GPIO     GPIO_NUM_9
#define BOARD_GC9A01_SPI_MOSI_GPIO     GPIO_NUM_10
#define BOARD_GC9A01_SPI_MISO_GPIO     GPIO_NUM_NC
#define BOARD_GC9A01_DC_GPIO           GPIO_NUM_11
#define BOARD_GC9A01_SPI_CS_GPIO       GPIO_NUM_12
#define BOARD_GC9A01_RST_GPIO          GPIO_NUM_13

/* Status LEDs. GPIO2 and GPIO7 avoid the ES8311 bus pins. */
#define BOARD_LED_RED_GPIO             GPIO_NUM_3
#define BOARD_LED_GREEN_GPIO           GPIO_NUM_2
#define BOARD_LED_YELLOW_GPIO          GPIO_NUM_7

/* SG90 lid actuator. */
#define BOARD_SG90_PWM_GPIO            GPIO_NUM_6

/*
 * SonKey-compatible local voiceprint registration inputs.
 * Buttons connect GPIO to GND and use the internal pull-up.
 */
#define BOARD_ENROLL_BUTTON_PIN        GPIO_NUM_1
#define BOARD_ENROLL_SELECT_BUTTON_PIN GPIO_NUM_42
#define BOARD_EC11_A_GPIO              GPIO_NUM_41
#define BOARD_EC11_B_GPIO              GPIO_NUM_39
#define BOARD_CONFIRM_BUTTON_GPIO      GPIO_NUM_40
#define BOARD_ADMIN_BUTTON_GPIO        GPIO_NUM_8
#define BOARD_BUTTON_ACTIVE_LEVEL      0
