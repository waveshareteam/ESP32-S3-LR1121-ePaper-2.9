#include <stdio.h>

#include "sdkconfig.h"
#include "board_epaper.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epaper_test";

void app_main(void)
{
    board_epaper_t panel = {0};
    ESP_ERROR_CHECK(board_epaper_start(&panel));

#if CONFIG_EPAPER_DEMO_CLEAR
    ESP_ERROR_CHECK(board_epaper_lock(-1) ? ESP_OK : ESP_ERR_TIMEOUT);
    lv_obj_t *screen = lv_display_get_screen_active(panel.display);
    if (!screen) {
        board_epaper_unlock();
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
        return;
    }
    lv_obj_clean(screen);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    board_epaper_unlock();

    ESP_ERROR_CHECK(board_epaper_refresh(true));
    ESP_ERROR_CHECK(board_epaper_sleep());
    ESP_LOGI(TAG, "screen cleared to white; panel sleeping; periodic refresh disabled");
    return;
#else
    char panel_line[24];
    snprintf(panel_line, sizeof(panel_line), "%ux%u", panel.info.width, panel.info.height);
    char count_line[24] = "Update: 0";
    const char *lines[] = {
        "Policy: FULL + 10 FAST",
        panel_line,
        "Status: ready",
        "Interval: 30 s",
        count_line,
        "1-bit black/white",
    };
    ESP_ERROR_CHECK(board_epaper_show_lines("EPD 2.9", lines, 6, false));
    ESP_LOGI(TAG, "e-paper test screen shown");

    uint32_t count = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        count++;

        snprintf(count_line, sizeof(count_line), "Update: %lu", (unsigned long)count);
        ESP_ERROR_CHECK(board_epaper_show_lines("EPD 2.9", lines, 6, false));
    }
#endif
}
