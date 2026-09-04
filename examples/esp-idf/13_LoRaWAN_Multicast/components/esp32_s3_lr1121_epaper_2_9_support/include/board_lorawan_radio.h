#pragma once

#include <stdint.h>

#include "esp_err.h"

class LR1121;

/*
 * Creates the single board-owned LR1121 RadioLib instance.
 *
 * The returned pointer remains owned by this component and is reused on later
 * calls. The initial frequency and power are bootstrap values; LoRaWAN applies
 * its regional channel settings after initialization.
 */
esp_err_t board_lorawan_radio_init(float initial_frequency_mhz,
                                   int8_t initial_power_dbm,
                                   LR1121 **radio);
