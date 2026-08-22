/**
 * @file board_lorawan_radio.h
 * @brief Creates the board-specific LR1121 RadioLib instance.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

class LR1121;

/**
 * @brief Initialize the singleton LR1121 used by the LoRaWAN application.
 *
 * The adapter validates the proven board pinout, configures the high-power RF
 * path, and supplies the board TCXO voltage to RadioLib.
 *
 * @param initial_frequency_mhz Initial LoRa frequency passed to RadioLib.
 * @param initial_power_dbm Initial transmit power passed to RadioLib.
 * @param[out] radio Receives the initialized LR1121 instance on success.
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t board_lorawan_radio_init(float initial_frequency_mhz,
                                   int8_t initial_power_dbm,
                                   LR1121 **radio);

/**
 * @brief Return the most recent RadioLib result from radio initialization.
 *
 * @return A RadioLib status code; zero indicates no RadioLib error.
 */
int16_t board_lorawan_radio_last_error(void);
