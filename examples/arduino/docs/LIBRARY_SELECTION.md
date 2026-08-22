# Library selection evidence

## Selected: RadioLib 7.7.1

- Upstream: `jgromes/RadioLib`.
- License: MIT.
- Arduino metadata: `architectures=*`; its `library.properties` does not list
  Adafruit dependencies.
- Source/API coverage: LR1121, LoRaWAN OTAA and ABP, Class A and Class C,
  `startMulticastSession`, persistent nonce/session buffers, and downlink event
  metadata.
- Project evidence: the ESP-IDF examples 09-12 already pin RadioLib 7.7.1.

The bundled Waveshare library declares `RadioLib (=7.7.1)` as its only
external dependency. The exact pin is required because this adapter uses the
7.7.1 session-buffer layout and the parameter-form LR1121 `begin()` API, which
upstream has deprecated for removal in 8.0. Its source include tree contains no
Adafruit header.

Upstream sources were checked on 2026-08-20:

- `https://github.com/jgromes/RadioLib/releases/tag/7.7.1`
- `https://github.com/jgromes/RadioLib/tree/7.7.1/src`
- `https://github.com/ZinggJM/GxEPD2/tree/1.6.9`

## Selected: native SSD16xx adapter

No external graphics or e-paper library is required. The adapter was organized
from the project's current MIT-licensed generic SSD16xx driver and board
support. This keeps the verified GPIOs, active-high BUSY, reset/power delays,
FULL/FAST/PARTIAL command sequences, and the 2.13 RAM-width correction in one
small Arduino layer.

## Excluded: GxEPD2 1.6.9

The related 2.13-inch Arduino material uses GxEPD2, but it does not meet this
project's dependency rule:

- GxEPD2 1.6.9 `library.properties` declares
  `depends=Adafruit GFX Library`.
- `GxEPD2_GFX.h` directly includes `Adafruit_GFX.h`.
- Therefore the dependency is disallowed even if the sketch never includes an
  Adafruit header directly.

The related example also carries outdated BQ27220 naming for the actual
AXP2602 path, so it is not a reliable board-support baseline.

## Other generic display libraries

Generic display libraries were not selected merely because they contain an
SSD1680 class. The local evidence requires two board variants, a 122-visible /
128-RAM-width distinction, shared power/SPI behavior, and this project's
refresh policy. The native adapter is smaller and has a traceable hardware
contract without introducing another graphics dependency tree.

