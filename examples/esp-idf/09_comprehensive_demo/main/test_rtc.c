#include "app_tests.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board_support.h"
#include "esp_app_desc.h"
#include "esp_log.h"

static const char *TAG = "rtc_test";

static bool rtc_fields_valid(const board_rtc_time_t *time)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (time->year < 2000 || time->year > 2099 ||
        time->month < 1 || time->month > 12 || time->weekday > 6 ||
        time->hour > 23 || time->minute > 59 || time->second > 59) {
        return false;
    }
    const unsigned max_day = days[time->month - 1] +
        (time->month == 2 && time->year % 4 == 0);
    return time->day >= 1 && time->day <= max_day;
}

static time_t rtc_to_seconds(const board_rtc_time_t *value, int *weekday)
{
    struct tm tm = {
        .tm_year = value->year - 1900,
        .tm_mon = value->month - 1,
        .tm_mday = value->day,
        .tm_hour = value->hour,
        .tm_min = value->minute,
        .tm_sec = value->second,
        .tm_isdst = -1,
    };
    const time_t seconds = mktime(&tm);
    if (weekday) {
        *weekday = tm.tm_wday;
    }
    return seconds;
}

static esp_err_t rtc_build_time(board_rtc_time_t *initial)
{
    const esp_app_desc_t *app = esp_app_get_description();
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month_name[4] = {0};
    unsigned year, day, hour, minute, second;
    if (sscanf(app->date, "%3s %u %u", month_name, &day, &year) != 3 ||
        sscanf(app->time, "%u:%u:%u", &hour, &minute, &second) != 3 ||
        year < 2000 || year > 2099 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 59) {
        return ESP_ERR_INVALID_STATE;
    }
    unsigned month = 0;
    for (unsigned i = 0; i < 12; ++i) {
        if (strncmp(month_name, months + i * 3, 3) == 0) {
            month = i + 1;
            break;
        }
    }
    *initial = (board_rtc_time_t) {
        .year = year, .month = month, .day = day,
        .hour = hour, .minute = minute, .second = second,
    };
    if (!rtc_fields_valid(initial)) {
        return ESP_ERR_INVALID_STATE;
    }
    int weekday = 0;
    if (rtc_to_seconds(initial, &weekday) == (time_t)-1) {
        return ESP_ERR_INVALID_STATE;
    }
    initial->weekday = weekday;
    return ESP_OK;
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
    if (ret != ESP_OK) {
        app_result_set(result, "RTC", APP_TEST_FAIL,
                       "PCF85063A read fail", esp_err_to_name(ret));
        return ret;
    }
    if (now.voltage_low || !rtc_fields_valid(&now)) {
        board_rtc_time_t initial = {0};
        ret = rtc_build_time(&initial);
        if (ret != ESP_OK) {
            app_result_set(result, "RTC", APP_TEST_FAIL,
                           "Build time unavailable", "enable APP_COMPILE_TIME_DATE");
            return ret;
        }
        ESP_LOGW(TAG, "RTC time invalid/VL; initializing from firmware build time: "
                 "%04u-%02u-%02u %02u:%02u:%02u (build host local time)",
                 initial.year, initial.month, initial.day,
                 initial.hour, initial.minute, initial.second);
        ret = board_rtc_set_time(&initial);
        if (ret != ESP_OK) {
            app_result_set(result, "RTC", APP_TEST_FAIL,
                           "RTC set time fail", esp_err_to_name(ret));
            return ret;
        }
        ret = board_rtc_get_time(&now);
        if (ret == ESP_OK) {
            int weekday = -1;
            const double elapsed = rtc_fields_valid(&now)
                ? difftime(rtc_to_seconds(&now, &weekday), rtc_to_seconds(&initial, NULL))
                : -1;
            if (now.voltage_low || elapsed < 0 || elapsed > 2 ||
                now.weekday != weekday) {
                ret = ESP_ERR_INVALID_RESPONSE;
            }
        }
        if (ret != ESP_OK) {
            app_result_set(result, "RTC", APP_TEST_FAIL,
                           "RTC time verify fail", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "RTC initial time written and verified; VL=0");
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
    app_result_set(result, "RTC", APP_TEST_PASS, line1, line2);
    return ESP_OK;
}
