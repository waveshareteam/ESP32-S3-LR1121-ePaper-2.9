#include "photo_display.h"
#include "sdkconfig.h"
#include "esp_check.h"

static const char *TAG = "epaper_photo";

#if CONFIG_EPAPER_DEMO_PHOTO_HF
#define PHOTO_VARIANT "HF"
#else
#define PHOTO_VARIANT "LF"
#endif

#if CONFIG_EPAPER_PHOTO_GRAY4_V2
/* Packed native 128x296 pixels: 0=black, 1=dark, 2=light, 3=white. */
static const uint8_t photo_pixels[] = {
#if CONFIG_EPAPER_DEMO_PHOTO_HF
#include "photo_hf_4gray.inc"
#else
#include "photo_lf_4gray.inc"
#endif
};
_Static_assert(sizeof(photo_pixels) == 128 * 296 / 4,
               "Native four-gray artwork must contain 9472 bytes");
#else
/* LVGL I1: two BGRA palette entries, then MSB-first row-major pixels. */
static const uint8_t photo_pixels[] = {
    0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff,
#if CONFIG_EPAPER_DEMO_PHOTO_HF
#include "photo_hf.inc"
#elif CONFIG_EPAPER_DEMO_PHOTO_LF
#include "photo_lf.inc"
#else
#error "Photo display requires an explicitly selected HF or LF photo mode"
#endif
};

_Static_assert(sizeof(photo_pixels) == 8 + 296 * 128 / 8,
               "Photo artwork must be 296x128, 1 bit per pixel");

static const lv_image_dsc_t photo_image = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_I1,
        .w = 296,
        .h = 128,
        .stride = 296 / 8,
    },
    .data_size = sizeof(photo_pixels),
    .data = photo_pixels,
};
#endif

esp_err_t photo_display_show(const board_epaper_t *panel)
{
    ESP_RETURN_ON_FALSE(panel && panel->display, ESP_ERR_INVALID_ARG, TAG,
                        "panel is not initialized");
#if CONFIG_EPAPER_PHOTO_GRAY4_V2
    ESP_LOGI(TAG, "photo v2: %s / native 4-gray, Waveshare 2.9inch V2", PHOTO_VARIANT);
    return board_epaper_show_gray4_v2(photo_pixels, sizeof(photo_pixels));
#else
    ESP_LOGI(TAG, "photo v2: %s / B/W dither", PHOTO_VARIANT);
    ESP_RETURN_ON_FALSE(board_epaper_lock(-1), ESP_ERR_TIMEOUT, TAG,
                        "LVGL lock timeout");
    if (lv_display_get_horizontal_resolution(panel->display) != 296 ||
        lv_display_get_vertical_resolution(panel->display) != 128) {
        board_epaper_unlock();
        ESP_LOGE(TAG, "photo artwork requires a 296x128 logical display");
        return ESP_ERR_NOT_SUPPORTED;
    }
    lv_obj_t *screen = lv_display_get_screen_active(panel->display);
    if (!screen) {
        board_epaper_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    lv_obj_clean(screen);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *image = lv_image_create(screen);
    if (!image) {
        board_epaper_unlock();
        return ESP_ERR_NO_MEM;
    }
    lv_image_set_src(image, &photo_image);
    lv_obj_set_pos(image, 0, 0);
    board_epaper_unlock();

    /* No timer or radio task is started by this example's photo path. */
    return board_epaper_refresh(true);
#endif
}
