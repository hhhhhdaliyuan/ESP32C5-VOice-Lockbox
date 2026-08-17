#include "servo.h"

#include <stdbool.h>

#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "servo";

#define SERVO_PWM_FREQ_HZ 50
#define SERVO_PWM_RESOLUTION LEDC_TIMER_13_BIT
#define SERVO_OPEN_ANGLE 150

static int s_gpio_pin = -1;
static int s_min_pulse_us = 500;
static int s_max_pulse_us = 2500;
static int s_current_angle;
static servo_state_t s_state = SERVO_STATE_CLOSED;
static bool s_initialized;

static uint32_t angle_to_duty(int angle)
{
    if (angle < 0) {
        angle = 0;
    } else if (angle > 180) {
        angle = 180;
    }

    int pulse_us = s_min_pulse_us
                   + (angle * (s_max_pulse_us - s_min_pulse_us)) / 180;
    return (uint32_t)((uint64_t)pulse_us * 8191U / 20000U);
}

esp_err_t servo_init(int gpio_pin, int min_pulse_us, int max_pulse_us)
{
    if (gpio_pin < 0 || min_pulse_us <= 0 || max_pulse_us <= min_pulse_us) {
        return ESP_ERR_INVALID_ARG;
    }

    s_gpio_pin = gpio_pin;
    s_min_pulse_us = min_pulse_us;
    s_max_pulse_us = max_pulse_us;

    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = SERVO_PWM_RESOLUTION,
        .freq_hz = SERVO_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const ledc_channel_config_t channel_config = {
        .gpio_num = s_gpio_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = angle_to_duty(0),
        .hpoint = 0,
    };
    ret = ledc_channel_config(&channel_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    s_current_angle = 0;
    s_state = SERVO_STATE_CLOSED;
    ESP_LOGI(TAG, "servo initialized on GPIO%d, pulse range %d~%d us",
             gpio_pin, min_pulse_us, max_pulse_us);
    return ESP_OK;
}

esp_err_t servo_set_angle(int angle)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (angle < 0) {
        angle = 0;
    } else if (angle > 180) {
        angle = 180;
    }

    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                  angle_to_duty(angle));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC duty set failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC duty update failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_current_angle = angle;
    return ESP_OK;
}

esp_err_t servo_open(int step_delay_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state == SERVO_STATE_OPENED) {
        return ESP_OK;
    }
    if (step_delay_ms <= 0) {
        step_delay_ms = 20;
    }

    ESP_LOGI(TAG, "opening lid");
    for (int angle = s_current_angle; angle <= SERVO_OPEN_ANGLE; angle++) {
        esp_err_t ret = servo_set_angle(angle);
        if (ret != ESP_OK) {
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
    }

    s_current_angle = SERVO_OPEN_ANGLE;
    s_state = SERVO_STATE_OPENED;
    ESP_LOGI(TAG, "lid opened");
    return ESP_OK;
}

esp_err_t servo_close(int step_delay_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state == SERVO_STATE_CLOSED) {
        return ESP_OK;
    }
    if (step_delay_ms <= 0) {
        step_delay_ms = 20;
    }

    ESP_LOGI(TAG, "closing lid");
    for (int angle = s_current_angle; angle >= 0; angle--) {
        esp_err_t ret = servo_set_angle(angle);
        if (ret != ESP_OK) {
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
    }

    s_current_angle = 0;
    s_state = SERVO_STATE_CLOSED;
    ESP_LOGI(TAG, "lid closed");
    return ESP_OK;
}

servo_state_t servo_get_state(void)
{
    return s_state;
}

int servo_get_angle(void)
{
    return s_current_angle;
}