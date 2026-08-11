#include "display_status.h"

#include <stdbool.h>

#include "esp_log.h"
#include "gc9a01.h"

static const char *TAG = "display_status";

typedef struct {
    const char *lid;
    const char *voice;
    uint16_t accent;
} display_status_view_t;

static bool s_ready;

static esp_err_t display_status_draw(const display_status_view_t *view)
{
    esp_err_t ret = gc9a01_fill(GC9A01_COLOR_DARKBLUE);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gc9a01_fill_rect(20, 24, 219, 55, view->accent);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_draw_string(3, 9, "VOICE LOCKBOX", GC9A01_COLOR_WHITE,
                             view->accent);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gc9a01_draw_string(6, 10, "LID STATUS", GC9A01_COLOR_LGRAY,
                             GC9A01_COLOR_DARKBLUE);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_draw_string(8, 11, view->lid, view->accent,
                             GC9A01_COLOR_DARKBLUE);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gc9a01_fill_rect(20, 166, 219, 167, GC9A01_COLOR_GRAY);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gc9a01_draw_string(11, 8, "VOICE STATUS", GC9A01_COLOR_LGRAY,
                             GC9A01_COLOR_DARKBLUE);
    if (ret != ESP_OK) {
        return ret;
    }
    return gc9a01_draw_string(13, 7, view->voice, GC9A01_COLOR_WHITE,
                              GC9A01_COLOR_DARKBLUE);
}

static void display_status_show(const display_status_view_t *view)
{
    if (!s_ready) {
        return;
    }

    esp_err_t ret = display_status_draw(view);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "status update failed: %s", esp_err_to_name(ret));
    }
}

esp_err_t display_status_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t ret = gc9a01_init();
    if (ret != ESP_OK) {
        return ret;
    }

    s_ready = true;
    display_status_show(&(display_status_view_t) {
        .lid = "CLOSED",
        .voice = "STARTING",
        .accent = GC9A01_COLOR_CYAN,
    });
    ESP_LOGI(TAG, "GC9A01 status display ready");
    return ESP_OK;
}

void display_status_show_listening(void)
{
    display_status_show(&(display_status_view_t) {
        .lid = "CLOSED",
        .voice = "LISTENING",
        .accent = GC9A01_COLOR_GREEN,
    });
}

void display_status_show_opening(void)
{
    display_status_show(&(display_status_view_t) {
        .lid = "OPENING",
        .voice = "VERIFIED",
        .accent = GC9A01_COLOR_YELLOW,
    });
}

void display_status_show_opened(void)
{
    display_status_show(&(display_status_view_t) {
        .lid = "OPEN",
        .voice = "LISTENING",
        .accent = GC9A01_COLOR_GREEN,
    });
}

void display_status_show_error(const char *message)
{
    display_status_show(&(display_status_view_t) {
        .lid = "CLOSED",
        .voice = message ? message : "ERROR",
        .accent = GC9A01_COLOR_RED,
    });
}
