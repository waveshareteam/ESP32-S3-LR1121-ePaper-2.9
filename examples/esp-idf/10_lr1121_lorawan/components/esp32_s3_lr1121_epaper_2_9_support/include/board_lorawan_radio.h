/*
 * Board-specific RadioLib LR1121 factory.
 *
 * The implementation owns shared-SPI arbitration, RF switch setup, TCXO
 * voltage, and selection of the LR1121 high-power PA path.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

class LR1121;

/**
 * Initialize the singleton LR1121 instance and return a borrowed pointer.
 *
 * Repeated successful calls return the same object. The caller must not delete
 * it because the board adapter owns the RadioLib HAL, Module, and radio.
 */
esp_err_t board_lorawan_radio_init(float initial_frequency_mhz,
                                   int8_t initial_power_dbm,
                                   LR1121 **radio);
