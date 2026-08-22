#include "abp_ui.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "board_epaper.h"
#include "esp_log.h"

static constexpr size_t kLineChars = 34U;
static constexpr size_t kLineCount = 7U;
static const char *const kTag = "abp_ui";

AbpStatusUi::AbpStatusUi()
{
    state_.status = status_;
    state_.detail = detail_;
    snprintf(status_, sizeof(status_), "CONFIG REQUIRED");
    snprintf(detail_, sizeof(detail_), "configure credentials");
}

abp_ui_state_t &AbpStatusUi::state(void)
{
    return state_;
}

static esp_err_t show_state(const abp_ui_state_t *state,
                            bool force_full_refresh)
{
    if (state == nullptr || state->status == nullptr ||
        state->detail == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Fixed line geometry prevents stale text during partial e-paper refreshes.
    char lines[kLineCount][kLineChars + 1U] = {};
    const char *line_ptrs[kLineCount] = {};

    snprintf(lines[0], sizeof(lines[0]), "STATE %.28s", state->status);
    snprintf(lines[1], sizeof(lines[1]), "DEVADDR %08" PRIX32,
             state->dev_addr);
    snprintf(lines[2], sizeof(lines[2]), "FCUP %" PRIu32 " NFCNT %" PRIu32,
             state->fcnt_up, state->nfcnt_down);
    snprintf(lines[3], sizeof(lines[3]), "AFCNT %" PRIu32 " APP %" PRIu32,
             state->afcnt_down, state->application_sequence);

    if (state->tx_valid && state->rx_valid) {
        snprintf(lines[4], sizeof(lines[4]),
                 "TX P%u L%zu RX%d P%u L%zu",
                 (unsigned)state->tx_port, state->tx_length,
                 (int)state->rx_window, (unsigned)state->rx_port,
                 state->rx_length);
    } else if (state->tx_valid) {
        snprintf(lines[4], sizeof(lines[4]), "TX P%u L%zu RX --",
                 (unsigned)state->tx_port, state->tx_length);
    } else if (state->rx_valid) {
        snprintf(lines[4], sizeof(lines[4]), "TX -- RX%d P%u L%zu",
                 (int)state->rx_window, (unsigned)state->rx_port,
                 state->rx_length);
    } else {
        snprintf(lines[4], sizeof(lines[4]), "TX -- RX --");
    }

    if (state->signal_valid) {
        snprintf(lines[5], sizeof(lines[5]), "RSSI %.1f SNR %.1f",
                 (double)state->rssi_dbm, (double)state->snr_db);
    } else {
        snprintf(lines[5], sizeof(lines[5]), "RSSI -- SNR --");
    }

    snprintf(lines[6], sizeof(lines[6]), "CODE %" PRId32 " %.20s",
             state->last_code, state->detail);

    for (size_t i = 0; i < kLineCount; ++i) {
        line_ptrs[i] = lines[i];
    }

    return board_epaper_show_lines("LR1121 LoRaWAN ABP", line_ptrs,
                                   kLineCount, force_full_refresh);
}

void AbpStatusUi::update(const char *status,
                         const char *detail,
                         int32_t code)
{
    snprintf(status_, sizeof(status_), "%s",
             status != nullptr ? status : "ERROR");
    snprintf(detail_, sizeof(detail_), "%s",
             detail != nullptr ? detail : "no detail");
    state_.last_code = code;

    const esp_err_t err = show_state(&state_, needs_full_refresh_);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "e-paper status update failed: %s",
                 esp_err_to_name(err));
        return;
    }

    needs_full_refresh_ = false;
}

void AbpStatusUi::error(const char *detail, int32_t code)
{
    update("ERROR", detail, code);
}
