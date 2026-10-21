#pragma once

#include "board_epaper.h"

/* Display the selected static artwork once, using a full refresh. */
esp_err_t photo_display_show(const board_epaper_t *panel);
