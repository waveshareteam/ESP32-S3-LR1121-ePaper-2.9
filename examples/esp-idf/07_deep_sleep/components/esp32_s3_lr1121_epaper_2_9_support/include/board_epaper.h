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

typedef struct {
    epd_handle_t epd;
    lv_display_t *display;
    epd_panel_info_t info;
} board_epaper_t;

esp_err_t board_epaper_start(board_epaper_t *out_panel);
esp_err_t board_epaper_refresh(bool full_refresh);
bool board_epaper_lock(int32_t timeout_ms);
void board_epaper_unlock(void);
esp_err_t board_epaper_show_lines(const char *title, const char *const *lines, size_t line_count, bool full_refresh);
esp_err_t board_epaper_sleep(void);

#ifdef __cplusplus
}
#endif
