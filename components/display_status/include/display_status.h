#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the GC9A01 status surface.
 *
 * The initial screen shows a closed lid and a starting voice service.
 */
esp_err_t display_status_init(void);

/**
 * @brief Show the normal voice-listening and closed-lid state.
 */
void display_status_show_listening(void);

/**
 * @brief Show that a verified wakeup is moving the lid.
 */
void display_status_show_opening(void);

/**
 * @brief Show that the lid-open command has completed.
 *
 * This reports the servo command state, not a physical lid-position sensor.
 */
void display_status_show_opened(void);

/**
 * @brief Show that the local button is closing the lid.
 */
void display_status_show_closing(void);

/**
 * @brief Show the normal voice-listening and closed-lid state.
 */
void display_status_show_closed(void);

/**
 * @brief Show a short startup error while keeping the current lid state.
 */
void display_status_show_error(const char *message);

#ifdef __cplusplus
}
#endif
