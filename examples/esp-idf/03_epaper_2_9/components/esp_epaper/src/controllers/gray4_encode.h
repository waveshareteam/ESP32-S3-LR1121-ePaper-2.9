#pragma once

#include <stdint.h>

/* Official V2 Gray4 encoding: 0x24 carries inverse bit 0, 0x26 inverse bit 1.
 * Eight pixels in two packed bytes become one byte in each RAM plane. */
static inline uint8_t gray4_plane_byte(const uint8_t *pixels, unsigned plane)
{
    uint8_t result = 0;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned level = (pixels[i / 4] >> (6 - 2 * (i % 4))) & 3;
        result = (uint8_t)((result << 1) | (((~level) >> plane) & 1));
    }
    return result;
}
