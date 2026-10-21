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
| `07_deep_sleep` | Deep-sleep and wake-up test |
| `08_comprehensive_demo` | Combined board test |
| `09_lr1121_lorawan` | LR1121 LoRaWAN Class A OTAA example |

| `10_LoRaWAN_ABP` | LR1121 LoRaWAN Class A ABP example with persistent frame counters |
| `11_LoRaWAN_Class_C` | LR1121 LoRaWAN Class C example |
| `12_LoRaWAN_Multicast` | LR1121 LoRaWAN multicast example |

## Board Manager integration

Examples 01 through 12 depend on the registry package
`waveshare/waveshare_boards`, which brings in `espressif/esp_board_manager`
transitively. Local component manifests intentionally do not declare Board Manager
directly. With `esp-bmgr-assist` installed in the active ESP-IDF Python environment,
a fresh example can download dependencies and list boards with one command:

```sh
idf.py bmgr -l
```

No prior `idf.py reconfigure` or generated board code is needed for this listing.
A direct Board Manager manifest dependency would select the helper's minimal
bootstrap and omit unrelated packages such as the Waveshare board package.
CMake `REQUIRES esp_board_manager` entries remain necessary for compilation.
When Board Manager is already downloaded but the Waveshare package is absent,
the helper reuses Board Manager; use `idf.py reconfigure` to complete that partial
download. Without the helper, use the regular ESP-IDF component download and
extension-loading workflow.

The registry package 0.1.2 requires Board Manager `>=0.7.2`; dependency resolution
currently selects Board Manager 0.7.4. Its five board definitions do not include
`esp32_s3_lr1121_epaper_2_9`. Example 01 and examples 03 through 12 also declare
the local `waveshare_esp_boards` dependency, so `bmgr -l` discovers the shared
board YAML under `components/waveshare_esp_boards/esp32_s3_lr1121_epaper_2_9`.
For example 02, add `-c ../components/waveshare_esp_boards` to list that local board.

Most examples declare ESP-IDF `>=5.4.3`; example 08 retains its own
`>=6.2.0,<6.3.0` constraint.
Use an ESP-IDF version compatible with the selected example before listing or
building it, since the initial bootstrap resolves the full dependency graph.

Validation: examples 03 through 12 all downloaded and listed boards in fresh
project copies with no `managed_components`, lock file, sdkconfig, build directory,
or generated board component. After restoring or generating board code, all ten
examples built successfully: 03–07 and 09–12 with ESP-IDF 5.5.5, and 08 with its
ESP-IDF 6.2 development checkout (`08e0d30a`). All resolved Board Manager 0.7.4
and Waveshare boards 0.1.2. Example 01 also passed first-run board listing and
a complete build with ESP-IDF 5.5.5 after its Board Manager migration. Hardware
operation was not tested.

Board Manager owns the I2C and shared SPI buses, the BOOT GPIO, e-paper power
control, and the WS2812 RMT device. The AXP2602, PCF85063A, e-paper, and LR1121
remain custom devices because Board Manager provides resource and lifecycle
management, not those chip protocols. Examples 02 through 12 each carry their own `components/esp32_s3_lr1121_epaper_2_9_support` copy; keep
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

Building requires `components/gen_bmgr_codes`. Listing boards does not generate
that component. If it is missing, select the board as described below before
running `idf.py build`. For ordinary clean rebuilds, retain the generated sources
and run `idf.py fullclean` followed by `idf.py build`.

## Regenerating board code

Generate when board code is missing, or regenerate after changing a shared board
YAML file. Run this from every affected example directory:

```text
idf.py bmgr -l
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9 -c ../components/waveshare_esp_boards
```

The generator writes environment-specific output. Before committing a
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















