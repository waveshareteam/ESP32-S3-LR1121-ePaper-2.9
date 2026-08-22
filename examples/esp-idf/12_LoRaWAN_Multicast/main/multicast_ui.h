#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Asynchronous e-paper status interface.
 *
 * Radio work posts value snapshots and never waits for a panel refresh. The
 * display task owns the queued snapshot and shares the physical SPI bus through
 * the board support lock.
 */

/* Complete render state; no pointer in this snapshot may outlive the caller. */
struct MulticastUiState {
    char status[24];
    char detail[32];
    uint32_t multicast_address;
    uint8_t group_id;
    uint64_t join_eui;
    uint8_t last_port;
    uint32_t unicast_rx_count;
    uint32_t multicast_rx_count;
    uint32_t tx_count;
    bool session_restored;
    bool signal_valid;
    float rssi;
    float snr;
    int32_t last_code;
};

/* Starts the display worker. Radio initialization does not depend on it. */
esp_err_t multicast_ui_start();

/*
 * Replaces any queued snapshot so stale display work cannot accumulate.
 * A forced full refresh is useful after startup or visible panel artifacts.
 */
esp_err_t multicast_ui_post(const MulticastUiState &state,
                            bool force_full_refresh = false);
