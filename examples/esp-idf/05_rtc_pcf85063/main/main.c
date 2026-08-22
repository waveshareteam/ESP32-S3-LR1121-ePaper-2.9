#include <stdio.h>
#include <stdint.h>

#include "board_epaper.h"
#include "board_support.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rtc_pcf85063a";

#define DISPLAY_REFRESH_INTERVAL_SEC 30

static bool rtc_time_invalid(const board_rtc_time_t *time)
{
    return time->voltage_low || time->year < 2024 || time->month == 0 || time->month > 12 ||
           time->day == 0 || time->day > 31 || time->hour > 23 || time->minute > 59 || time->second > 59;
}

void app_main(void)
{
    ESP_ERROR_CHECK(board_support_prepare());
    ESP_ERROR_CHECK(board_epaper_start(NULL));
    ESP_ERROR_CHECK(board_rtc_init());
    ESP_ERROR_CHECK(board_rtc_start_1hz_timer_interrupt());

    board_rtc_time_t now = {0};
    ESP_ERROR_CHECK(board_rtc_get_time(&now));
    if (rtc_time_invalid(&now)) {
        const board_rtc_time_t initial = {
            .year = 2026,
            .month = 1,
            .day = 1,
            .weekday = 4,
            .hour = 12,
            .minute = 0,
            .second = 0,
        };
        ESP_LOGW(TAG, "RTC invalid, set default time to 2026-01-01 12:00:00");
        ESP_ERROR_CHECK(board_rtc_set_time(&initial));
    }

    TickType_t next_display_refresh = xTaskGetTickCount();
    while (true) {
        ESP_ERROR_CHECK(board_rtc_get_time(&now));
        board_rtc_interrupt_status_t int_status = {0};
        ESP_ERROR_CHECK(board_rtc_get_interrupt_status(&int_status));
        ESP_ERROR_CHECK(board_rtc_clear_interrupt_flags());

        char l0[40];
        char l1[40];
        char l2[40];
        char l3[40];
        snprintf(l0, sizeof(l0), "%04u-%02u-%02u", now.year, now.month, now.day);
        snprintf(l1, sizeof(l1), "%02u:%02u:%02u", now.hour, now.minute, now.second);
        snprintf(l2, sizeof(l2), "Weekday: %u VL:%d", now.weekday, now.voltage_low);
        snprintf(l3, sizeof(l3), "INT level:%d cnt:%lu", int_status.gpio_level,
                 (unsigned long)int_status.count);
        const char *lines[] = {l0, l1, l2, l3, "PCF85063A 1Hz"};
        ESP_LOGI(TAG, "%s %s %s %s", l0, l1, l2, l3);

        TickType_t now_ticks = xTaskGetTickCount();
        if ((int32_t)(now_ticks - next_display_refresh) >= 0) {
            ESP_ERROR_CHECK(board_epaper_show_lines("RTC TEST", lines, 5, false));
            next_display_refresh = xTaskGetTickCount() +
                                   pdMS_TO_TICKS(DISPLAY_REFRESH_INTERVAL_SEC * 1000);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
