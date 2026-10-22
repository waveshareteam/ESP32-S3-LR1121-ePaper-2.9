#pragma once

#include <stddef.h>
#include "epaper.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Waveshare 2.9inch V2 waveform ONLY, not all SSD1680 128x296 panels.
 * Input: 9472 bytes in native 128x296 orientation, four pixels/byte,
 * MSB first. Levels: 0 black, 1 dark gray, 2 light gray, 3 white.
 * Caller owns SPI locking and must sleep/reset the panel after this call
 * before using the normal B/W refresh API (also on an error).
 */
esp_err_t epd_show_gray4_v2(epd_handle_t panel, const uint8_t *pixels, size_t size);

#ifdef __cplusplus
}
#endif
