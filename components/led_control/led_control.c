#include "led_control.h"

#include <stdio.h>
#include <stdbool.h>
#include "board_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led_control";

/* ---------- 定时参数（毫秒） ---------- */
#define BLINK_UNLOCK_SUCCESS_MS    300   /* 解锁成功闪烁间隔 */
#define BLINK_UNLOCK_SUCCESS_TIMES 3     /* 解锁成功闪烁次数 */
#define BLINK_UNLOCK_FAIL_MS       150   /* 解锁失败快速闪烁间隔 */
#define BLINK_CLOSED_MS            400   /* 闭盒完毕闪烁间隔 */
#define CYCLE_OPENING_MS           200   /* 开盒循环每步间隔 */
#define IDLE_DELAY_MS              500   /* 待机任务轮询间隔 */

/* ---------- 静态变量 ---------- */
static led_pattern_t s_pattern = LED_PATTERN_OFF;
static TaskHandle_t s_task_handle = NULL;
static bool s_initialized = false;

/* ---------- 底层 GPIO 操作 ---------- */

static bool led_pins_assigned(void)
{
    return BOARD_LED_RED_GPIO != GPIO_NUM_NC
           && BOARD_LED_GREEN_GPIO != GPIO_NUM_NC
           && BOARD_LED_YELLOW_GPIO != GPIO_NUM_NC;
}

static uint64_t led_gpio_bit(gpio_num_t pin)
{
    return pin == GPIO_NUM_NC ? 0 : 1ULL << pin;
}

static void led_set(uint8_t red, uint8_t green, uint8_t yellow)
{
    gpio_set_level(BOARD_LED_RED_GPIO,    red);
    gpio_set_level(BOARD_LED_GREEN_GPIO,  green);
    gpio_set_level(BOARD_LED_YELLOW_GPIO, yellow);
}
static void all_off(void)  { led_set(0, 0, 0); }
static void green_on(void) { led_set(0, 1, 0); }
static void green_yellow_on(void){ led_set(0, 1, 1); }
/* ---------- 闪烁辅助 ---------- */

/**
 * @brief 指定灯闪烁 count 次，每次间隔 ms
 *        结束后保持 off。
 */
static void blink_gpio(uint8_t r, uint8_t g, uint8_t y, int count, int ms)
{
    for (int i = 0; i < count; i++) {
        led_set(r, g, y);
        vTaskDelay(pdMS_TO_TICKS(ms));
        all_off();
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

/* ---------- 各模式执行函数 ---------- */

static void pattern_off(void)
{
    all_off();
    /* 任务挂起直到被再次唤醒 */
    vTaskSuspend(NULL);
}

static void pattern_idle(void)
{
    green_on();
    vTaskDelay(pdMS_TO_TICKS(IDLE_DELAY_MS));
}

static void pattern_unlock_success(void)
{
    blink_gpio(0, 1, 0, BLINK_UNLOCK_SUCCESS_TIMES, BLINK_UNLOCK_SUCCESS_MS);
    /* 闪烁结束后绿灯常亮 */
    green_on();
    vTaskDelay(pdMS_TO_TICKS(IDLE_DELAY_MS));
}

static void pattern_unlock_fail(void)
{
    blink_gpio(1, 0, 0, 1, BLINK_UNLOCK_FAIL_MS);
}

static void pattern_opening(void)
{
    /* 多色循环：红 → 绿 → 黄 → 红+绿 → 绿+黄 → 红+黄 → 全亮 → 全灭 → 重复 */
    static const uint8_t colors[8][3] = {
        {1, 0, 0},  /* 红 */
        {0, 1, 0},  /* 绿 */
        {0, 0, 1},  /* 黄 */
        {1, 1, 0},  /* 红+绿 */
        {0, 1, 1},  /* 绿+黄 */
        {1, 0, 1},  /* 红+黄 */
        {1, 1, 1},  /* 全亮 */
        {0, 0, 0},  /* 全灭 */
    };
    static int idx = 0;
    led_set(colors[idx][0], colors[idx][1], colors[idx][2]);
    idx = (idx + 1) % 8;
    vTaskDelay(pdMS_TO_TICKS(CYCLE_OPENING_MS));
}

static void pattern_closed(void)
{
    green_yellow_on();
    vTaskDelay(pdMS_TO_TICKS(BLINK_CLOSED_MS));
    all_off();
    vTaskDelay(pdMS_TO_TICKS(BLINK_CLOSED_MS));
}

/* ---------- LED 控制任务 ---------- */

static void led_task(void *arg)
{
    (void)arg;
    led_pattern_t current = LED_PATTERN_OFF;

    ESP_LOGI(TAG, "led task started");

    while (1) {
        /* 检查模式是否变化，若切换到了 OFF 则恢复任务 */
        if (s_pattern != current) {
            current = s_pattern;
            ESP_LOGD(TAG, "switch to pattern %d", current);
        }

        switch (current) {
        case LED_PATTERN_OFF:
            pattern_off();
            break;
        case LED_PATTERN_IDLE:
            pattern_idle();
            break;
        case LED_PATTERN_UNLOCK_SUCCESS:
            pattern_unlock_success();
            break;
        case LED_PATTERN_UNLOCK_FAIL:
            pattern_unlock_fail();
            break;
        case LED_PATTERN_OPENING:
            pattern_opening();
            break;
        case LED_PATTERN_CLOSED:
            pattern_closed();
            break;
        default:
            all_off();
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
        }
    }
}

/* ---------- 公开 API ---------- */

esp_err_t led_control_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

    if (!led_pins_assigned()) {
        ESP_LOGW(TAG, "LED GPIOs are not assigned for this board");
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* 配置 GPIO */
    gpio_config_t io_conf = {
        .pin_bit_mask = led_gpio_bit(BOARD_LED_RED_GPIO)
                      | led_gpio_bit(BOARD_LED_GREEN_GPIO)
                      | led_gpio_bit(BOARD_LED_YELLOW_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 初始全灭 */
    all_off();

    /* 创建控制任务 */
    ret = xTaskCreate(led_task, "led_ctrl", 2048, NULL, 5, &s_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_FAIL;
    }

    s_initialized = true;
    s_pattern = LED_PATTERN_OFF;

    ESP_LOGI(TAG, "LED control initialized (R=GPIO%d, G=GPIO%d, Y=GPIO%d)",
             BOARD_LED_RED_GPIO, BOARD_LED_GREEN_GPIO, BOARD_LED_YELLOW_GPIO);
    return ESP_OK;
}

void led_control_set_pattern(led_pattern_t pattern)
{
    if (pattern < LED_PATTERN_OFF || pattern > LED_PATTERN_CLOSED) {
        ESP_LOGE(TAG, "invalid pattern %d", pattern);
        return;
    }

    s_pattern = pattern;

    /* 如果当前任务是挂起状态（OFF 模式），恢复它 */
    if (s_task_handle != NULL && s_pattern != LED_PATTERN_OFF) {
        vTaskResume(s_task_handle);
    }

    ESP_LOGI(TAG, "set pattern %d", pattern);
}

led_pattern_t led_control_get_pattern(void)
{
    return s_pattern;
}
