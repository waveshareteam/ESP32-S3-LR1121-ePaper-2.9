# Repository-local e-paper driver

This component is a minimal source copy of tuanpmt/esp_epaper 2.0.0. The
upstream MIT license is retained in LICENSE.

Examples 3 through 8 use this single copy so the SSD16xx fast-refresh sequence
cannot be lost when ESP-IDF regenerates managed_components. The local change
keeps the controller awake after writing RAM, then reapplies the rotated RAM
window and cursor before the next fast update.

Only files required to build the component are kept here; upstream examples,
images, and package checksum metadata are intentionally omitted.

Example 03 additionally contains an explicit Waveshare 2.9inch V2 Gray4 photo
adapter in `src/controllers/ssd1680_290_gray4.c`. It preserves the upstream
Waveshare permission notice and does not change the generic SSD16xx registry
or B/W framebuffer. This panel-specific adapter must not be treated as
automatic four-gray support for all 128x296 panels. See the example README
for source provenance, selection and hardware-validation limitations.
