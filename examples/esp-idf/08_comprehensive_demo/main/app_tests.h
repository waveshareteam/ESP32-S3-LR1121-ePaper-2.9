#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <app_config.h>
#include "board_radio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_TEST_NOT_TESTED = 0,
    APP_TEST_PASS,
    APP_TEST_FAIL,
} app_test_status_t;

typedef struct {
    app_test_status_t status;
    char title[24];
    char line1[48];
    char line2[48];
} app_test_result_t;

typedef struct {
    bool running;
    uint32_t digest;
    uint32_t error_count;
    uint32_t iterations[2];
    uint32_t internal_bytes;
    uint32_t psram_bytes;
    uint32_t internal_kib_s;
    uint32_t psram_kib_s;
    uint32_t uptime_ms;
    uint32_t stack_low_watermark[2];
} app_stress_status_t;

typedef enum {
    ACTIVE_SELF_TEST = 0,
    ACTIVE_STRESS,
    LORA_TX,
    LORA_RX,
    PREPARE_SLEEP,
    LOW_POWER,
    WAKE_REINIT,
} app_runtime_state_t;

typedef enum {
    APP_EVENT_BUTTON_GPIO0 = 1,
    APP_EVENT_BUTTON_GPIO1,
    APP_EVENT_RADIO_TX_DONE,
    APP_EVENT_SELF_TEST_DONE,
} app_runtime_event_type_t;

typedef struct {
    app_runtime_event_type_t type;
    esp_err_t result;
    uint32_t sequence;
    int16_t radio_state;
} app_runtime_event_t;

void app_result_set(app_test_result_t *result, const char *title,
                    app_test_status_t status,
                    const char *line1, const char *line2);

esp_err_t app_ui_init(void);
esp_err_t app_ui_show_summary(const app_test_result_t *results, size_t count,
                              bool full_refresh);
esp_err_t app_ui_show_message(const char *title, const char *line1,
                              const char *line2, bool full_refresh);
esp_err_t app_ui_show_runtime(const char *state, uint32_t tx_count,
                             uint32_t tx_fail_count, uint32_t rx_count,
                             uint32_t ap_clients, bool sta_connected,
                             bool ble_started, uint32_t ble_scan_count);

esp_err_t test_led_run(app_test_result_t *result);
esp_err_t test_i2c_run(app_test_result_t *result);
esp_err_t test_epaper_run(app_test_result_t *result);
esp_err_t test_battery_run(app_test_result_t *result);
esp_err_t test_rtc_run(app_test_result_t *result);
esp_err_t test_radio_run(app_test_result_t *result);
esp_err_t test_system_run(app_test_result_t *result);
esp_err_t test_storage_run(app_test_result_t *result);
esp_err_t app_storage_stop(void);

esp_err_t app_battery_monitor_start(void);
esp_err_t app_battery_monitor_stop(void);

esp_err_t app_buttons_start(QueueHandle_t events);
esp_err_t app_buttons_stop(void);
esp_err_t app_buttons_wait_released(void);

esp_err_t app_stress_start(void);
esp_err_t app_stress_stop(void);
uint32_t app_stress_get_digest(void);
void app_stress_get_status(app_stress_status_t *status);

esp_err_t app_radio_service_start(QueueHandle_t events);
esp_err_t app_radio_service_stop(void);
esp_err_t app_radio_service_request_tx(void);
esp_err_t app_radio_service_get_status(board_radio_status_t *status);

esp_err_t app_wifi_start(void);
esp_err_t app_wifi_stop(void);
bool app_wifi_is_started(void);
bool app_wifi_sta_connected(void);
bool app_wifi_sta_configured(void);
uint8_t app_wifi_ap_clients(void);

esp_err_t app_ble_start(void);
esp_err_t app_ble_stop(void);
bool app_ble_is_started(void);
uint32_t app_ble_scan_count(void);

esp_err_t test_sleep_prepare_wakeup(void);
esp_err_t test_sleep_prepare_peripherals(void);
void test_sleep_abort(void);
esp_err_t test_sleep_enter_low_power(void);

void app_runtime_start(void);

#ifdef __cplusplus
}
#endif
