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

/*
 * ESP32-C5-WROOM-1 phase-1 microphone mapping.
 *
 * This wiring was validated on the MCN16R8 board with ES8311 capture.
 */
#define BOARD_ES8311_I2C_PORT          I2C_NUM_0
#define BOARD_ES8311_I2C_SDA_GPIO      GPIO_NUM_7
#define BOARD_ES8311_I2C_SCL_GPIO      GPIO_NUM_6

#define BOARD_ES8311_I2S_PORT          I2S_NUM_0
#define BOARD_ES8311_I2S_MCLK_GPIO     GPIO_NUM_0
#define BOARD_ES8311_I2S_BCLK_GPIO     GPIO_NUM_4
#define BOARD_ES8311_I2S_WS_GPIO       GPIO_NUM_5
#define BOARD_ES8311_I2S_DOUT_GPIO     GPIO_NUM_2
#define BOARD_ES8311_I2S_DIN_GPIO      GPIO_NUM_3

/*
 * GC9A01 status display.
 *
 * These pins are exposed by the MCN16R8 development board and do not overlap
 * with the ES8311, SG90, enrollment button, or CH340 console pins.
 */
#define BOARD_GC9A01_SPI_HOST          SPI2_HOST
#define BOARD_GC9A01_SPI_SCLK_GPIO     GPIO_NUM_8
#define BOARD_GC9A01_SPI_MOSI_GPIO     GPIO_NUM_9
#define BOARD_GC9A01_SPI_MISO_GPIO     GPIO_NUM_NC
#define BOARD_GC9A01_DC_GPIO           GPIO_NUM_13
#define BOARD_GC9A01_SPI_CS_GPIO       GPIO_NUM_14
#define BOARD_GC9A01_RST_GPIO          GPIO_NUM_23

#define BOARD_LED_RED_GPIO             GPIO_NUM_NC
#define BOARD_LED_GREEN_GPIO           GPIO_NUM_NC
#define BOARD_LED_YELLOW_GPIO          GPIO_NUM_NC

#define BOARD_SG90_PWM_GPIO            GPIO_NUM_10

/*
 * Local button inputs.
 * Buttons connect GPIO to GND and use the internal pull-up.
 */
#define BOARD_CLOSE_BUTTON_PIN         GPIO_NUM_1
#define BOARD_ENROLL_BUTTON_PIN        GPIO_NUM_24
#define BOARD_DELETE_BUTTON_PIN        GPIO_NUM_25
#define BOARD_ENROLL_SELECT_BUTTON_PIN GPIO_NUM_NC
#define BOARD_EC11_A_GPIO              GPIO_NUM_NC
#define BOARD_EC11_B_GPIO              GPIO_NUM_NC
#define BOARD_CONFIRM_BUTTON_GPIO      GPIO_NUM_NC
#define BOARD_ADMIN_BUTTON_GPIO        GPIO_NUM_NC
#define BOARD_BUTTON_ACTIVE_LEVEL      0
