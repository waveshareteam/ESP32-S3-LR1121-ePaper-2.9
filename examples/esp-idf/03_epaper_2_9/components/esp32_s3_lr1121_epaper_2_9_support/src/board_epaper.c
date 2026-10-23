#include "board_epaper.h"
#include "epaper_ui_font.h"

#include <stdlib.h>
#include <string.h>
#include "board_support.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "board_epaper";

#define LVGL_TICK_PERIOD_MS 5
#define BOARD_EPAPER_RENDER_LINES 10
#define BOARD_EPAPER_MAX_LINES 7U
#define BOARD_EPAPER_FAST_REFRESH_LIMIT 10U

static board_epaper_t s_panel;
static bool s_started;
static bool s_sleeping;
static bool s_lvgl_inited;
static SemaphoreHandle_t s_lvgl_mux;
static esp_timer_handle_t s_lvgl_tick_timer;
static uint8_t *s_lvgl_buf;
static esp_err_t s_last_flush_ret = ESP_OK;
static bool s_flush_completed;
static epd_update_mode_t s_flush_mode = EPD_UPDATE_FULL;
static uint8_t s_fast_refreshes_remaining;

static esp_err_t board_epaper_create_panel(epd_handle_t *panel)
{
    board_epaper_hw_config_t hw = {0};
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG, "panel output is null");
    *panel = NULL;
    ESP_RETURN_ON_ERROR(board_support_prepare(), TAG,
                        "prepare board support failed");
    ESP_RETURN_ON_ERROR(board_epaper_get_hw_config(&hw), TAG,
                        "get e-paper board config failed");

    const epd_config_t config = {
        .pins = {
            .busy = hw.busy,
            .rst = hw.reset,
            .dc = hw.dc,
            .cs = hw.cs,
            .sck = hw.sck,
            .mosi = hw.mosi,
        },
        .spi = {
            .host = hw.spi_host,
            .speed_hz = hw.spi_speed_hz,
        },
        .panel = {
            .type = EPD_PANEL_SSD16XX_290,
            .width = 0,
            .height = 0,
            .mirror_x = false,
            .mirror_y = false,
            .rotation = 0,
        },
    };

    esp_err_t ret = board_epaper_power_set(true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "e-paper power on failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (!board_spi_lock(BOARD_SPI_OWNER_EPAPER, -1)) {
        ret = ESP_ERR_TIMEOUT;
        ESP_LOGE(TAG, "SPI lock timeout");
        goto power_off;
    }

    ret = epd_init(&config, panel);
    board_spi_unlock(BOARD_SPI_OWNER_EPAPER);
    if (ret == ESP_OK) {
        return ESP_OK;
    }

power_off:
    {
        esp_err_t power_ret = board_epaper_power_set(false);
        if (power_ret != ESP_OK) {
            ESP_LOGE(TAG, "e-paper rollback power off failed: %s", esp_err_to_name(power_ret));
            if (ret == ESP_OK) {
                ret = power_ret;
            }
        }
    }
    return ret;
}
static esp_err_t board_epaper_release_panel_locked(void)
{
    esp_err_t first_error = ESP_OK;

    if (s_panel.display) {
        lv_display_delete(s_panel.display);
        s_panel.display = NULL;
    }
    free(s_lvgl_buf);
    s_lvgl_buf = NULL;

    if (s_panel.epd) {
        if (board_spi_lock(BOARD_SPI_OWNER_EPAPER, -1)) {
            esp_err_t ret = epd_deinit(s_panel.epd);
            board_spi_unlock(BOARD_SPI_OWNER_EPAPER);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "deinitialize e-paper failed: %s", esp_err_to_name(ret));
                first_error = ret;
            }
            s_panel.epd = NULL;
        } else {
            ESP_LOGE(TAG, "SPI lock timeout while deinitializing e-paper");
            first_error = ESP_ERR_TIMEOUT;
        }
    }

    esp_err_t power_ret = board_epaper_power_set(false);
    if (power_ret != ESP_OK) {
        ESP_LOGE(TAG, "e-paper rollback power off failed: %s", esp_err_to_name(power_ret));
        if (first_error == ESP_OK) {
            first_error = power_ret;
        }
    }

    if (!s_panel.epd) {
        memset(&s_panel, 0, sizeof(s_panel));
    }
    s_started = false;
    s_sleeping = false;
    s_last_flush_ret = ESP_OK;
    s_flush_completed = false;
    s_flush_mode = EPD_UPDATE_FULL;
    s_fast_refreshes_remaining = 0;
    return first_error;
}

static esp_err_t board_epaper_wake_locked(void)
{
    esp_err_t ret = board_epaper_power_set(true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "e-paper wake power on failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (!board_spi_lock(BOARD_SPI_OWNER_EPAPER, -1)) {
        ret = ESP_ERR_TIMEOUT;
        ESP_LOGE(TAG, "SPI lock timeout while waking e-paper");
    } else {
        ret = epd_wake(s_panel.epd);
        board_spi_unlock(BOARD_SPI_OWNER_EPAPER);
    }

    if (ret != ESP_OK) {
        esp_err_t power_ret = board_epaper_power_set(false);
        if (power_ret != ESP_OK) {
            ESP_LOGE(TAG, "e-paper wake rollback power off failed: %s",
                     esp_err_to_name(power_ret));
        }
        return ret;
    }

    s_sleeping = false;
    s_last_flush_ret = ESP_OK;
    s_flush_completed = false;
    s_flush_mode = EPD_UPDATE_FULL;
    s_fast_refreshes_remaining = 0;
    ESP_LOGI(TAG, "e-paper woke from sleep; next framebuffer update uses full refresh");
    return ESP_OK;
}

static void lv_tick_timer_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static inline bool board_epaper_rgb565_is_black(uint16_t color)
{
    uint8_t r = (uint8_t)((((color >> 11) & 0x1F) * 255) / 31);
    uint8_t g = (uint8_t)((((color >> 5) & 0x3F) * 255) / 63);
    uint8_t b = (uint8_t)(((color & 0x1F) * 255) / 31);
    uint16_t gray = (uint16_t)((r * 30 + g * 59 + b * 11) / 100);
    return gray < 128;
}

static inline void board_epaper_set_bw_pixel(uint8_t *fb, uint16_t x, uint16_t y,
                                             uint16_t width, bool black)
{
    uint32_t byte_idx = y * (width / 8) + (x / 8);
    uint8_t bit = 7 - (x % 8);
    if (black) {
        fb[byte_idx] &= ~(1U << bit);
    } else {
        fb[byte_idx] |= (1U << bit);
    }
}

static bool board_epaper_map_lvgl_to_panel(int32_t x, int32_t y, uint16_t *panel_x, uint16_t *panel_y)
{
    int32_t phys_x = y;
    int32_t phys_y = (int32_t)s_panel.info.height - 1 - x;

    if (phys_x < 0 || phys_x >= s_panel.info.width ||
        phys_y < 0 || phys_y >= s_panel.info.height) {
        return false;
    }

    *panel_x = (uint16_t)phys_x;
    *panel_y = (uint16_t)phys_y;
    return true;
}

static void board_epaper_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint8_t *fb = epd_get_framebuffer(s_panel.epd);
    if (!fb) {
        s_last_flush_ret = ESP_ERR_INVALID_STATE;
        if (lv_display_flush_is_last(disp)) {
            s_flush_completed = true;
        }
        lv_display_flush_ready(disp);
        return;
    }

    const uint16_t *src = (const uint16_t *)px_map;
    for (int32_t y = area->y1; y <= area->y2; y++) {
        for (int32_t x = area->x1; x <= area->x2; x++) {
            uint16_t panel_x;
            uint16_t panel_y;
            bool black = board_epaper_rgb565_is_black(*src++);
            if (board_epaper_map_lvgl_to_panel(x, y, &panel_x, &panel_y)) {
                board_epaper_set_bw_pixel(fb, panel_x, panel_y, s_panel.info.width, black);
            }
        }
    }

    if (lv_display_flush_is_last(disp)) {
        s_last_flush_ret = epd_flush_framebuffer(s_panel.epd, s_flush_mode);
        s_flush_completed = true;
        if (s_last_flush_ret != ESP_OK) {
            ESP_LOGE(TAG, "flush e-paper framebuffer failed: %s", esp_err_to_name(s_last_flush_ret));
        }
    }

    lv_display_flush_ready(disp);
}

static esp_err_t board_epaper_lvgl_init_once(void)
{
    if (!s_lvgl_inited) {
        lv_init();
        s_lvgl_inited = true;
    }

    if (!s_lvgl_mux) {
        s_lvgl_mux = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lvgl_mux, ESP_ERR_NO_MEM, TAG, "create LVGL mutex failed");
    }

    if (!s_lvgl_tick_timer) {
        const esp_timer_create_args_t tick_args = {
            .callback = lv_tick_timer_cb,
            .name = "lvgl_tick",
        };
        esp_err_t ret = esp_timer_create(&tick_args, &s_lvgl_tick_timer);
        ESP_RETURN_ON_ERROR(ret, TAG, "create LVGL tick timer failed");
        ret = esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000);
        if (ret != ESP_OK) {
            (void)esp_timer_delete(s_lvgl_tick_timer);
            s_lvgl_tick_timer = NULL;
            ESP_LOGE(TAG, "start LVGL tick timer failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    return ESP_OK;
}

bool board_epaper_lock(int32_t timeout_ms)
{
    if (!s_lvgl_mux && board_epaper_lvgl_init_once() != ESP_OK) {
        return false;
    }

    TickType_t ticks = portMAX_DELAY;
    if (timeout_ms >= 0) {
        ticks = pdMS_TO_TICKS((uint32_t)timeout_ms);
    }
    return xSemaphoreTake(s_lvgl_mux, ticks) == pdTRUE;
}

void board_epaper_unlock(void)
{
    if (s_lvgl_mux) {
        xSemaphoreGive(s_lvgl_mux);
    }
}

esp_err_t board_epaper_start(board_epaper_t *out_panel)
{
    ESP_RETURN_ON_ERROR(board_epaper_lvgl_init_once(), TAG, "LVGL init failed");
    ESP_RETURN_ON_FALSE(board_epaper_lock(-1), ESP_ERR_TIMEOUT, TAG, "LVGL lock timeout");

    esp_err_t ret = ESP_OK;
    if (s_started) {
        if (s_sleeping) {
            ret = board_epaper_wake_locked();
        }
        goto done;
    }

    if (s_panel.epd || s_panel.display || s_lvgl_buf) {
        ret = board_epaper_release_panel_locked();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "release incomplete e-paper state failed: %s", esp_err_to_name(ret));
            goto done;
        }
    }

    ret = board_epaper_create_panel(&s_panel.epd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "initialize managed e-paper failed: %s", esp_err_to_name(ret));
        goto done;
    }

    ret = epd_get_info(s_panel.epd, &s_panel.info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "get panel info failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    ESP_LOGI(TAG, "e-paper panel: %ux%u buffer=%lu",
             (unsigned int)s_panel.info.width,
             (unsigned int)s_panel.info.height,
             (unsigned long)s_panel.info.buffer_size);

    uint32_t logical_w = s_panel.info.height;
    uint32_t logical_h = s_panel.info.width;
    uint32_t lvgl_buf_size = logical_w * BOARD_EPAPER_RENDER_LINES * sizeof(uint16_t);

    s_lvgl_buf = heap_caps_malloc(lvgl_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_lvgl_buf) {
        s_lvgl_buf = malloc(lvgl_buf_size);
    }
    if (!s_lvgl_buf) {
        ret = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "alloc LVGL render buffer failed");
        goto fail;
    }

    s_panel.display = lv_display_create((int32_t)logical_w, (int32_t)logical_h);
    if (!s_panel.display) {
        ret = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "create LVGL display failed");
        goto fail;
    }
    lv_display_set_flush_cb(s_panel.display, board_epaper_lvgl_flush_cb);
    lv_display_set_buffers(s_panel.display, s_lvgl_buf, NULL, lvgl_buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    ESP_LOGI(TAG, "LVGL display initialized: %lux%lu, render-buffer=partial, buffer=%lu bytes (%u lines)",
             (unsigned long)logical_w,
             (unsigned long)logical_h,
             (unsigned long)lvgl_buf_size,
             (unsigned int)BOARD_EPAPER_RENDER_LINES);
    s_flush_mode = EPD_UPDATE_FULL;
    s_fast_refreshes_remaining = 0;
    ESP_LOGI(TAG, "e-paper logical display: %ldx%ld, panel refresh=full/fast",             (long)lv_display_get_horizontal_resolution(s_panel.display),
             (long)lv_display_get_vertical_resolution(s_panel.display));

    s_started = true;
    s_sleeping = false;
    goto done;

fail:
    {
        esp_err_t cleanup_ret = board_epaper_release_panel_locked();
        if (cleanup_ret != ESP_OK) {
            ESP_LOGE(TAG, "e-paper initialization rollback incomplete: %s",
                     esp_err_to_name(cleanup_ret));
            ret = cleanup_ret;
        }
    }

done:
    if (ret == ESP_OK && out_panel) {
        *out_panel = s_panel;
    }
    board_epaper_unlock();
    return ret;
}

static epd_update_mode_t board_epaper_select_update_mode(bool force_full_refresh)
{
    return force_full_refresh || s_fast_refreshes_remaining == 0
               ? EPD_UPDATE_FULL
               : EPD_UPDATE_FAST;
}

static esp_err_t board_epaper_refresh_locked(epd_update_mode_t update_mode)
{
    ESP_RETURN_ON_FALSE(s_started && !s_sleeping && s_panel.display && s_panel.epd,
                        ESP_ERR_INVALID_STATE, TAG, "e-paper not ready");
    ESP_RETURN_ON_FALSE(update_mode == EPD_UPDATE_FULL ||
                            update_mode == EPD_UPDATE_FAST,
                        ESP_ERR_INVALID_ARG, TAG, "unsupported update mode");

    uint8_t *fb = epd_get_framebuffer(s_panel.epd);
    ESP_RETURN_ON_FALSE(fb, ESP_ERR_INVALID_STATE, TAG, "e-paper framebuffer unavailable");
    memset(fb, 0xFF, s_panel.info.buffer_size);

    ESP_RETURN_ON_FALSE(board_spi_lock(BOARD_SPI_OWNER_EPAPER, -1),
                        ESP_ERR_TIMEOUT, TAG, "SPI lock timeout");

    s_last_flush_ret = ESP_OK;
    s_flush_completed = false;
    s_flush_mode = update_mode;
    lv_obj_t *screen = lv_display_get_screen_active(s_panel.display);
    if (!screen) {
        board_spi_unlock(BOARD_SPI_OWNER_EPAPER);
        ESP_LOGE(TAG, "LVGL active screen unavailable");
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_invalidate(screen);
    lv_refr_now(s_panel.display);
    board_spi_unlock(BOARD_SPI_OWNER_EPAPER);

    if (!s_flush_completed && s_last_flush_ret == ESP_OK) {
        s_last_flush_ret = ESP_FAIL;
        ESP_LOGE(TAG, "LVGL refresh completed without a final e-paper flush");
    }
    if (s_last_flush_ret != ESP_OK) {
        s_fast_refreshes_remaining = 0;
    }
    ESP_RETURN_ON_ERROR(s_last_flush_ret, TAG, "refresh e-paper failed");

    if (update_mode == EPD_UPDATE_FULL) {
        s_fast_refreshes_remaining = BOARD_EPAPER_FAST_REFRESH_LIMIT;
        ESP_LOGI(TAG, "e-paper full refresh complete");
    } else {
        --s_fast_refreshes_remaining;
        ESP_LOGI(TAG, "e-paper fast refresh complete (%u remaining)",
                 (unsigned int)s_fast_refreshes_remaining);
    }
    return ESP_OK;
}

esp_err_t board_epaper_refresh(bool force_full_refresh)
{
    ESP_RETURN_ON_FALSE(board_epaper_lock(-1), ESP_ERR_TIMEOUT, TAG, "LVGL lock timeout");
    const epd_update_mode_t update_mode =
        board_epaper_select_update_mode(force_full_refresh);
    esp_err_t ret = board_epaper_refresh_locked(update_mode);
    board_epaper_unlock();
    return ret;
}

#define BOARD_EPAPER_FONT (&lv_font_terminus_14_bold)

static lv_obj_t *panel_rect(lv_obj_t *parent, int x, int y, int w, int h,
                            uint8_t border_width, lv_color_t bg_color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, bg_color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_black(), 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *label_add(lv_obj_t *parent, const char *text, int x, int y, int w,
                           const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(label, w, lv_font_get_line_height(font) + 1);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, x, y);
    return label;
}

static void label_set_aligned(lv_obj_t *parent, const char *text, int x, int y, int w,
                              const lv_font_t *font, lv_color_t color, lv_text_align_t align)
{
    lv_obj_t *label = label_add(parent, text, x, y, w, font, color);
    lv_obj_set_style_text_align(label, align, 0);
}

esp_err_t board_epaper_show_lines(const char *title, const char *const *lines, size_t line_count, bool force_full_refresh)
{
    ESP_RETURN_ON_FALSE(line_count <= BOARD_EPAPER_MAX_LINES, ESP_ERR_INVALID_ARG, TAG,
                        "show_lines supports at most %u lines", (unsigned)BOARD_EPAPER_MAX_LINES);
    ESP_RETURN_ON_FALSE(lines || line_count == 0, ESP_ERR_INVALID_ARG, TAG,
                        "lines is null");
    ESP_RETURN_ON_ERROR(board_epaper_start(NULL), TAG, "start e-paper failed");
    ESP_RETURN_ON_FALSE(board_epaper_lock(-1), ESP_ERR_TIMEOUT, TAG, "LVGL lock timeout");

    const epd_update_mode_t update_mode =
        board_epaper_select_update_mode(force_full_refresh);

    lv_obj_t *scr = lv_display_get_screen_active(s_panel.display);
    if (!scr) {
        board_epaper_unlock();
        ESP_LOGE(TAG, "LVGL active screen unavailable");
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    int32_t w = lv_display_get_horizontal_resolution(s_panel.display);
    int32_t h = lv_display_get_vertical_resolution(s_panel.display);
    if (w <= 0 || h <= 0) {
        w = 296;
        h = 128;
    }

    const int32_t margin = 5;
    const int32_t header_h = 22;
    const int32_t body_y = header_h + 3;
    const int32_t body_w = w - margin * 2;
    const int32_t body_h = h - body_y - margin;
    const size_t shown_lines = line_count > 0 ? line_count : 1;

    panel_rect(scr, 0, 0, w, h, 1, lv_color_white());
    lv_obj_t *header = panel_rect(scr, 0, 0, w, header_h, 0, lv_color_black());
    label_add(header, title ? title : "EPD 2.9", 7, 3, w - 72,
              BOARD_EPAPER_FONT, lv_color_white());
    lv_obj_t *mode_badge = panel_rect(header, w - 57, 3, 50, 16, 0, lv_color_white());
    label_set_aligned(mode_badge,
                      update_mode == EPD_UPDATE_FULL ? "FULL" : "FAST",
                      2, 1, 46, BOARD_EPAPER_FONT,
                      lv_color_black(), LV_TEXT_ALIGN_CENTER);

    lv_obj_t *body = panel_rect(scr, margin, body_y, body_w, body_h, 1, lv_color_white());
    const int32_t content_h = body_h - 2;
    const int32_t font_h = lv_font_get_line_height(BOARD_EPAPER_FONT);

    for (size_t i = 0; i < shown_lines; i++) {
        int32_t row_y = 1 + (int32_t)(content_h * i / shown_lines);
        int32_t next_row_y = 1 + (int32_t)(content_h * (i + 1) / shown_lines);
        int32_t row_h = next_row_y - row_y;
        if (i > 0) {
            panel_rect(body, 5, row_y, body_w - 10, 1, 0, lv_color_black());
        }

        const char *text = line_count == 0 ? "No data" : (lines[i] ? lines[i] : "");
        int32_t label_y = row_y;
        if (row_h > font_h) {
            label_y += (row_h - font_h) / 2;
        }
        label_add(body, text, 6, label_y, body_w - 12, BOARD_EPAPER_FONT, lv_color_black());
    }

    esp_err_t ret = board_epaper_refresh_locked(update_mode);
    board_epaper_unlock();
    return ret;
}

esp_err_t board_epaper_sleep(void)
{
    ESP_RETURN_ON_FALSE(board_epaper_lock(-1), ESP_ERR_TIMEOUT, TAG, "LVGL lock timeout");

    if (!s_started) {
        esp_err_t power_ret = board_epaper_power_set(false);
        board_epaper_unlock();
        return power_ret;
    }

    esp_err_t ret = ESP_OK;
    if (!s_sleeping) {
        if (!board_spi_lock(BOARD_SPI_OWNER_EPAPER, -1)) {
            ret = ESP_ERR_TIMEOUT;
            ESP_LOGE(TAG, "SPI lock timeout while sleeping e-paper");
        } else {
            ret = epd_sleep(s_panel.epd);
            board_spi_unlock(BOARD_SPI_OWNER_EPAPER);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "e-paper sleep failed: %s", esp_err_to_name(ret));
            } else {
                s_sleeping = true;
                s_flush_completed = false;
            }
        }
    }

    esp_err_t power_ret = ESP_OK;
    if (ret == ESP_OK) {
        power_ret = board_epaper_power_set(false);
        if (power_ret != ESP_OK) {
            ESP_LOGE(TAG, "e-paper power off failed: %s", esp_err_to_name(power_ret));
        }
    }
    board_epaper_unlock();

    return ret != ESP_OK ? ret : power_ret;
}
