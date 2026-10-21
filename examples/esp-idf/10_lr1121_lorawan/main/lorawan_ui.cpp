/*
 * Formats the LoRaWAN status page and performs synchronous full/fast refresh.
 * The workflow calls this only outside RadioLib's RX1/RX2 windows.
 */
#include "lorawan_ui.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "board_epaper.h"
#include "esp_log.h"
#include "lorawan_config.h"

#define LORAWAN_UI_LINE_CHARS 34U
#define LORAWAN_UI_LINE_COUNT 7U

static const char *TAG = "lorawan_ui";

// Bind invariant deployment fields and internal text storage once.
LoRaWANStatusUi::LoRaWANStatusUi()
{
    state_.status = status_;
    state_.detail = detail_;
    state_.region = LORAWAN_REGION_NAME;
    state_.sub_band = LORAWAN_SUB_BAND;
    state_.uplink_port = LORAWAN_UPLINK_PORT;
    state_.join_eui = LORAWAN_JOIN_EUI;
    state_.interval_seconds = LORAWAN_UPLINK_INTERVAL_SECONDS;
    state_.adr_enabled = LORAWAN_ADR_ENABLED != 0;
    state_.confirmed_uplink = LORAWAN_CONFIRMED_UPLINK != 0;
#if !LORAWAN_SINGLE_CHANNEL_MODE && \
    (LORAWAN_REGION_SELECTION == LORAWAN_REGION_EU868)
    state_.default_channels_total = 3U;
#endif
}

lorawan_ui_state_t &LoRaWANStatusUi::state(void)
{
    return state_;
}

// Convert the model into seven fixed-width lines for the 2.9-inch panel.
static esp_err_t show_state(const lorawan_ui_state_t *state,
                            bool force_full_refresh)
{
    if (state == nullptr || state->status == nullptr ||
        state->detail == nullptr || state->region == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    char lines[LORAWAN_UI_LINE_COUNT][LORAWAN_UI_LINE_CHARS + 1U] = {};
    const char *line_ptrs[LORAWAN_UI_LINE_COUNT] = {};

    snprintf(lines[0], sizeof(lines[0]), "STATE %.27s", state->status);
    snprintf(lines[1], sizeof(lines[1]), "JOIN EUI %016" PRIX64,
             state->join_eui);
    snprintf(lines[2], sizeof(lines[2]), "UP %" PRIu32 " DOWN %" PRIu32,
             state->uplink_count, state->downlink_count);
    snprintf(lines[3], sizeof(lines[3]), "FCNT %" PRIu32 " SEQ %" PRIu32,
             state->fcnt_up, state->next_sequence);
    // Keep an explicit placeholder until a valid join/downlink measurement exists.
    if (state->signal_valid) {
        snprintf(lines[4], sizeof(lines[4]),
                 "LAST RSSI %.1f SNR %.1f",
                 (double)state->rssi, (double)state->snr);
    } else {
        snprintf(lines[4], sizeof(lines[4]), "LAST RSSI -- SNR --");
    }
    snprintf(lines[5], sizeof(lines[5]), "CODE %" PRId32 " %.18s",
             state->last_code, state->detail);
    if (state->tx_frequency_valid && state->default_channels_total > 0U) {
        snprintf(lines[6], sizeof(lines[6]),
                 "TX %.4fMHz CH %u/%u",
                 (double)state->tx_frequency_mhz,
                 (unsigned)state->default_channels_seen,
                 (unsigned)state->default_channels_total);
    } else if (state->tx_frequency_valid) {
        snprintf(lines[6], sizeof(lines[6]), "TX %.4fMHz",
                 (double)state->tx_frequency_mhz);
    } else {
        snprintf(lines[6], sizeof(lines[6]), "PORT %u EVERY %" PRIu32 "s",
                 (unsigned)state->uplink_port, state->interval_seconds);
    }

    // The board API consumes an array of stable line pointers synchronously.
    for (size_t i = 0; i < LORAWAN_UI_LINE_COUNT; ++i) {
        line_ptrs[i] = lines[i];
    }
    return board_epaper_show_lines("LR1121 LoRaWAN", line_ptrs,
                                   LORAWAN_UI_LINE_COUNT,
                                   force_full_refresh);
}

/*
 * Copy caller-owned text before rendering. A failed first refresh keeps the
 * full-refresh request set so the next successful frame establishes a base.
 */
void LoRaWANStatusUi::update(const char *status,
                             const char *detail,
                             int32_t code)
{
    snprintf(status_, sizeof(status_), "%s",
             status != nullptr ? status : "UNKNOWN");
    snprintf(detail_, sizeof(detail_), "%s",
             detail != nullptr ? detail : "no detail");
    state_.last_code = code;

    const esp_err_t err = show_state(&state_, needs_full_refresh_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "e-paper status update failed: %s",
                 esp_err_to_name(err));
        return;
    }
    needs_full_refresh_ = false;
}
