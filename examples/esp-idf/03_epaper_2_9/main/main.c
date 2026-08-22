#include <stdio.h>

#include "board_epaper.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epaper_test";

void app_main(void)
{
    board_epaper_t panel = {0};
    ESP_ERROR_CHECK(board_epaper_start(&panel));

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
}