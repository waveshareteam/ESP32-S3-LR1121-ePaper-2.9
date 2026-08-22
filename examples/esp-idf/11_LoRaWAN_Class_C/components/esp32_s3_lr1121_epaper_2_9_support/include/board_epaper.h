/**
 * @file board_epaper.h
 * @brief Board-specific e-paper and LVGL display services.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "epaper.h"
#include "epaper_lvgl.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Handles returned by the initialized board display service. */
typedef struct {
    epd_handle_t epd;
    lv_display_t *display;
    epd_panel_info_t info;
} board_epaper_t;

/** Initializes or wakes the panel and optionally returns its handles. */
esp_err_t board_epaper_start(board_epaper_t *out_panel);

/** Refreshes the current LVGL screen; false cycles one full plus ten fast. */
esp_err_t board_epaper_refresh(bool force_full_refresh);

/** Acquires the display state gate for the requested timeout. */
bool board_epaper_lock(int32_t timeout_ms);

/** Releases the display state gate. */
void board_epaper_unlock(void);

/** Renders a title and up to seven text lines, then refreshes the panel. */
esp_err_t board_epaper_show_lines(const char *title, const char *const *lines,
                                  size_t line_count,
                                  bool force_full_refresh);

/** Sleeps the panel while keeping the LR1121 shared power rail enabled. */
esp_err_t board_epaper_sleep(void);

#ifdef __cplusplus
}
#endif
