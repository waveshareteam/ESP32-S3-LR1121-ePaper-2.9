#pragma once

#include <stdint.h>

#include "esp_err.h"

class LR1121;

esp_err_t board_lorawan_radio_init(LR1121 **radio);
