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

/*
 * Board e-paper adapter used by the asynchronous multicast status UI.
 * LVGL access is serialized here, while physical transfers also take the
 * board-wide shared SPI lock.
 */

/* Non-owning snapshot of the component-managed panel and LVGL display. */
typedef struct {
    epd_handle_t epd;
    lv_display_t *display;
    epd_panel_info_t info;
} board_epaper_t;

/* Starts the panel once, or wakes it and returns the existing handles. */
esp_err_t board_epaper_start(board_epaper_t *out_panel);

/* false follows one FULL plus ten FAST updates; true forces a FULL update. */
esp_err_t board_epaper_refresh(bool force_full_refresh);

/* Serializes LVGL object and framebuffer access; negative timeout waits forever. */
bool board_epaper_lock(int32_t timeout_ms);
void board_epaper_unlock(void);

/* Rebuilds the fixed status layout and refreshes up to seven text rows. */
esp_err_t board_epaper_show_lines(const char *title, const char *const *lines,
                                  size_t line_count, bool force_full_refresh);

/* Sleeps only the panel controller; the LR1121-shared rail remains enabled. */
esp_err_t board_epaper_sleep(void);

#ifdef __cplusplus
}
#endif
