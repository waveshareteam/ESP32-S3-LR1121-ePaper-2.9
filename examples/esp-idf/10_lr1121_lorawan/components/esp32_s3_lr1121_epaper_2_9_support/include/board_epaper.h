/*
 * Board-owned 2.9-inch e-paper and LVGL adapter.
 *
 * Public calls are synchronous and share SPI2 safely with the LR1121.
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

/** Borrowed handles and immutable panel information returned by start(). */
typedef struct {
    epd_handle_t epd;
    lv_display_t *display;
    epd_panel_info_t info;
} board_epaper_t;

/** Initialize or wake the singleton panel; optionally return borrowed handles. */
esp_err_t board_epaper_start(board_epaper_t *out_panel);
/**
 * Refresh the current LVGL screen.
 * false follows one FULL plus up to ten FAST updates; true forces FULL.
 */
/* false cycles FULL + 10 FAST; true forces FULL. */
esp_err_t board_epaper_refresh(bool force_full_refresh);
/** Lock LVGL state; a negative timeout waits indefinitely. */
bool board_epaper_lock(int32_t timeout_ms);
/** Release the LVGL state lock acquired by board_epaper_lock(). */
void board_epaper_unlock(void);
/** Replace the screen with a title and at most seven status lines. */
esp_err_t board_epaper_show_lines(const char *title, const char *const *lines,
                                  size_t line_count, bool force_full_refresh);
/** Put the panel into sleep and remove its power when possible. */
esp_err_t board_epaper_sleep(void);

#ifdef __cplusplus
}
#endif
