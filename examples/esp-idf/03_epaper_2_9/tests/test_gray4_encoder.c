#include <assert.h>
#include <stdio.h>
#include "../components/esp_epaper/src/controllers/gray4_encode.h"

int main(void)
{
    /* Explicit reference table from Waveshare's EPD_2IN9_V2_4GrayDisplay.
     * Indexed by level: black, dark gray, light gray, white. */
    static const unsigned expected[2][4] = {{1, 0, 1, 0}, {1, 1, 0, 0}};
    for (unsigned input = 0; input <= 0xffff; ++input) {
        uint8_t pixels[2] = {input >> 8, input & 0xff};
        for (unsigned plane = 0; plane < 2; ++plane) {
            unsigned output = 0;
            for (unsigned i = 0; i < 8; ++i) {
                unsigned level = (input >> (14 - i * 2)) & 3;
                output = (output << 1) | expected[plane][level];
            }
            assert(gray4_plane_byte(pixels, plane) == output);
        }
    }
    puts("PASS: both RAM planes match the official map for all 65,536 eight-pixel inputs");
}
