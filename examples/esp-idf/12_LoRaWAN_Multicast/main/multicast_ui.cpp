#include "multicast_ui.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "board_epaper.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "multicast_config.h"

/*
 * The radio task publishes status snapshots to a one-element queue. This file
 * performs all slow e-paper rendering on a separate task and rate-limits bus
 * usage without delaying LoRaWAN processing.
 */

namespace {

/* The fixed row count matches the compact 296 x 128 status layout. */
constexpr size_t kLineCount = 7U;
constexpr size_t kLineChars = 34U;

struct UiMessage {
    MulticastUiState state;
    bool force_full_refresh;
};

const char *kTag = "multicast_ui";
QueueHandle_t s_queue;

void wait_until_refresh_allowed(int64_t last_refresh_us)
{
    const int64_t minimum_interval_us =
        static_cast<int64_t>(MULTICAST_DISPLAY_MIN_INTERVAL_MS) * 1000LL;
    const int64_t remaining_us =
        minimum_interval_us - (esp_timer_get_time() - last_refresh_us);
    if (remaining_us > 0) {
        const uint32_t wait_ms =
            static_cast<uint32_t>((remaining_us + 999LL) / 1000LL);
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

/* Formats bounded, non-secret display rows from one immutable snapshot. */
esp_err_t render(const UiMessage &message)
{
    char lines[kLineCount][kLineChars + 1U] = {};
    const char *line_ptrs[kLineCount] = {};
    const MulticastUiState &s = message.state;

    snprintf(lines[0], sizeof(lines[0]), "STATE %.27s", s.status);
    snprintf(lines[1], sizeof(lines[1]), "MC%u %08" PRIX32 " %s",
             static_cast<unsigned>(s.group_id), s.multicast_address,
             s.session_restored ? "RESTORED" : "NEW");
    snprintf(lines[2], sizeof(lines[2]), "UNI RX %" PRIu32 "  MC RX %" PRIu32,
             s.unicast_rx_count, s.multicast_rx_count);
    snprintf(lines[3], sizeof(lines[3]), "TX %" PRIu32 "  PORT %u",
             s.tx_count, static_cast<unsigned>(s.last_port));
    if (s.signal_valid) {
        snprintf(lines[4], sizeof(lines[4]), "RSSI %.1f  SNR %.1f",
                 static_cast<double>(s.rssi), static_cast<double>(s.snr));
    } else {
        snprintf(lines[4], sizeof(lines[4]), "RSSI --  SNR --");
    }
    snprintf(lines[5], sizeof(lines[5]), "EUI %016" PRIX64 " C %" PRId32,
             s.join_eui, s.last_code);
    snprintf(lines[6], sizeof(lines[6]), "%.33s", s.detail);

    for (size_t i = 0; i < kLineCount; ++i) {
        line_ptrs[i] = lines[i];
    }
    return board_epaper_show_lines("LR1121 MULTICAST", line_ptrs,
                                   kLineCount,
                                   message.force_full_refresh);
}

void display_task(void *)
{
    int64_t last_refresh_us = 0;
    bool first_refresh = true;

    while (true) {
        UiMessage message = {};
        if (xQueueReceive(s_queue, &message, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Coalesce bursts before taking the shared display/radio SPI bus. */
        UiMessage newer = {};
        while (xQueueReceive(s_queue, &newer, 0) == pdTRUE) {
            message = newer;
        }

        if (!first_refresh) {
            wait_until_refresh_allowed(last_refresh_us);
            while (xQueueReceive(s_queue, &newer, 0) == pdTRUE) {
                message = newer;
            }
        }

        /* The first update establishes a clean full-refresh baseline. */
        message.force_full_refresh =
            message.force_full_refresh || first_refresh;
        const esp_err_t err = render(message);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "e-paper update failed: %s",
                     esp_err_to_name(err));
        } else {
            last_refresh_us = esp_timer_get_time();
            first_refresh = false;
        }
    }
}

}  // namespace

esp_err_t multicast_ui_start()
{
    if (s_queue != nullptr) {
        return ESP_OK;
    }

    /* One slot is intentional: only the newest state is useful to the panel. */
    s_queue = xQueueCreate(1, sizeof(UiMessage));
    if (s_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t created = xTaskCreate(
        display_task,
        "multicast_ui",
        MULTICAST_DISPLAY_TASK_STACK,
        nullptr,
        MULTICAST_DISPLAY_TASK_PRIORITY,
        nullptr);
    if (created != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t multicast_ui_post(const MulticastUiState &state,
                            bool force_full_refresh)
{
    if (s_queue == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    UiMessage message = {
        .state = state,
        .force_full_refresh = force_full_refresh,
    };
    /* Copying the complete message keeps producer-owned memory out of the task. */
    return xQueueOverwrite(s_queue, &message) == pdTRUE
               ? ESP_OK
               : ESP_FAIL;
}
