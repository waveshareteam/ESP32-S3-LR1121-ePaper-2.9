#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *status;
    const char *detail;
    uint32_t dev_addr;
    uint32_t fcnt_up;
    uint32_t nfcnt_down;
    uint32_t afcnt_down;
    uint32_t application_sequence;
    bool tx_valid;
    uint8_t tx_port;
    size_t tx_length;
    bool rx_valid;
    int16_t rx_window;
    uint8_t rx_port;
    size_t rx_length;
    bool signal_valid;
    float rssi_dbm;
    float snr_db;
    int32_t last_code;
} abp_ui_state_t;

class AbpStatusUi {
public:
    AbpStatusUi();

    abp_ui_state_t &state(void);
    void update(const char *status, const char *detail, int32_t code);
    void error(const char *detail, int32_t code);

private:
    abp_ui_state_t state_ = {};
    char status_[32] = {};
    char detail_[48] = {};
    bool needs_full_refresh_ = true;
};
