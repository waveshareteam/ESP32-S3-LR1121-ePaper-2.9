#include "app_tests.h"

#include "board_support.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TEST_LED_BRIGHTNESS 48
#define TEST_LED_HOLD_MS    350

static void remember_error(esp_err_t ret, esp_err_t *first_error)
{
    if (ret != ESP_OK && *first_error == ESP_OK) {
        *first_error = ret;
    }
}

esp_err_t test_led_run(app_test_result_t *result)
{
    esp_err_t first_error = ESP_OK;
    remember_error(board_status_led_set_rgb(TEST_LED_BRIGHTNESS, 0, 0),
                   &first_error);
    vTaskDelay(pdMS_TO_TICKS(TEST_LED_HOLD_MS));
    remember_error(board_status_led_set_rgb(0, TEST_LED_BRIGHTNESS, 0),
                   &first_error);
    vTaskDelay(pdMS_TO_TICKS(TEST_LED_HOLD_MS));
    remember_error(board_status_led_set_rgb(0, 0, TEST_LED_BRIGHTNESS),
                   &first_error);
    vTaskDelay(pdMS_TO_TICKS(TEST_LED_HOLD_MS));
    remember_error(board_status_led_set(true), &first_error);

    app_result_set(result, "LED",
                   first_error == ESP_OK ? APP_TEST_PASS : APP_TEST_FAIL,
                   first_error == ESP_OK ? "WS2812 RGB PASS" : "WS2812 FAIL",
                   first_error == ESP_OK ? "red/green/blue/white" :
                                           esp_err_to_name(first_error));
    return first_error;
}
