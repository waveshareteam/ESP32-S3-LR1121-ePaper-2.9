#include "app_tests.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "board_radio.h"
#include "esp_check.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "radio_service";

#define RADIO_TASK_DONE       BIT0
#define RADIO_RX_POLL_MS      250U
#define RADIO_RETRY_PERIOD_MS 5000U

typedef enum {
    RADIO_COMMAND_TX = 1,
    RADIO_COMMAND_STOP,
} radio_command_t;

static QueueHandle_t s_events;
static QueueHandle_t s_commands;
static EventGroupHandle_t s_done;
static TaskHandle_t s_task;
static atomic_bool s_running;
static atomic_bool s_stop_requested;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static board_radio_status_t s_status;

static void publish_status(const board_radio_status_t *status)
{
    taskENTER_CRITICAL(&s_status_lock);
    s_status = *status;
    taskEXIT_CRITICAL(&s_status_lock);
}

static void post_radio_event(app_runtime_event_type_t type, esp_err_t result,
                             uint32_t sequence,
                             const board_radio_status_t *status)
{
    if (!s_events) {
        return;
    }
    const app_runtime_event_t event = {
        .type = type,
        .result = result,
        .sequence = sequence,
        .radio_state = status ? status->state : 0,
    };
    if (xQueueSendToFront(s_events, &event, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGE(TAG, "runtime event queue full; critical event %d dropped",
                 (int)type);
    }
}

static void log_received_packet(const board_radio_status_t *status)
{
    ESP_LOGI(TAG,
             "RX payload=%s RSSI=%.1f dBm SNR=%.1f dB freq_error=NOT_AVAILABLE",
             status->last_payload, (double)status->last_rssi,
             (double)status->last_snr);
}

static esp_err_t receive_once(board_radio_status_t *status, uint32_t timeout_ms)
{
    const esp_err_t ret = board_radio_receive(status, timeout_ms);
    publish_status(status);
    if (ret == ESP_OK) {
        log_received_packet(status);
    } else if (ret != ESP_ERR_TIMEOUT && ret != ESP_FAIL) {
        ESP_LOGW(TAG, "RX service error: %s radio=%d (%s)",
                 esp_err_to_name(ret), status->state,
                 board_radio_state_name(status->state));
    }
    return ret;
}

static void format_test_payload(char *payload, size_t payload_size,
                                uint32_t sequence)
{
    char body[64];
    const int64_t now_ms = esp_timer_get_time() / 1000;
    snprintf(body, sizeof(body), "id=%s seq=%" PRIu32 " ms=%" PRIi64,
             TEST_DEVICE_ID, sequence, now_ms);
    const uint32_t crc = esp_crc32_le(
        0, (const uint8_t *)body, strlen(body));
    snprintf(payload, payload_size, "%s crc=%08" PRIX32, body, crc);
}

static void handle_tx(board_radio_status_t *status, uint32_t *sequence,
                      bool *radio_ready)
{
    ++(*sequence);
    char payload[sizeof(status->last_payload)];
    format_test_payload(payload, sizeof(payload), *sequence);

    esp_err_t tx_ret = ESP_ERR_INVALID_STATE;
    if (*radio_ready) {
        tx_ret = board_radio_send(status, payload);
    }
    publish_status(status);
    ESP_LOGI(TAG, "TX seq=%" PRIu32 " result=%s radio=%d (%s) payload=%s",
             *sequence, esp_err_to_name(tx_ret), status->state,
             board_radio_state_name(status->state), payload);

    if (*radio_ready) {
        const esp_err_t rx_ret = receive_once(status, 1);
        if (rx_ret != ESP_OK && rx_ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "TX finished but LR1121 RX restore failed: %s",
                     esp_err_to_name(rx_ret));
            *radio_ready = false;
        }
    }
    post_radio_event(APP_EVENT_RADIO_TX_DONE, tx_ret, *sequence, status);
}

static void radio_task(void *arg)
{
    (void)arg;
    const board_radio_config_t config = TEST_LORA_CONFIG_INIT();
    board_radio_status_t status = {0};
    uint32_t sequence = 0;
    TickType_t next_retry = 0;
    bool radio_ready = board_radio_init(&config, &status) == ESP_OK;
    publish_status(&status);

    if (radio_ready) {
        ESP_LOGI(TAG,
                 "LR1121 RX service ready: %.1fMHz BW%.1fkHz SF%u CR4/%u SW0x%02X PWR%d",
                 (double)config.frequency_mhz, (double)config.bandwidth_khz,
                 config.spreading_factor, config.coding_rate,
                 config.sync_word, config.output_power_dbm);
    } else {
        ESP_LOGE(TAG, "LR1121 service init failed: radio=%d (%s)",
                 status.state, board_radio_state_name(status.state));
        next_retry = xTaskGetTickCount() +
                     pdMS_TO_TICKS(RADIO_RETRY_PERIOD_MS);
    }

    while (!atomic_load_explicit(&s_stop_requested, memory_order_acquire)) {
        radio_command_t command = 0;
        if (xQueueReceive(s_commands, &command, 0) == pdTRUE) {
            if (command == RADIO_COMMAND_STOP) {
                break;
            }
            if (command == RADIO_COMMAND_TX) {
                if (!radio_ready) {
                    radio_ready = board_radio_init(&config, &status) == ESP_OK;
                    publish_status(&status);
                }
                handle_tx(&status, &sequence, &radio_ready);
                continue;
            }
        }

        if (radio_ready) {
            const esp_err_t rx_ret = receive_once(&status, RADIO_RX_POLL_MS);
            if (rx_ret != ESP_OK && rx_ret != ESP_ERR_TIMEOUT &&
                status.state != 0) {
                ESP_LOGW(TAG, "LR1121 receive state=%d (%s); retrying init",
                         status.state, board_radio_state_name(status.state));
                (void)board_radio_sleep();
                radio_ready = false;
                next_retry = xTaskGetTickCount() +
                             pdMS_TO_TICKS(RADIO_RETRY_PERIOD_MS);
            }
        } else if ((int32_t)(xTaskGetTickCount() - next_retry) >= 0) {
            radio_ready = board_radio_init(&config, &status) == ESP_OK;
            publish_status(&status);
            if (radio_ready) {
                ESP_LOGI(TAG, "LR1121 service recovered; RX restored");
            } else {
                next_retry = xTaskGetTickCount() +
                             pdMS_TO_TICKS(RADIO_RETRY_PERIOD_MS);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(RADIO_RX_POLL_MS));
        }
    }

    const esp_err_t sleep_ret = board_radio_sleep();
    if (sleep_ret != ESP_OK) {
        ESP_LOGE(TAG, "LR1121 final sleep failed: %s",
                 esp_err_to_name(sleep_ret));
    }
    publish_status(&status);
    atomic_store_explicit(&s_running, false, memory_order_release);
    xEventGroupSetBits(s_done, RADIO_TASK_DONE);
    vTaskDelete(NULL);
}

esp_err_t app_radio_service_start(QueueHandle_t events)
{
    ESP_RETURN_ON_FALSE(events, ESP_ERR_INVALID_ARG, TAG,
                        "event queue is null");
    if (atomic_load_explicit(&s_running, memory_order_acquire)) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(!s_task, ESP_ERR_INVALID_STATE, TAG,
                        "radio task has not finished cleanup");

    s_commands = xQueueCreate(8, sizeof(radio_command_t));
    s_done = xEventGroupCreate();
    if (!s_commands || !s_done) {
        if (s_commands) {
            vQueueDelete(s_commands);
            s_commands = NULL;
        }
        if (s_done) {
            vEventGroupDelete(s_done);
            s_done = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    s_events = events;
    memset(&s_status, 0, sizeof(s_status));
    atomic_store_explicit(&s_stop_requested, false, memory_order_release);
    atomic_store_explicit(&s_running, true, memory_order_release);
    if (xTaskCreate(radio_task, "radio_service", 7168, NULL, 7, &s_task) !=
        pdPASS) {
        atomic_store_explicit(&s_running, false, memory_order_release);
        vQueueDelete(s_commands);
        vEventGroupDelete(s_done);
        s_commands = NULL;
        s_done = NULL;
        s_events = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t app_radio_service_stop(void)
{
    if (!s_task) {
        return ESP_OK;
    }

    atomic_store_explicit(&s_stop_requested, true, memory_order_release);
    const radio_command_t command = RADIO_COMMAND_STOP;
    (void)xQueueSendToFront(s_commands, &command, 0);
    const EventBits_t bits = xEventGroupWaitBits(
        s_done, RADIO_TASK_DONE, pdFALSE, pdTRUE, pdMS_TO_TICKS(7000));
    if ((bits & RADIO_TASK_DONE) == 0) {
        ESP_LOGE(TAG, "radio service stop timed out; resources retained");
        return ESP_ERR_TIMEOUT;
    }

    s_task = NULL;
    vQueueDelete(s_commands);
    vEventGroupDelete(s_done);
    s_commands = NULL;
    s_done = NULL;
    s_events = NULL;
    return ESP_OK;
}

esp_err_t app_radio_service_request_tx(void)
{
    ESP_RETURN_ON_FALSE(
        atomic_load_explicit(&s_running, memory_order_acquire) && s_commands &&
            !atomic_load_explicit(&s_stop_requested, memory_order_acquire),
        ESP_ERR_INVALID_STATE, TAG, "radio service is not running");
    const radio_command_t command = RADIO_COMMAND_TX;
    return xQueueSend(s_commands, &command, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

esp_err_t app_radio_service_get_status(board_radio_status_t *status)
{
    ESP_RETURN_ON_FALSE(status, ESP_ERR_INVALID_ARG, TAG, "status is null");
    taskENTER_CRITICAL(&s_status_lock);
    *status = s_status;
    taskEXIT_CRITICAL(&s_status_lock);
    return ESP_OK;
}
