# ESP32-S3 LR1121 e-Paper 2.9 ESP-IDF examples

These numbered examples share one board definition and use Espressif's
`esp_board_manager`. Example 01 acquires the onboard WS2812 through Board Manager
and uses its native `led_strip` handle for color updates.

| Directory | Function |
| --- | --- |
| `01_ws2812_led` | Onboard WS2812 red/green/blue and color-breathing test |
| `02_i2c_scan` | I2C scan for the PCF85063A and AXP2602 |
| `03_epaper_2_9` | 2.9-inch e-paper full/fast refresh test |
| `04_bat_axp2602` | AXP2602 battery-gauge data |
| `05_rtc_pcf85063` | PCF85063A RTC and interrupt test |
| `06_lr1121_pingpong` | LR1121 ping-pong test |
| `07_sd_card` | microSD 1-bit SDMMC mount, card info, file write/read/cleanup test |
| `08_deep_sleep` | Deep-sleep and wake-up test |
| `09_comprehensive_demo` | Combined board test |
| `10_lr1121_lorawan` | LR1121 LoRaWAN Class A OTAA example |
| `11_LoRaWAN_ABP` | LR1121 LoRaWAN Class A ABP example with persistent frame counters |
| `12_LoRaWAN_Class_C` | LR1121 LoRaWAN Class C example |
| `13_LoRaWAN_Multicast` | LR1121 LoRaWAN multicast example |

## Board Manager integration

Examples 01–06 and 08–13 depend on `waveshare/waveshare_boards`, which
brings in `espressif/esp_board_manager` transitively. Example 07 uses SDMMC
and FATFS directly. Install the bootstrap helper in the active IDF environment:

```bash
python -m pip install esp-bmgr-assist==0.8.3
```

From the example directory, `idf.py bmgr -l` downloads dependencies and lists
registry boards without a preceding `idf.py reconfigure`:

```bash
idf.py bmgr -l
```

Board definitions belong to `waveshare/waveshare_boards`; this repository does
not include a `boards` directory or another local copy. The downloaded 0.1.2
package does not contain `esp32_s3_lr1121_epaper_2_9` yet. Until a component
release includes it, manually copy the matching board folder supplied by the
board-package maintainer into this example's downloaded component:

```text
managed_components/waveshare__waveshare_boards/boards/esp32_s3_lr1121_epaper_2_9/
```

The folder must include `board_info.yaml`, `board_peripherals.yaml`, and
`board_devices.yaml`. Repeat this local preparation for each example that uses
Board Manager, and after deleting or replacing downloaded components. Once the
published component includes the board, this manual step is unnecessary.

Then list and select the board from the component and build:

```bash
idf.py bmgr -l
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9
idf.py build
```

`components/gen_bmgr_codes`, downloaded components, lock files and build outputs
are intentionally omitted. Do not commit the manually copied board folder or
generated sources. Example 07 requires none of these Board Manager steps.
Unattended CI requires a published component containing this board; it reports
a missing-board error until that component is available.

All 13 examples declare IDF >=5.5.0,<6.3.0 and target ESP32-S3. Use the stable
5.5 toolchain for the user manual; 6.2 compatibility is checked against a pinned
development SDK rather than a floating master branch. See
[framework compatibility](../../docs/idf-compatibility.md) for exact versions,
build results and the separate experimental CI job.

Board Manager owns buses, GPIOs, power control and the native WS2812/RMT device.
Examples carry their own support/display/font components; changes to common
code must be kept consistent across those copies.

Examples 04, 08, and 09 declare `lewisxhe/sensorlib` 0.4.1 in their `main`
component and compile the AXP2602 users as C++. They instantiate
`GaugeAXP2602` directly after acquiring the Board Manager I2C bus; the board
support component no longer contains an AXP2602 driver, C wrapper, or custom
device callback. SensorLib 0.4.1 does not report every individual register-read
or configuration-write failure, so hardware validation must include
plausibility checks over repeated samples.

## Shared e-paper font

Every example that renders text on the 2.9-inch e-paper (examples 03 through
06 and 08 through 13) uses the local `components/epaper_ui_font` component.
It provides the same Terminus Bold 8x14
ASCII 1-bpp font and crisp, non-antialiased LVGL label rendering established by
the comprehensive demo.
The font covers U+0020 through U+007E; `06_lr1121_pingpong` escapes non-ASCII
payload bytes as `\xNN` before display. The component README records the
upstream source and its SIL Open Font License 1.1 terms.

Each numbered directory remains an independent firmware project, so each
resulting firmware necessarily links one copy of the font object even though
each example keeps a source copy.

## E-paper refresh policy

Examples 03 through 06 and 09 through 13 use a full waveform for the first
frame, up to 10 successful full-screen fast updates, and then another full
waveform to clear accumulated ghosting. A failed panel flush forces the next
attempt back to full. `FAST` is the controller's accelerated full-screen
waveform; it is not `EPD_UPDATE_PARTIAL` and does not change the LVGL screen
layout.

Examples 03 through 06 and 08 through 13 resolve esp_epaper from their own
components/esp_epaper copies. There is no separate repository-level component required for these copies.

`08_deep_sleep` remains full-refresh-only. It displays one frame, powers down the
e-paper controller, and enters ESP deep sleep, so every wake starts with a new
controller and framebuffer state.

The `01_ws2812_led` directory tests the single onboard WS2812 on GPIO21. See
`01_ws2812_led/README.md` for the test sequence, brightness limit, hardware-evidence boundary, and flash command.

Building requires `components/gen_bmgr_codes`. Listing boards does not generate
that component. If it is missing, select the board as described below before
running `idf.py build`. For ordinary clean rebuilds, retain the generated sources
and run `idf.py fullclean` followed by `idf.py build`.

## Regenerating board code

Regenerate after updating the board definition in the downloaded component or
after cleaning the generated component. Run the selection command above from
each affected example directory.
The generator can replace `sdkconfig`, so preserve deliberate configuration
changes first. Generated files and manually supplied board YAML stay local.
Maintain board-definition changes in the separate board-package repository.

## Hardware checks

The WS2812 definition uses GPIO21, one pixel, GRB order, and 10 MHz RMT. It
shares the active-high GPIO38 supply with the e-paper, so confirm the RED,
GREEN, and BLUE stages and the shared-power behavior on hardware. Software
configuration also cannot establish the fitted AXP2602 sense resistor, CSP/CSN
direction, e-paper power polarity, panel waveform, or LR1121 sleep-pin
behavior. Check these against the board schematic. The SensorLib integration
and error-reporting limits are documented in `04_bat_axp2602/README.md`.
