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

static esp_err_t wait_for_rtc_gpio0_release(void)
{
    const TickType_t started = xTaskGetTickCount();
    TickType_t high_since = started;
    while ((TickType_t)(xTaskGetTickCount() - started) < pdMS_TO_TICKS(3000)) {
        const TickType_t now = xTaskGetTickCount();
        if (rtc_gpio_get_level(BOARD_BUTTON_SLEEP_GPIO) == 0) {
            high_since = now;
        } else if ((TickType_t)(now - high_since) >=
                   pdMS_TO_TICKS(TEST_BUTTON_RELEASE_MS)) {
            ESP_LOGI(TAG, "GPIO0 RTC input stably HIGH; wake button released");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "GPIO0 RTC input not stably HIGH; deep sleep cancelled");
    return ESP_ERR_TIMEOUT;
}

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

    ESP_RETURN_ON_ERROR(rtc_gpio_hold_dis(BOARD_BUTTON_SLEEP_GPIO), TAG,
                        "release GPIO0 RTC hold failed");
    ESP_RETURN_ON_ERROR(rtc_gpio_init(BOARD_BUTTON_SLEEP_GPIO), TAG,
                        "switch GPIO0 to RTC IO failed");
    ESP_RETURN_ON_ERROR(
        rtc_gpio_set_direction(BOARD_BUTTON_SLEEP_GPIO, RTC_GPIO_MODE_INPUT_ONLY),
        TAG, "enable GPIO0 RTC input failed");
    ESP_RETURN_ON_ERROR(rtc_gpio_pullup_dis(BOARD_BUTTON_SLEEP_GPIO), TAG,
                        "disable GPIO0 RTC pull-up failed");
    ESP_RETURN_ON_ERROR(rtc_gpio_pulldown_dis(BOARD_BUTTON_SLEEP_GPIO), TAG,
                        "disable GPIO0 RTC pull-down failed");
    ESP_RETURN_ON_ERROR(wait_for_rtc_gpio0_release(), TAG,
                        "GPIO0 RTC release check failed");
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
    if (first_error != ESP_OK) {
        return first_error;
    }

    /* Drivers may reset pin configuration during deinit; apply holds afterwards. */
    ret = board_support_deinit();
    record_step("Board Manager/buses off", ret, &first_error);
    if (first_error != ESP_OK) {
        return first_error;
    }
    ret = board_shared_power_hold_off();
    record_step("shared 3V3 held off", ret, &first_error);
    if (first_error != ESP_OK) {
        return first_error;
    }
    ret = board_prepare_gpio_deep_sleep();
    record_step("board GPIO safe levels", ret, &first_error);
    if (first_error != ESP_OK) {
        return first_error;
    }
    ret = board_radio_isolate_powered_off_pins();
    record_step("LR1121/SPI powered-off Hi-Z", ret, &first_error);

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
    ESP_LOGI(TAG, "wake configuration cleared; GPIO holds released");
}

esp_err_t test_sleep_enter_low_power(void)
{
    ESP_LOGI(TAG,
             "state=LOW_POWER; deep sleep starts after final UART drain");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    /* Check the actual wake input after all shutdown work and UART draining. */
    ESP_RETURN_ON_ERROR(wait_for_rtc_gpio0_release(), TAG,
                        "final GPIO0 release check failed");
    ESP_RETURN_ON_FALSE(rtc_gpio_get_level(BOARD_BUTTON_SLEEP_GPIO) == 1,
                        ESP_ERR_INVALID_STATE, TAG,
                        "GPIO0 became LOW; deep sleep cancelled");
    ESP_LOGI(TAG, "entering deep sleep: GPIO0 RTC=HIGH, EXT1 ANY_LOW only");
    esp_deep_sleep_start();
    return ESP_ERR_INVALID_STATE;
}
