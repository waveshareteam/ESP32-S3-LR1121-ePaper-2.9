# Waveshare ESP Board Definitions

This component contains Waveshare board definitions for ESP Board Manager.
Its layout follows `espressif/esp_boards`: each supported board has one direct
child directory containing `board_info.yaml`, `board_peripherals.yaml`, and
`board_devices.yaml`.

The `esp32_s3_lr1121_epaper_2_9` board models the onboard GPIO21 WS2812 as a
native Board Manager `led_strip` device using the RMT backend and GRB wire
order.

This package intentionally contains no runtime adapters or chip drivers. Each
consuming example keeps its board-specific runtime support in its own
`components/esp32_s3_lr1121_epaper_2_9_support` directory, so this board
definition package can be maintained and published independently.
