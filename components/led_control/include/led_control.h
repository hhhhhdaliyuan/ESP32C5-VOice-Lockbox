/**
 * @file led_control.h
 * @brief RGB LED sequence controller for lid motion.
 *
 * GPIOs are assigned by board_config. LEDs are active-low: anode -> 3.3 V
 * through a resistor, cathode -> GPIO.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_PATTERN_OFF = 0,
    LED_PATTERN_OPENING,
    LED_PATTERN_CLOSING,
    LED_PATTERN_OPEN_FAILED,
} led_pattern_t;

esp_err_t led_control_init(void);
void led_control_set_pattern(led_pattern_t pattern);
led_pattern_t led_control_get_pattern(void);

#ifdef __cplusplus
}
#endif