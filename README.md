# ESP32-S3-LR1121-ePaper-2.9

[中文](README_ZH.md)

This repository contains ESP-IDF and Arduino examples and hardware documentation
for the Waveshare ESP32-S3-LR1121-ePaper-2.9.

## Getting started

- ESP-IDF projects: [`examples/esp-idf/`](examples/esp-idf/)
- Arduino sketches and bundled library: [`examples/arduino/`](examples/arduino/)
- Hardware references: [`hardware/`](hardware/)
- CI documentation: [`docs/ci.md`](docs/ci.md)
- Firmware packages: [`releases/`](releases/)

Each numbered directory contains an example project. All ESP-IDF examples,
including 07, use `esp_board_manager` through `waveshare/waveshare_boards`.
Follow the [board preparation instructions](examples/esp-idf/README.md#board-manager-integration)
to generate the board configuration before building.

For Arduino, set the IDE's **Sketchbook location** to this repository's
`examples/arduino` folder and restart the IDE to use the bundled libraries.
Read the selected example's README for its configuration and operation.

## Examples

| Number | ESP-IDF | Arduino | Focus |
| --- | --- | --- | --- |
| 01 | `01_ws2812_led` | `01_ws2812_led` | Onboard WS2812 |
| 02 | `02_i2c_scan` | `02_i2c_scan` | I2C device scan |
| 03 | `03_epaper_2_9` | `03_epaper` | 2.9-inch e-paper |
| 04 | `04_bat_axp2602` | `04_bat_axp2602` | AXP2602 battery gauge |
| 05 | `05_rtc_pcf85063` | `05_rtc_pcf85063` | PCF85063A RTC |
| 06 | `06_lr1121_pingpong` | `06_lr1121_pingpong` | LR1121 ping-pong |
| 07 | `07_sd_card` | `07_sd_card` | microSD 1-bit SDMMC read/write test with e-paper status |
| 08 | `08_deep_sleep` | `08_deep_sleep` | Deep sleep |
| 09 | `09_comprehensive_demo` | `09_comprehensive_demo` | Integrated board demo |
| 10 | `10_lr1121_lorawan` | `10_lr1121_lorawan` | LoRaWAN Class A OTAA |
| 11 | `11_LoRaWAN_ABP` | `11_LoRaWAN_ABP` | LoRaWAN Class A ABP |
| 12 | `12_LoRaWAN_Class_C` | `12_LoRaWAN_Class_C` | LoRaWAN Class C |
| 13 | `13_LoRaWAN_Multicast` | `13_LoRaWAN_Multicast` | LoRaWAN multicast |

Before running a LoRaWAN example, enter your network credentials in its
configuration header and set the region, frequency plan, antenna, and transmit
power for your deployment.

## CI and firmware packaging

The build matrix in [`config/ci.json`](config/ci.json) contains:

| Framework | Version |
| --- | --- |
| ESP-IDF | v5.5.6 |
| ESP-IDF development build | v6.2.0-dev, SDK commit `08e0d30a` |
| Arduino-ESP32 | 3.3.11 |

The workflow in [`.github/workflows/examples.yml`](.github/workflows/examples.yml)
discovers the numbered examples and defines their build jobs. The packaging
tools in [`releases/`](releases/) create ZIP archives containing a manifest,
SHA-256 checksums, binary segments, a combined image, and Windows/POSIX flash
helpers. The workflow configures GitHub Release publication for `v*` tags
when the firmware artifact set passes its checks.

## Repository layout

| Path | Purpose |
| --- | --- |
| `examples/esp-idf/` | First-party ESP-IDF projects |
| `examples/arduino/` | First-party Arduino sketches and bundled library |
| `hardware/` | Schematics and hardware documentation |
| `config/` | Machine-readable CI configuration |
| `scripts/` | Example discovery and static automation |
| `releases/` | Source-build firmware packaging |
| `firmware/` | Supplied HF/LF binary references |
| `docs/` | Repository and maintainer documentation |

See [`docs/repository-structure.md`](docs/repository-structure.md) for directory
and generated-file management.

## Hardware documents

- Schematic: [`hardware/schematics/`](hardware/schematics/)
- Firmware references: [`firmware/`](firmware/)

## Support and contribution

See [`CONTRIBUTING.md`](CONTRIBUTING.md), [`SUPPORT.md`](SUPPORT.md), and
[`SECURITY.md`](SECURITY.md).

## License

Copyright 2026 Waveshare.

Original software is licensed under [Apache License 2.0](LICENSE). Original
documentation, images, schematics, and hardware materials are licensed under
CC BY 4.0. Nested third-party licenses remain authoritative. See
the [CC BY 4.0 license](https://creativecommons.org/licenses/by/4.0/) and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
