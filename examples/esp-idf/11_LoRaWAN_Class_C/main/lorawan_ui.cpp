/**
 * @file lorawan_ui.cpp
 * @brief Queued and rate-limited rendering for the shared-SPI e-paper panel.
 */

#include "lorawan_ui.h"

#include <inttypes.h>
#include <stdio.h>

#include "board_epaper.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lorawan_config.h"

namespace {

constexpr size_t kLineCount = 7U;
constexpr size_t kLineChars = 34U;
constexpr EventBits_t kFirstAttemptDone =
    static_cast<EventBits_t>(1U << 0);

struct UiMessage {
    LorawanUiState state;
    bool force_full_refresh;
};

const char *kTag = "class_c_ui";
// The length-one queue deliberately keeps only the newest complete snapshot.
QueueHandle_t s_queue;
EventGroupHandle_t s_events;
// This gate keeps a display transfer outside multi-step LR1121 operations.
SemaphoreHandle_t s_radio_critical;
esp_err_t s_first_result = ESP_ERR_INVALID_STATE;

/** Formats a bounded seven-line snapshot and performs one panel refresh. */
esp_err_t render(const UiMessage &message)
{
    char lines[kLineCount][kLineChars + 1U] = {};
    const char *line_ptrs[kLineCount] = {};
    const LorawanUiState &s = message.state;

    snprintf(lines[0], sizeof(lines[0]), "STATE %.27s", s.status);
    if (s.rx2_valid) {
        snprintf(lines[1], sizeof(lines[1]), "%.9s RX2 %.3f D%u",
                 s.region, (double)s.rx2_frequency_mhz,
                 (unsigned)s.rx2_datarate);
    } else {
        snprintf(lines[1], sizeof(lines[1]), "%.12s RX2 --", s.region);
    }
    snprintf(lines[2], sizeof(lines[2]), "UP %" PRIu32 " DOWN %" PRIu32 " %s",
             s.uplink_count, s.downlink_count,
             s.session_restored ? "RESTORED" : "NEW");
    snprintf(lines[3], sizeof(lines[3]), "FCNT %" PRIu32 " LAST PORT %u",
             s.fcnt_up, (unsigned)s.last_port);
    if (s.signal_valid) {
        snprintf(lines[4], sizeof(lines[4]), "RSSI %.1f SNR %.1f",
                 (double)s.rssi, (double)s.snr);
    } else {
        snprintf(lines[4], sizeof(lines[4]), "RSSI -- SNR --");
    }
    snprintf(lines[5], sizeof(lines[5]), "EUI %016" PRIX64 " C %" PRId32,
             s.join_eui, s.last_code);
    snprintf(lines[6], sizeof(lines[6]), "%.33s", s.detail);

    for (size_t i = 0; i < kLineCount; ++i) {
        line_ptrs[i] = lines[i];
    }
    return board_epaper_show_lines("LR1121 CLASS C", line_ptrs,
                                   kLineCount,
                                   message.force_full_refresh);
}

void merge_message(UiMessage &current, const UiMessage &newer)
{
    const bool force_full =
        current.force_full_refresh || newer.force_full_refresh;
    current = newer;
    current.force_full_refresh = force_full;
}

/**
 * Coalesces status bursts while enforcing the configured minimum refresh
 * interval. Radio processing continues while this task waits on its queue.
 */
void display_task(void *)
{
    int64_t last_attempt_us = 0;
    bool have_successful_refresh = false;
    bool first_attempt_reported = false;

    while (true) {
        UiMessage message = {};
        if (xQueueReceive(s_queue, &message, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        UiMessage newer = {};
        while (xQueueReceive(s_queue, &newer, 0) == pdTRUE) {
            merge_message(message, newer);
        }

        if (last_attempt_us != 0) {
            const int64_t deadline_us =
                last_attempt_us +
                (int64_t)LORAWAN_DISPLAY_MIN_INTERVAL_MS * 1000LL;
            while (esp_timer_get_time() < deadline_us) {
                const int64_t remaining_us =
                    deadline_us - esp_timer_get_time();
                uint32_t wait_ms =
                    (uint32_t)((remaining_us + 999LL) / 1000LL);
                if (wait_ms == 0U) {
                    wait_ms = 1U;
                }
                if (xQueueReceive(s_queue, &newer,
                                  pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
                    merge_message(message, newer);
                }
            }
        }

        while (xQueueReceive(s_queue, &newer, 0) == pdTRUE) {
            merge_message(message, newer);
        }
        message.force_full_refresh =
            message.force_full_refresh || !have_successful_refresh;

        lorawan_ui_radio_critical_enter();
        const esp_err_t err = render(message);
        lorawan_ui_radio_critical_exit();
        last_attempt_us = esp_timer_get_time();
        if (err == ESP_OK) {
            have_successful_refresh = true;
        } else {
            ESP_LOGW(kTag, "e-paper update failed: %s",
                     esp_err_to_name(err));
        }

        if (!first_attempt_reported) {
            s_first_result = err;
            first_attempt_reported = true;
            xEventGroupSetBits(s_events, kFirstAttemptDone);
        }
    }
}

}  // namespace

esp_err_t lorawan_ui_start()
{
    if (s_queue != nullptr) {
        return ESP_OK;
    }

    s_queue = xQueueCreate(1, sizeof(UiMessage));
    if (s_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    s_events = xEventGroupCreate();
    if (s_events == nullptr) {
        vQueueDelete(s_queue);
        s_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    s_radio_critical = xSemaphoreCreateMutex();
    if (s_radio_critical == nullptr) {
        vEventGroupDelete(s_events);
        vQueueDelete(s_queue);
        s_events = nullptr;
        s_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t created = xTaskCreate(
        display_task, "lorawan_class_c_ui", LORAWAN_DISPLAY_TASK_STACK,
        nullptr, LORAWAN_DISPLAY_TASK_PRIORITY, nullptr);
    if (created != pdPASS) {
        vSemaphoreDelete(s_radio_critical);
        vEventGroupDelete(s_events);
        vQueueDelete(s_queue);
        s_radio_critical = nullptr;
        s_events = nullptr;
        s_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t lorawan_ui_post(const LorawanUiState &state,
                          bool force_full_refresh)
{
    if (s_queue == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    UiMessage message = {};
    message.state = state;
    message.force_full_refresh = force_full_refresh;

    // Preserve a queued full-refresh request when replacing an older snapshot.
    UiMessage queued = {};
    if (xQueueReceive(s_queue, &queued, 0) == pdTRUE) {
        message.force_full_refresh =
            message.force_full_refresh || queued.force_full_refresh;
    }
    return xQueueOverwrite(s_queue, &message) == pdTRUE
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t lorawan_ui_wait_first_attempt(uint32_t timeout_ms)
{
    if (s_events == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const EventBits_t bits = xEventGroupWaitBits(
        s_events, kFirstAttemptDone, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & kFirstAttemptDone) != 0U
               ? s_first_result
               : ESP_ERR_TIMEOUT;
}

void lorawan_ui_radio_critical_enter()
{
    if (s_radio_critical != nullptr) {
        (void)xSemaphoreTake(s_radio_critical, portMAX_DELAY);
    }
}

void lorawan_ui_radio_critical_exit()
{
    if (s_radio_critical != nullptr) {
        (void)xSemaphoreGive(s_radio_critical);
    }
}
