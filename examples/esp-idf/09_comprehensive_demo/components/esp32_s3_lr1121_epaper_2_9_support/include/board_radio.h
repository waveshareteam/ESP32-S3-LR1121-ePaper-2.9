#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float frequency_mhz;
    float bandwidth_khz;
    uint8_t spreading_factor;
    uint8_t coding_rate;
    uint8_t sync_word;
    int8_t output_power_dbm;
    uint16_t preamble_len;
    float tcxo_voltage;
} board_radio_config_t;

#define BOARD_RADIO_CONFIG_DEFAULT() { \
    .frequency_mhz = 868.0f, \
    .bandwidth_khz = 125.0f, \
    .spreading_factor = 7, \
    .coding_rate = 5, \
    .sync_word = 0x12, \
    .output_power_dbm = 22, \
    .preamble_len = 8, \
    .tcxo_voltage = 3.0f, \
}

typedef struct {
    int16_t state;
    bool initialized;
    uint32_t tx_count;
    uint32_t tx_fail_count;
    uint32_t rx_count;
    uint32_t rx_error_count;
    float last_rssi;
    float last_snr;
    float last_frequency_error_hz;
    bool frequency_error_available;
    char last_payload[80];
    board_radio_config_t config;
} board_radio_status_t;

esp_err_t board_radio_init(const board_radio_config_t *config, board_radio_status_t *status);
esp_err_t board_radio_send(board_radio_status_t *status, const char *payload);
esp_err_t board_radio_ping(board_radio_status_t *status);
esp_err_t board_radio_receive(board_radio_status_t *status, uint32_t timeout_ms);
esp_err_t board_radio_sleep(void);
esp_err_t board_radio_isolate_powered_off_pins(void);
esp_err_t board_radio_prepare_deep_sleep(void);
const char *board_radio_state_name(int16_t state);

#ifdef __cplusplus
}
#endif
