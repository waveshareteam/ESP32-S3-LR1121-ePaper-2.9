#include "app_tests.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "board_epaper.h"
#include "board_radio.h"
#include "board_support.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "runtime";

static QueueHandle_t s_events;
static TaskHandle_t s_task;
static app_runtime_state_t s_state;
static bool s_nvs_initialized;
static uint32_t s_displayed_rx_count;
static TickType_t s_last_epaper_attempt;
static bool s_self_test_running;
static app_test_result_t s_self_test_results[8];
static bool s_boot_wakeup_pending;

static bool rx_refresh_due(TickType_t now, uint32_t rx_count)
{
    return rx_count != s_displayed_rx_count &&
           (TickType_t)(now - s_last_epaper_attempt) >=
               pdMS_TO_TICKS(TEST_EPAPER_RX_MIN_INTERVAL_MS);
}

static const char *state_name(app_runtime_state_t state)
{
    switch (state) {
    case ACTIVE_SELF_TEST:
        return "ACTIVE_SELF_TEST";
    case ACTIVE_READY:
        return "ACTIVE_READY";
    case LORA_TX:
        return "LORA_TX";
    case LORA_RX:
        return "LORA_RX";
    case PREPARE_SLEEP:
        return "PREPARE_SLEEP";
    case LOW_POWER:
        return "LOW_POWER";
    case WAKE_REINIT:
        return "WAKE_REINIT";
    default:
        return "UNKNOWN";
    }
}

static void set_state(app_runtime_state_t state)
{
    s_state = state;
    ESP_LOGI(TAG, "state=%s", state_name(state));
}

static void remember_error(const char *operation, esp_err_t ret,
                           esp_err_t *first_error)
{
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "%s: PASS", operation);
    } else {
        ESP_LOGE(TAG, "%s: FAIL %s", operation, esp_err_to_name(ret));
        if (*first_error == ESP_OK) {
            *first_error = ret;
        }
    }
}

static esp_err_t nvs_prepare(void)
{
    if (s_nvs_initialized) {
        return ESP_OK;
    }
    const esp_err_t ret = nvs_flash_init();
    if (ret == ESP_OK) {
        s_nvs_initialized = true;
    } else if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
               ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG,
                 "NVS requires maintenance; automatic erase is intentionally disabled");
    }
    return ret;
}

static esp_err_t run_self_test(void)
{
    app_test_result_t *results = s_self_test_results;
    esp_err_t first_error = results[4].status == APP_TEST_FAIL ? ESP_FAIL : ESP_OK;
    esp_err_t ret;

    ret = test_system_run(&results[0]);
    if (ret != ESP_OK && first_error == ESP_OK) first_error = ret;
    ret = test_i2c_run(&results[1]);
    if (ret != ESP_OK && first_error == ESP_OK) first_error = ret;
    ret = test_rtc_run(&results[2]);
    if (ret != ESP_OK && first_error == ESP_OK) first_error = ret;
    ret = test_battery_run(&results[3]);
    if (ret != ESP_OK && first_error == ESP_OK) first_error = ret;
    ret = test_epaper_run(&results[5]);
    if (ret != ESP_OK && first_error == ESP_OK) first_error = ret;
    ret = test_storage_run(&results[6]);
    if (ret != ESP_OK && first_error == ESP_OK) first_error = ret;
    ret = test_radio_run(&results[7]);
    if (ret != ESP_OK && first_error == ESP_OK) first_error = ret;

    return first_error;
}

static void self_test_task(void *arg)
{
    (void)arg;
    const app_runtime_event_t event = {
        .type = APP_EVENT_SELF_TEST_DONE,
        .result = run_self_test(),
    };
    /* Publish only after all result writes and peripheral operations finish. */
    (void)xQueueSend(s_events, &event, portMAX_DELAY);
    vTaskDelete(NULL);
}

static void show_self_test_summary(esp_err_t first_error)
{
    const app_test_result_t *results = s_self_test_results;
    const esp_err_t ret = app_ui_show_summary(results, 8, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "e-paper self-test summary unavailable: %s",
                 esp_err_to_name(ret));
    } else {
        /* Start the reading interval after the full panel refresh completes. */
        vTaskDelay(pdMS_TO_TICKS(TEST_EPAPER_SUMMARY_HOLD_MS));
    }
    for (size_t i = 0; i < 8; ++i) {
        const char *status =
            results[i].status == APP_TEST_PASS
                ? "PASS"
                : (results[i].status == APP_TEST_FAIL
                       ? "FAIL"
                       : "NOT_TESTED");
        ESP_LOGI(TAG, "SELF_TEST %-12s %-10s %s | %s",
                 results[i].title, status, results[i].line1,
                 results[i].line2);
    }
    ESP_LOGI(TAG, "self-test complete: %s",
             first_error == ESP_OK ? "PASS/NOT_TESTED" : "FAIL");
}

static esp_err_t start_active_services(void)
{
    esp_err_t first_error = ESP_OK;
    remember_error("Wi-Fi APSTA start", app_wifi_start(), &first_error);
    remember_error("BLE passive scan start", app_ble_start(), &first_error);
    remember_error("battery monitor start",
                   app_battery_monitor_start(), &first_error);
    remember_error("button service start",
                   app_buttons_start(s_events), &first_error);
    remember_error("LR1121 RX service start",
                   app_radio_service_start(s_events), &first_error);
    return first_error;
}

static void report_runtime(bool refresh_epaper)
{
    board_radio_status_t radio = {0};
    (void)app_radio_service_get_status(&radio);

    const char *wifi_status =
        app_wifi_sta_connected()
            ? "UP"
            : (app_wifi_sta_configured() ? "WAIT" : "NOT_CONFIGURED");
    const uint8_t ap_clients = app_wifi_ap_clients();
    const bool sta_connected = app_wifi_sta_connected();
    const bool ble_started = app_ble_is_started();
    const uint32_t ble_scans = app_ble_scan_count();
    const char *ble_status = ble_started ? "RUNNING" : "FAILED";

    ESP_LOGI(TAG,
             "state=%s TX=%" PRIu32 "/fail=%" PRIu32
             " RX=%" PRIu32 "/err=%" PRIu32
             " WiFi=%s APclients=%u BLE=%s scans=%" PRIu32,
             state_name(s_state), radio.tx_count, radio.tx_fail_count,
             radio.rx_count, radio.rx_error_count,
             wifi_status, (unsigned)ap_clients, ble_status, ble_scans);
    ESP_LOGI(TAG, "heap internal=%u PSRAM=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (refresh_epaper) {
        const esp_err_t ret = app_ui_show_runtime(
            state_name(s_state), radio.tx_count, radio.tx_fail_count,
            radio.rx_count, ap_clients, sta_connected, ble_started,
            ble_scans);
        s_last_epaper_attempt = xTaskGetTickCount();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "runtime e-paper refresh failed: %s",
                     esp_err_to_name(ret));
        } else {
            /* Only acknowledge rendered packets; arrivals mid-refresh stay pending. */
            s_displayed_rx_count = radio.rx_count;
        }
    }
}

static void cleanup_partial_active_state(void)
{
    (void)app_buttons_stop();
    (void)app_storage_stop();
    (void)app_battery_monitor_stop();
    (void)app_ble_stop();
    (void)app_wifi_stop();
    (void)app_radio_service_stop();
    (void)board_radio_sleep();
    (void)board_epaper_deinit();
    (void)board_support_deinit();
}

static void wait_for_gpio0_release(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(BOARD_BUTTON_SLEEP_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&config);
    uint32_t stable_samples = 0;
    while (stable_samples < 10) {
        if (gpio_get_level(BOARD_BUTTON_SLEEP_GPIO) != 0) {
            ++stable_samples;
        } else {
            stable_samples = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void wake_reinit(void)
{
    const uint32_t causes = esp_sleep_get_wakeup_causes();
    const uint64_t ext1_status = esp_sleep_get_ext1_wakeup_status();
    if (s_boot_wakeup_pending) {
        ESP_LOGI(TAG,
                 "DEEP_SLEEP_WAKE WAKE_REINIT causes=0x%08" PRIX32
                 " ext1_gpio_mask=0x%016" PRIX64,
                 causes, ext1_status);
        s_boot_wakeup_pending = false;
    } else {
        ESP_LOGW(TAG, "SLEEP_CANCELLED: recovering active services without a deep-sleep wakeup");
    }
    test_sleep_abort();
    cleanup_partial_active_state();
    wait_for_gpio0_release();
    xQueueReset(s_events);
    ESP_LOGI(TAG,
             "GPIO0 released; all buses, peripherals and active services will be rebuilt");
}

static bool stop_active_services(void)
{
    esp_err_t first_error = ESP_OK;
    remember_error("button task stop", app_buttons_stop(), &first_error);
    remember_error("SD sync/unmount", app_storage_stop(), &first_error);
    remember_error("battery monitor stop",
                   app_battery_monitor_stop(), &first_error);
    remember_error("BLE stop/deinit", app_ble_stop(), &first_error);
    remember_error("Wi-Fi APSTA stop/deinit", app_wifi_stop(), &first_error);
    remember_error("LR1121 service stop",
                   app_radio_service_stop(), &first_error);
    if (s_nvs_initialized) {
        const esp_err_t ret = nvs_flash_deinit();
        remember_error("NVS deinit", ret, &first_error);
        if (ret == ESP_OK) {
            s_nvs_initialized = false;
        }
    }
    return first_error == ESP_OK;
}

static bool prepare_sleep(void)
{
    const esp_err_t ui_ret = app_ui_show_message(
        "PREPARE_SLEEP", "Stopping tasks/radios",
        "GPIO0 wakes from deep sleep", true);
    if (ui_ret != ESP_OK) {
        ESP_LOGW(TAG, "final e-paper message failed: %s",
                 esp_err_to_name(ui_ret));
    }

    if (!stop_active_services()) {
        ESP_LOGE(TAG,
                 "critical task/network stop failed; deep sleep cancelled");
        test_sleep_abort();
        return false;
    }
    if (app_buttons_wait_released() != ESP_OK) {
        ESP_LOGE(TAG, "button release check failed; deep sleep cancelled");
        test_sleep_abort();
        return false;
    }
    if (test_sleep_prepare_peripherals() != ESP_OK) {
        ESP_LOGE(TAG,
                 "critical peripheral shutdown failed; deep sleep cancelled");
        test_sleep_abort();
        return false;
    }
    if (test_sleep_prepare_wakeup() != ESP_OK) {
        ESP_LOGE(TAG, "GPIO0 EXT1 setup failed; deep sleep cancelled");
        test_sleep_abort();
        return false;
    }
    return true;
}

static void runtime_task(void *arg)
{
    (void)arg;
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    s_boot_wakeup_pending = reset_reason == ESP_RST_DEEPSLEEP;
    ESP_LOGI(TAG, "boot reset=%d wake_bitmap=0x%08" PRIX32,
             (int)reset_reason, wake_causes);
    if (reset_reason == ESP_RST_USB) {
        ESP_LOGW(TAG, "USB_RESET: reset by USB peripheral, not GPIO0 deep-sleep wakeup");
    }
    set_state(s_boot_wakeup_pending ? WAKE_REINIT : ACTIVE_SELF_TEST);
    TickType_t next_uart = 0;
    TickType_t next_epaper = 0;

    while (true) {
        if (s_state == WAKE_REINIT) {
            wake_reinit();
            set_state(ACTIVE_SELF_TEST);
        } else if (s_state == ACTIVE_SELF_TEST) {
            if (!s_self_test_running) {
                memset(s_self_test_results, 0, sizeof(s_self_test_results));
                esp_err_t ret = board_support_prepare();
                if (ret == ESP_OK) {
                    ret = board_epaper_power_set(true);
                }
                if (ret == ESP_OK) {
                    /* The LED shares the peripheral rail; power it before UI init. */
                    (void)test_led_run(&s_self_test_results[4]);
                } else {
                    ESP_LOGE(TAG, "startup peripheral power failed: %s",
                             esp_err_to_name(ret));
                    app_result_set(&s_self_test_results[4], "LED", APP_TEST_FAIL,
                                   "Power/init failed", esp_err_to_name(ret));
                }

                const esp_err_t ui_ret = app_ui_init();
                if (ui_ret != ESP_OK) {
                    ESP_LOGW(TAG, "e-paper init failed; UART remains available: %s",
                             esp_err_to_name(ui_ret));
                }
                const esp_err_t nvs_ret = nvs_prepare();
                if (nvs_ret != ESP_OK) {
                    ESP_LOGE(TAG, "NVS init failed; Wi-Fi/BLE may be unavailable: %s",
                             esp_err_to_name(nvs_ret));
                }

                /* UI is initialized before the worker can inspect the panel. */
                s_self_test_running = xTaskCreate(
                    self_test_task, "startup_self_test", 12288,
                    NULL, 4, NULL) == pdPASS;
                if (ui_ret == ESP_OK) {
                    ret = app_ui_show_message("SELF TEST", "Testing hardware...",
                                              "Please wait for results", true);
                    if (ret != ESP_OK) {
                        ESP_LOGW(TAG, "startup page failed: %s", esp_err_to_name(ret));
                    }
                }
                if (!s_self_test_running) {
                    ESP_LOGW(TAG, "self-test task allocation failed; running synchronously");
                    show_self_test_summary(run_self_test());
                    (void)start_active_services();
                    set_state(ACTIVE_READY);
                    continue;
                }
            }

            /* Buttons/radios start only after the worker releases the hardware. */
            app_runtime_event_t event = {0};
            if (xQueueReceive(s_events, &event, pdMS_TO_TICKS(100)) == pdTRUE &&
                event.type == APP_EVENT_SELF_TEST_DONE) {
                s_self_test_running = false;
                show_self_test_summary(event.result);
                (void)start_active_services();
                set_state(ACTIVE_READY);
            }
        } else if (s_state == ACTIVE_READY) {
            report_runtime(true);
            set_state(LORA_RX);
            next_uart = xTaskGetTickCount() +
                        pdMS_TO_TICKS(TEST_UART_STATUS_PERIOD_MS);
            next_epaper = xTaskGetTickCount() +
                          pdMS_TO_TICKS(TEST_EPAPER_STATUS_PERIOD_MS);
        } else if (s_state == LORA_RX || s_state == LORA_TX) {
            app_runtime_event_t event = {0};
            if (xQueueReceive(s_events, &event, pdMS_TO_TICKS(100)) ==
                pdTRUE) {
                if (event.type == APP_EVENT_BUTTON_GPIO0) {
                    set_state(PREPARE_SLEEP);
                } else if (event.type == APP_EVENT_BUTTON_GPIO1 &&
                           s_state == LORA_RX) {
                    const esp_err_t ret = app_radio_service_request_tx();
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG,
                                 "GPIO1 edge accepted; exactly one LoRa TX queued");
                        set_state(LORA_TX);
                    } else {
                        ESP_LOGE(TAG, "queue LoRa TX failed: %s",
                                 esp_err_to_name(ret));
                    }
                } else if (event.type == APP_EVENT_RADIO_TX_DONE) {
                    ESP_LOGI(TAG,
                             "LoRa TX complete result=%s seq=%" PRIu32
                             "; RX restored",
                             esp_err_to_name(event.result), event.sequence);
                    set_state(LORA_RX);

                    /* LR1121 and e-paper share SPI; refresh after RX restore. */
                    report_runtime(true);
                    const TickType_t refreshed_at = xTaskGetTickCount();
                    next_uart =
                        refreshed_at +
                        pdMS_TO_TICKS(TEST_UART_STATUS_PERIOD_MS);
                    next_epaper =
                        refreshed_at +
                        pdMS_TO_TICKS(TEST_EPAPER_STATUS_PERIOD_MS);
                }
            }

            const TickType_t now = xTaskGetTickCount();
            if ((int32_t)(now - next_uart) >= 0) {
                report_runtime(false);
                next_uart = now +
                            pdMS_TO_TICKS(TEST_UART_STATUS_PERIOD_MS);
            }
            /* Poll the shared snapshot: RX bursts must not fill the button/TX queue. */
            board_radio_status_t radio = {0};
            (void)app_radio_service_get_status(&radio);
            if (s_state == LORA_RX &&
                ((int32_t)(now - next_epaper) >= 0 ||
                 rx_refresh_due(now, radio.rx_count))) {
                report_runtime(true);
                next_epaper = xTaskGetTickCount() +
                              pdMS_TO_TICKS(TEST_EPAPER_STATUS_PERIOD_MS);
            }
        } else if (s_state == PREPARE_SLEEP) {
            if (prepare_sleep()) {
                set_state(LOW_POWER);
                const esp_err_t ret = test_sleep_enter_low_power();
                ESP_LOGE(TAG, "deep-sleep entry cancelled/returned: %s",
                         esp_err_to_name(ret));
                set_state(WAKE_REINIT);
            } else {
                set_state(WAKE_REINIT);
            }
        } else if (s_state == LOW_POWER) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void app_runtime_start(void)
{
    s_events = xQueueCreate(20, sizeof(app_runtime_event_t));
    if (!s_events) {
        ESP_LOGE(TAG, "create runtime event queue failed");
        return;
    }
    if (xTaskCreate(runtime_task, "runtime_state", 14336, NULL, 6,
                    &s_task) != pdPASS) {
        ESP_LOGE(TAG, "create runtime state task failed");
        vQueueDelete(s_events);
        s_events = NULL;
        s_task = NULL;
    }
}
