#include "app_tests.h"

#include <stdio.h>

#include "board_support.h"

static bool rtc_fields_valid(const board_rtc_time_t *time)
{
    return time->year >= 2000 && time->year <= 2099 &&
           time->month >= 1 && time->month <= 12 &&
           time->day >= 1 && time->day <= 31 &&
           time->weekday <= 6 && time->hour <= 23 &&
           time->minute <= 59 && time->second <= 59;
}

esp_err_t test_rtc_run(app_test_result_t *result)
{
    esp_err_t ret = board_rtc_init();
    if (ret != ESP_OK) {
        app_result_set(result, "RTC", APP_TEST_FAIL,
                       "PCF85063A init fail", esp_err_to_name(ret));
        return ret;
    }

    board_rtc_time_t now = {0};
    ret = board_rtc_get_time(&now);
    if (ret != ESP_OK || !rtc_fields_valid(&now)) {
        if (ret == ESP_OK) {
            ret = ESP_ERR_INVALID_RESPONSE;
        }
        app_result_set(result, "RTC", APP_TEST_FAIL,
                       "PCF85063A read fail", esp_err_to_name(ret));
        return ret;
    }
    board_rtc_interrupt_status_t irq = {0};
    ret = board_rtc_get_interrupt_status(&irq);
    if (ret != ESP_OK) {
        app_result_set(result, "RTC", APP_TEST_FAIL,
                       "RTC IRQ read fail", esp_err_to_name(ret));
        return ret;
    }

    char line1[48];
    char line2[48];
    snprintf(line1, sizeof(line1), "RTC %04u-%02u-%02u",
             now.year, now.month, now.day);
    snprintf(line2, sizeof(line2),
             "%02u:%02u:%02u VL:%d IRQ%d C2:%02X",
             now.hour, now.minute, now.second, now.voltage_low,
             irq.gpio_level, irq.control2);
    app_result_set(result, "RTC",
                   now.voltage_low ? APP_TEST_NOT_TESTED : APP_TEST_PASS,
                   line1, line2);
    return ESP_OK;
}
