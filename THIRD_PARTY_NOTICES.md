# Third-party notices

This file summarizes license notices distributed with the current source tree.
The license file closest to a component or asset remains authoritative.

## Bundled Arduino board library

- Path: examples/arduino/libraries/Waveshare_LR1121_EPaper/
- License: MIT
- Notice: Copyright (c) 2026 Waveshare
- License text: examples/arduino/libraries/Waveshare_LR1121_EPaper/LICENSE

The existing MIT grant is preserved and takes precedence over the repository's
default Apache-2.0 software license for this directory, except for the OFL-licensed
Terminus font listed below.

## ESP e-paper component

- Paths: local components/esp_epaper/ copies under the ESP-IDF examples
- License: MIT
- Notice: Copyright (c) 2025
- License text: the LICENSE file within each component copy

The component copies currently contain identical license texts. Each local
license remains authoritative.

## Waveshare 2.9inch V2 four-gray waveform

- Path: examples/esp-idf/03_epaper_2_9/components/esp_epaper/src/controllers/ssd1680_290_gray4.c
- Source: waveshareteam/e-Paper, RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_2in9_V2.c
- Upstream revision: e4bb8924d47c8ac88072cdddea5544e2f4314d92
- Author: Waveshare team
- License: upstream MIT-style permission notice, retained in the source file

The four-gray waveform and initialization sequence are panel-specific. Their
inclusion does not establish that every SSD16xx 2.9-inch panel is compatible.

## Terminus font

- Paths: local components/epaper_ui_font/ copies under the ESP-IDF examples;
  examples/arduino/libraries/Waveshare_LR1121_EPaper/src/WaveshareFont.h
- License: SIL Open Font License 1.1
- Copyright: Copyright (c) 2019 Dimitar Toshkov Zhekov
- Reserved Font Name: Terminus Font
- License text: the OFL.txt file within each font component copy and the Arduino board library

The SIL Open Font License applies to the font software and overrides the
repository's default licenses for those font files.

## Externally resolved dependencies

ESP-IDF managed components and Arduino library dependencies are fetched or
resolved according to project manifests and framework configuration. Examples
include Espressif Board Manager components, led_strip, and RadioLib. They
retain their upstream licenses and are not relicensed by this repository.

Before a public source release or firmware distribution, generate a dependency
inventory from the selected lock files and resolved versions, then verify that
all required license texts and attribution notices are included.
