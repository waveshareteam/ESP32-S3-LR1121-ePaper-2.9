#include "app_tests.h"

#include <stdio.h>

#include "board_epaper.h"
#include "board_radio.h"
#include "board_support.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power_state";

static void record_step(const char *name, esp_err_t ret,
                        esp_err_t *first_error)
{
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "PREPARE_SLEEP %-24s PASS", name);
    } else {
        ESP_LOGE(TAG, "PREPARE_SLEEP %-24s FAIL %s",
                 name, esp_err_to_name(ret));
        if (*first_error == ESP_OK) {
            *first_error = ret;
        }
    }
}

esp_err_t test_sleep_prepare_wakeup(void)
{
    esp_err_t ret = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    const gpio_config_t config = {
        .pin_bit_mask = BIT64(BOARD_BUTTON_SLEEP_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG,
                        "configure GPIO0 wake input failed");
    ESP_RETURN_ON_FALSE(
        gpio_get_level(BOARD_BUTTON_SLEEP_GPIO) == 1,
        ESP_ERR_INVALID_STATE, TAG,
        "GPIO0 is held low; refusing deep sleep");

    (void)rtc_gpio_hold_dis(BOARD_BUTTON_SLEEP_GPIO);
    ESP_RETURN_ON_ERROR(rtc_gpio_init(BOARD_BUTTON_SLEEP_GPIO), TAG,
                        "switch GPIO0 to RTC IO failed");
    ESP_RETURN_ON_ERROR(rtc_gpio_pullup_dis(BOARD_BUTTON_SLEEP_GPIO), TAG,
                        "disable GPIO0 RTC pull-up failed");
    ESP_RETURN_ON_ERROR(rtc_gpio_pulldown_dis(BOARD_BUTTON_SLEEP_GPIO), TAG,
                        "disable GPIO0 RTC pull-down failed");
    ESP_RETURN_ON_ERROR(
        esp_sleep_enable_ext1_wakeup_io(
            BIT64(BOARD_BUTTON_SLEEP_GPIO), ESP_EXT1_WAKEUP_ANY_LOW),
        TAG, "enable GPIO0 EXT1 ANY_LOW wake failed");
    ESP_RETURN_ON_ERROR(
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF),
        TAG, "power down RTC peripheral domain failed");
    ESP_LOGI(TAG,
             "GPIO0 EXT1 ANY_LOW wake configured; external 10K pull-up only, RTC_PERIPH off");
    return ESP_OK;
}

esp_err_t test_sleep_prepare_peripherals(void)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t ret;

    ret = board_radio_prepare_deep_sleep();
    record_step("LR1121 sleep/IRQ clear", ret, &first_error);
    ret = board_epaper_sleep();
    record_step("e-paper controller sleep", ret, &first_error);
    ret = board_status_led_set(false);
    record_step("WS2812 off", ret, &first_error);
    ret = board_rtc_stop_timer_interrupt();
    record_step("RTC timer/IRQ stop", ret, &first_error);
    ret = app_battery_prepare_sleep();
    record_step("AXP2602 IRQ off/sleep", ret, &first_error);
    ret = board_epaper_deinit();
    record_step("e-paper/LVGL/power off", ret, &first_error);
    ret = board_support_deinit();
    record_step("Board Manager/buses off", ret, &first_error);
    ret = board_prepare_gpio_deep_sleep();
    record_step("board GPIO safe levels", ret, &first_error);
    ret = board_radio_isolate_powered_off_pins();
    record_step("LR1121/SPI powered-off Hi-Z", ret, &first_error);
    ret = board_shared_power_hold_off();
    record_step("shared peripheral power held off", ret, &first_error);

    if (first_error == ESP_OK) {
        gpio_deep_sleep_hold_en();
        ESP_LOGI(TAG,
                 "PREPARE_SLEEP all critical peripheral steps PASS");
    }
    return first_error;
}

void test_sleep_abort(void)
{
    const esp_err_t ret =
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "disable wake sources while aborting failed: %s",
                 esp_err_to_name(ret));
    }
    (void)rtc_gpio_hold_dis(BOARD_BUTTON_SLEEP_GPIO);
    (void)rtc_gpio_deinit(BOARD_BUTTON_SLEEP_GPIO);
    board_release_gpio_deep_sleep_holds();
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(BOARD_BUTTON_SLEEP_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&config);
    ESP_LOGW(TAG, "sleep aborted; wake source and GPIO holds released");
}

esp_err_t test_sleep_enter_low_power(void)
{
    ESP_LOGI(TAG,
             "state=LOW_POWER; deep sleep starts after final UART drain");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
    return ESP_ERR_INVALID_STATE;
}
