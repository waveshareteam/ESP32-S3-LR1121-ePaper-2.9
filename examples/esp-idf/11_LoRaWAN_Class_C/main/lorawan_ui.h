/**
 * @file lorawan_ui.h
 * @brief Non-blocking e-paper status interface for the LoRaWAN runtime.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * Complete, pointer-free display snapshot copied through the UI queue.
 * Producers may reuse or destroy their source state immediately after posting.
 */
struct LorawanUiState {
    char status[24];
    char detail[34];
    char region[12];
    uint64_t join_eui;
    uint32_t uplink_count;
    uint32_t downlink_count;
    uint32_t fcnt_up;
    uint8_t last_port;
    bool session_restored;
    bool class_c_active;
    bool signal_valid;
    float rssi;
    float snr;
    bool rx2_valid;
    float rx2_frequency_mhz;
    uint8_t rx2_datarate;
    int32_t last_code;
};

/** Starts the display worker, snapshot queue, and radio/display gate. */
esp_err_t lorawan_ui_start();

/**
 * Replaces any pending snapshot with the latest state without waiting for an
 * e-paper refresh. A requested full refresh survives queue coalescing.
 */
esp_err_t lorawan_ui_post(const LorawanUiState &state,
                          bool force_full_refresh = false);

/** Waits for the first bounded display attempt, not necessarily a success. */
esp_err_t lorawan_ui_wait_first_attempt(uint32_t timeout_ms);

/**
 * Prevents an e-paper SPI transaction from interrupting a multi-step radio
 * operation. This call does not transfer RadioLib ownership to the UI task.
 */
void lorawan_ui_radio_critical_enter();

/** Releases the radio/display gate acquired by the matching enter call. */
void lorawan_ui_radio_critical_exit();
