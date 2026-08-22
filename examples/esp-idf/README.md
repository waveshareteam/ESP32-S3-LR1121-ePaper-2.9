# ESP32-S3 LR1121 e-Paper 2.9 ESP-IDF examples

These numbered examples share one board definition. Examples 02 through 09 and
10 through 12 use Espressif's `esp_board_manager` instead of the former local
BSP resource layer; example 01 drives the onboard WS2812 directly with
`led_strip`.

| Directory | Function |
| --- | --- |
| `01_ws2812_led` | Onboard WS2812 red/green/blue and color-breathing test |
| `02_i2c_scan` | I2C scan for the PCF85063A and AXP2602 |
| `03_epaper_2_9` | 2.9-inch e-paper full/fast refresh test |
| `04_bat_axp2602` | AXP2602 battery-gauge data |
| `05_rtc_pcf85063` | PCF85063A RTC and interrupt test |
| `06_lr1121_pingpong` | LR1121 ping-pong test |
| `07_deep_sleep` | Deep-sleep and wake-up test |
| `08_comprehensive_demo` | Combined board test |
| `09_lr1121_lorawan` | LR1121 LoRaWAN Class A OTAA example |

| `10_LoRaWAN_ABP` | LR1121 LoRaWAN Class A ABP example with persistent frame counters |
| `11_LoRaWAN_Class_C` | LR1121 LoRaWAN Class C example |
| `12_LoRaWAN_Multicast` | LR1121 LoRaWAN multicast example |

## Board Manager integration

Examples 02 through 09 and 10 through 12 pin `espressif/esp_board_manager` 0.6.1
and model the onboard status LED as its native `led_strip`/RMT device. Example
1 drives the same WS2812 directly with Espressif's `led_strip` 3.0.3. Every
Board Manager-backed example declares ESP-IDF `>=5.4.3`.
Upstream Board Manager 0.6.1 examples document IDF 5.4 `>=5.4.3` and IDF 5.5
`>=5.5.2`. The component also carries SoC capability data through IDF 6.2, but
the exact selected IDF branch still needs build validation. The shared board
files are under
`components/waveshare_esp_boards/esp32_s3_lr1121_epaper_2_9`, following the same direct-child layout as `espressif/esp_boards`.
The board package deliberately ends in `_boards`: Board Manager 0.6.1 uses
that suffix to discover local board dependencies declared outside an
example's own `components` directory. The selectable board name remains
`esp32_s3_lr1121_epaper_2_9`.

The registry currently also provides 0.7.0, which the referenced ESP32-S31
project resolves. Its release change is the unrelated audio-codec migration to
`esp_codec_dev` 2.0.0-beta1. This board has no audio device, and the migration
uses only APIs present in both versions, so the 0.6.1 pin is intentional.

Board Manager owns the I2C and shared SPI buses, the BOOT GPIO, e-paper power
control, and the WS2812 RMT device. The AXP2602, PCF85063A, e-paper, and LR1121
remain custom devices because Board Manager provides resource and lifecycle
management, not those chip protocols. Each Board Manager-backed example
carries its own `components/esp32_s3_lr1121_epaper_2_9_support` copy; keep
common changes in sync while preserving example-specific extensions. The shared
`components/waveshare_esp_boards` component contains only board definitions and
can be maintained or published independently.

## Shared e-paper font

Every example that renders text on the 2.9-inch e-paper (`03_epaper_2_9`
through `09_lr1121_lorawan`, plus examples 10 through 12) uses the shared
`components/epaper_ui_font` component. It provides the same Terminus Bold 8x14
ASCII 1-bpp font and crisp, non-antialiased LVGL label rendering established by
the comprehensive demo.
The font covers U+0020 through U+007E; `06_lr1121_pingpong` escapes non-ASCII
payload bytes as `\xNN` before display. The component README records the
upstream source and its SIL Open Font License 1.1 terms.

Each numbered directory remains an independent firmware project, so each
resulting firmware necessarily links one copy of the font object even though
the repository maintains only one source asset.

## E-paper refresh policy

Examples `03_epaper_2_9` through `06_lr1121_pingpong`,
`08_comprehensive_demo`, `09_lr1121_lorawan`, and examples 10 through 12 use a
full waveform for the first frame, up to 10 successful full-screen fast
updates, and then another full waveform to clear
accumulated ghosting. A failed panel flush forces the next attempt back to full.
`FAST` is the controller's accelerated full-screen waveform; it is not
`EPD_UPDATE_PARTIAL` and does not change the LVGL screen layout.

Examples 03 through 09 and 10 through 12 resolve esp_epaper from their own
components/esp_epaper copies. The repository-level component remains
unchanged as the source baseline for these private copies.

`07_deep_sleep` remains full-refresh-only. It displays one frame, powers down the
e-paper controller, and enters ESP deep sleep, so every wake starts with a new
controller and framebuffer state.

The `01_ws2812_led` directory tests the single onboard WS2812 on GPIO21. See
`01_ws2812_led/README.md` for the test sequence, brightness limit, hardware-evidence boundary, and flash command.

Examples 02 through 09 and 10 through 12 intentionally track their own
`components/gen_bmgr_codes` output, so a normal checkout can be configured
and built without regenerating the board files first:

```text
idf.py -C 04_bat_axp2602 set-target esp32s3
idf.py -C 04_bat_axp2602 reconfigure
idf.py -C 04_bat_axp2602 build
```

Replace `04_bat_axp2602` with the example to build.

## Regenerating board code

Only regenerate after changing one of the shared board YAML files. Run this
from every affected example directory:

```text
idf.py bmgr -l
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9
```

The 0.6.1 generator writes environment-specific output. Before committing a
regeneration, verify all of the following:

- `components/gen_bmgr_codes/.gitignore` keeps the generated sources tracked
  and ignores only `sdkconfig.bmgr_board.old`;
- `components/gen_bmgr_codes/CMakeLists.txt` contains no local absolute path;
- it compiles only the generated C sources in its own directory; the shared
  board directory contains YAML definitions, not C sources or headers, and
  must not be added to `SRC_DIRS` or `INCLUDE_DIRS`;
- generated board metadata and C sources are reviewed against the shared YAML
  and the example's verified SPI-host requirement before being accepted.

The generator can also replace `sdkconfig`, so preserve intentional project
configuration changes before running it.

## Hardware checks

The WS2812 definition uses GPIO21, one pixel, GRB order, and 10 MHz RMT. It
shares the active-high GPIO38 supply with the e-paper, so confirm the RED,
GREEN, and BLUE stages and the shared-power behavior on hardware. Software
configuration also cannot establish the fitted AXP2602 sense resistor, CSP/CSN
direction, e-paper power polarity, panel waveform, or LR1121 sleep-pin
behavior. Check these against the board schematic. The AXP2602 component search
and register-level limitations are documented in `04_bat_axp2602/README.md`.
















