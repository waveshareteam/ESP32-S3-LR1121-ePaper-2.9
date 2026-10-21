# Repository-local e-paper driver

This component is a minimal source copy of tuanpmt/esp_epaper 2.0.0. The
upstream MIT license is retained in LICENSE.

Examples 3 through 8 use this single copy so the SSD16xx fast-refresh sequence
cannot be lost when ESP-IDF regenerates managed_components. The local change
keeps the controller awake after writing RAM, then reapplies the rotated RAM
window and cursor before the next fast update.

Only files required to build the component are kept here; upstream examples,
images, and package checksum metadata are intentionally omitted.