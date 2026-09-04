# ESP32-S3-LR1121-ePaper-2.9

[中文](README_ZH.md)

This repository contains first-party ESP-IDF and Arduino examples, product
images, and hardware documentation for the Waveshare
ESP32-S3-LR1121-ePaper-2.9, product revision 1.0.0.

![Front view](assets/product/front.png)

![Back view](assets/product/back.png)

## Repository status

The source examples are organized as matching ESP-IDF and Arduino surfaces,
including a standalone 1-bit SDMMC card test. The CI and firmware packaging
definitions cover every first-party example. Compilation and CI do not replace
flashing and physical-board validation.

The supplied schematic remains under pending review because one e-paper
connector label conflicts with the product title. Mechanical drawings and
factory recovery firmware are reserved for a later update.

## Getting started

- ESP-IDF projects: [`examples/esp-idf/`](examples/esp-idf/)
- Arduino sketches and bundled library: [`examples/arduino/`](examples/arduino/)
- Hardware references: [`hardware/`](hardware/)
- CI documentation: [`docs/ci.md`](docs/ci.md)
- Firmware packages: [`releases/`](releases/)

Each numbered example is independent. Read the framework README and the README
inside the selected example before building or flashing it.

## Examples

| Number | ESP-IDF | Arduino | Focus |
| --- | --- | --- | --- |
| 01 | `01_ws2812_led` | `01_ws2812_led` | Onboard WS2812 |
| 02 | `02_i2c_scan` | `02_i2c_scan` | I2C device scan |
| 03 | `03_epaper_2_9` | `03_epaper` | 2.9-inch e-paper |
| 04 | `04_bat_axp2602` | `04_bat_axp2602` | AXP2602 battery gauge |
| 05 | `05_rtc_pcf85063` | `05_rtc_pcf85063` | PCF85063A RTC |
| 06 | `06_lr1121_pingpong` | `06_lr1121_pingpong` | LR1121 ping-pong |
| 07 | `07_sd_card` | `07_sd_card` | microSD 1-bit SDMMC read/write test |
| 08 | `08_deep_sleep` | `08_deep_sleep` | Deep sleep |
| 09 | `09_comprehensive_demo` | `09_comprehensive_demo` | Integrated board demo |
| 10 | `10_lr1121_lorawan` | `10_lr1121_lorawan` | LoRaWAN Class A OTAA |
| 11 | `11_LoRaWAN_ABP` | `11_LoRaWAN_ABP` | LoRaWAN Class A ABP |
| 12 | `12_LoRaWAN_Class_C` | `12_LoRaWAN_Class_C` | LoRaWAN Class C |
| 13 | `13_LoRaWAN_Multicast` | `13_LoRaWAN_Multicast` | LoRaWAN multicast |

LoRaWAN examples contain placeholder credentials. Configure credentials,
region, frequency plan, antenna, and transmit power for the deployment before
radio activation. Never publish real network keys in source code or logs.

## Continuous integration and Releases

The GitHub Actions workflow discovers direct first-party examples and is
configured for ESP-IDF v5.5.5, ESP-IDF v6.0.2, and Arduino-ESP32 3.3.11.
Successful builds are designed to produce flashable ZIP archives containing a
manifest, SHA-256 checksums, source binary segments, a combined image, and
Windows/POSIX flash helpers. A `v*` tag is designed to publish those archives
as a GitHub Release.

These configurations are statically checked only until they run in a real
remote repository. CI compilation does not prove radio, display, power,
sleep, timing, or other physical hardware behavior.

## Repository layout

| Path | Purpose |
| --- | --- |
| `examples/esp-idf/` | First-party ESP-IDF projects |
| `examples/arduino/` | First-party Arduino sketches and bundled library |
| `assets/` | Product images |
| `hardware/` | Schematics and mechanical-document placeholders |
| `config/` | Machine-readable CI configuration |
| `scripts/` | Example discovery and static automation |
| `releases/` | Source-build firmware packaging |
| `firmware/` | Factory/recovery firmware placeholder |
| `docs/` | Repository and maintainer documentation |

See [`docs/repository-structure.md`](docs/repository-structure.md) for
generated-file and publication boundaries.

## Hardware documents

- Product images: [`assets/product/`](assets/product/)
- Schematic pending review:
  [`hardware/schematics/`](hardware/schematics/)
- Mechanical drawings pending:
  [`hardware/dimensions/`](hardware/dimensions/)

## Support and contribution

See [`CONTRIBUTING.md`](CONTRIBUTING.md), [`SUPPORT.md`](SUPPORT.md), and
[`SECURITY.md`](SECURITY.md).

## License

Original software is licensed under Apache License 2.0. Original
documentation, images, schematics, and hardware materials are licensed under
CC BY 4.0. Nested third-party licenses remain authoritative. See
[`LICENSES/README.md`](LICENSES/README.md) and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
