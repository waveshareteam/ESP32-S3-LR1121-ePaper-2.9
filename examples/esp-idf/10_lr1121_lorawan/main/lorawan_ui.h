/*
 * Compact seven-line e-paper status model for the LoRaWAN example.
 *
 * The UI object owns copied status strings so callers may pass stack buffers.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** Snapshot rendered by one synchronous e-paper refresh. */
typedef struct {
    // Text and deployment fields.
    const char *status;
    const char *detail;
    const char *region;
    uint8_t sub_band;
    uint8_t uplink_port;
    uint64_t join_eui;
    uint32_t interval_seconds;
    bool adr_enabled;
    bool confirmed_uplink;
    // Application and LoRaWAN counters.
    uint32_t uplink_count;
    uint32_t downlink_count;
    uint32_t fcnt_up;
    uint32_t next_sequence;
    // Actual TX frequency and standard-channel coverage for field testing.
    bool tx_frequency_valid;
    float tx_frequency_mhz;
    uint8_t default_channels_seen;
    uint8_t default_channels_total;
    // Last valid join/downlink signal measurement.
    bool signal_valid;
    float rssi;
    float snr;
    // Last RadioLib or ESP-IDF status code shown to the user.
    int32_t last_code;
} lorawan_ui_state_t;

/**
 * Owns status buffers and selects full refresh only for the first good frame.
 */
class LoRaWANStatusUi {
public:
    LoRaWANStatusUi();

    /** Return mutable counters and signal fields for the workflow. */
    lorawan_ui_state_t &state(void);
    /** Copy status text and synchronously render one complete screen. */
    void update(const char *status, const char *detail, int32_t code);

private:
    lorawan_ui_state_t state_ = {};
    // Fixed buffers keep stack-based caller strings valid after update().
    char status_[32] = {};
    char detail_[32] = {};
    bool needs_full_refresh_ = true;
};
