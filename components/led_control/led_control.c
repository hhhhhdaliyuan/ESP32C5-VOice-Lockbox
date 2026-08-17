#include "led_control.h"

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led_control";

#define LED_SEQUENCE_STEP_MS 160
#define LED_FAILURE_FLASH_MS 120
#define LED_TASK_STACK_SIZE 2048
#define LED_TASK_PRIORITY 5

static volatile led_pattern_t s_pattern = LED_PATTERN_OFF;
static TaskHandle_t s_task_handle;
static bool s_initialized;

static bool led_pins_assigned(void)
{
    return BOARD_LED_RED_GPIO != GPIO_NUM_NC
           && BOARD_LED_GREEN_GPIO != GPIO_NUM_NC
           && BOARD_LED_BLUE_GPIO != GPIO_NUM_NC
           && BOARD_LED_RED_GPIO != BOARD_LED_GREEN_GPIO
           && BOARD_LED_RED_GPIO != BOARD_LED_BLUE_GPIO
           && BOARD_LED_GREEN_GPIO != BOARD_LED_BLUE_GPIO;
}

static void led_write(bool red_on, bool green_on, bool blue_on)
{
    gpio_set_level(BOARD_LED_RED_GPIO, red_on ? 0 : 1);
    gpio_set_level(BOARD_LED_GREEN_GPIO, green_on ? 0 : 1);
    gpio_set_level(BOARD_LED_BLUE_GPIO, blue_on ? 0 : 1);
}

static void led_all_off(void)
{
    led_write(false, false, false);
}

static void led_task(void *arg)
{
    (void)arg;

    led_pattern_t previous = LED_PATTERN_OFF;
    uint8_t step = 0;
    bool failure_on = false;

    while (true) {
        led_pattern_t pattern = s_pattern;
        if (pattern != previous) {
            previous = pattern;
            step = 0;
            failure_on = false;
        }

        switch (pattern) {
        case LED_PATTERN_OFF:
            led_all_off();
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            break;

        case LED_PATTERN_OPENING:
            switch (step) {
            case 0:
                led_write(true, false, false);
                break;
            case 1:
                led_write(false, true, false);
                break;
            default:
                led_write(false, false, true);
                break;
            }
            step = (uint8_t)((step + 1U) % 3U);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LED_SEQUENCE_STEP_MS));
            break;

        case LED_PATTERN_CLOSING:
            switch (step) {
            case 0:
                led_write(false, false, true);
                break;
            case 1:
                led_write(false, true, false);
                break;
            default:
                led_write(true, false, false);
                break;
            }
            step = (uint8_t)((step + 1U) % 3U);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LED_SEQUENCE_STEP_MS));
            break;

        case LED_PATTERN_OPEN_FAILED:
            failure_on = !failure_on;
            led_write(failure_on, false, false);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LED_FAILURE_FLASH_MS));
            break;

        default:
            s_pattern = LED_PATTERN_OFF;
            break;
        }
    }
}

esp_err_t led_control_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (!led_pins_assigned()) {
        ESP_LOGW(TAG, "RGB LED GPIOs are not assigned");
        return ESP_ERR_NOT_SUPPORTED;
    }

    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BOARD_LED_RED_GPIO)
                        | (1ULL << BOARD_LED_GREEN_GPIO)
                        | (1ULL << BOARD_LED_BLUE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RGB LED GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    led_all_off();
    if (xTaskCreate(led_task, "led_ctrl", LED_TASK_STACK_SIZE, NULL,
                    LED_TASK_PRIORITY, &s_task_handle) != pdPASS) {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    s_pattern = LED_PATTERN_OFF;
    ESP_LOGI(TAG, "RGB LED ready: red=GPIO%d green=GPIO%d blue=GPIO%d active-low",
             BOARD_LED_RED_GPIO, BOARD_LED_GREEN_GPIO, BOARD_LED_BLUE_GPIO);
    return ESP_OK;
}

void led_control_set_pattern(led_pattern_t pattern)
{
    if (!s_initialized) {
        return;
    }
    if (pattern < LED_PATTERN_OFF || pattern > LED_PATTERN_OPEN_FAILED) {
        ESP_LOGW(TAG, "invalid LED pattern: %d", pattern);
        return;
    }

    s_pattern = pattern;
    xTaskNotifyGive(s_task_handle);
}

led_pattern_t led_control_get_pattern(void)
{
    return s_pattern;
}