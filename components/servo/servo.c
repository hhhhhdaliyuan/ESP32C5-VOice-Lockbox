#include "servo.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "servo";

/* ---------- 硬件参数 ---------- */
#define SERVO_PWM_FREQ_HZ   50      /* SG90 标准 50 Hz */
#define SERVO_PWM_RESOLUTION LEDC_TIMER_13_BIT  /* 13-bit → 8192 级 */

/* ---------- 静态变量 ---------- */
static int s_gpio_pin = -1;
static int s_min_pulse_us = 500;   /* 0° 脉宽 */
static int s_max_pulse_us = 2500;  /* 180° 脉宽 */
static int s_current_angle = 0;
static servo_state_t s_state = SERVO_STATE_CLOSED;
static bool s_initialized = false;

/* ---------- 内部辅助 ---------- */

/**
 * @brief 将角度映射为 LEDC duty 值
 *
 * 50 Hz → 周期 20000 µs，13-bit 分辨率 → 满占空比 8191
 * duty = pulse_us * 8191 / 20000
 */
static uint32_t angle_to_duty(int angle)
{
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    int pulse_us = s_min_pulse_us +
                   (angle * (s_max_pulse_us - s_min_pulse_us)) / 180;

    uint32_t duty = (uint32_t)((uint64_t)pulse_us * 8191 / 20000);
    return duty;
}

/* ---------- 公开 API ---------- */

esp_err_t servo_init(int gpio_pin, int min_pulse_us, int max_pulse_us)
{
    if (gpio_pin < 0) {
        ESP_LOGE(TAG, "invalid GPIO: %d", gpio_pin);
        return ESP_ERR_INVALID_ARG;
    }

    s_gpio_pin = gpio_pin;
    s_min_pulse_us = min_pulse_us;
    s_max_pulse_us = max_pulse_us;

    /* 配置 LEDC 定时器 — 50 Hz, 13-bit */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num  = LEDC_TIMER_0,
        .duty_resolution = SERVO_PWM_RESOLUTION,
        .freq_hz    = SERVO_PWM_FREQ_HZ,
        .clk_cfg    = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 配置 LEDC 通道 */
    ledc_channel_config_t chan_cfg = {
        .gpio_num   = s_gpio_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = angle_to_duty(0),  /* 初始闭合 */
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    s_current_angle = 0;
    s_state = SERVO_STATE_CLOSED;

    ESP_LOGI(TAG, "servo initialized on GPIO%d, pulse range %d~%d us",
             gpio_pin, min_pulse_us, max_pulse_us);
    return ESP_OK;
}

void servo_set_angle(int angle)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "servo not initialized");
        return;
    }

    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    uint32_t duty = angle_to_duty(angle);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    s_current_angle = angle;
    ESP_LOGD(TAG, "set angle=%d, duty=%lu", angle, (unsigned long)duty);
}

void servo_open(int step_delay_ms) //open the box
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "servo not initialized");
        return;
    }
    if (step_delay_ms <= 0) step_delay_ms = 20;

    /* 已经打开则跳过 */
    if (s_state == SERVO_STATE_OPENED) {
        ESP_LOGW(TAG, "already opened, skip");
        return;
    }

    ESP_LOGI(TAG, "opening ...");

    /* 从当前角度步进到 90° */
    for (int angle = s_current_angle; angle <= 90; angle++) {
        servo_set_angle(angle);
        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
    }

    s_current_angle = 90;
    s_state = SERVO_STATE_OPENED;
    ESP_LOGI(TAG, "opened");
}
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "servo not initialized");
        return;
    }
    if (step_delay_ms <= 0) step_delay_ms = 20;

    /* 已经打开则跳过 */
    if (s_state == SERVO_STATE_OPENED) {
        ESP_LOGW(TAG, "already opened, skip");
        return;
    }

    ESP_LOGI(TAG, "opening ...");

    /* 从当前角度步进到 90° */
    for (int angle = s_current_angle; angle <= 90; angle++) {
        servo_set_angle(angle);
        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
    }

    s_current_angle = 90;
    s_state = SERVO_STATE_OPENED;
    ESP_LOGI(TAG, "opened");
}

void servo_close(int step_delay_ms)  //back to 0°
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "servo not initialized");
        return;
    }
    if (step_delay_ms <= 0) step_delay_ms = 20;

    /* 已经闭合则跳过 */
    if (s_state == SERVO_STATE_CLOSED) {
        ESP_LOGW(TAG, "already closed, skip");
        return;
    }

    ESP_LOGI(TAG, "closing ...");

    /* 从当前角度步进到 0° */
    for (int angle = s_current_angle; angle >= 0; angle--) {
        servo_set_angle(angle);
        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
    }

    s_current_angle = 0;
    s_state = SERVO_STATE_CLOSED;
    ESP_LOGI(TAG, "closed");
}

servo_state_t servo_get_state(void)
{
    return s_state;
}

int servo_get_angle(void)
{
    return s_current_angle;
}
