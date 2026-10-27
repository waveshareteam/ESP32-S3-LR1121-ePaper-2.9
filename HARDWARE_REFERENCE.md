# Hardware reference

Product: ESP32-S3-LR1121-ePaper-2.9  
Hardware revision: 1.1

The board uses an ESP32-S3-WROOM-1-N16R8 module with 16 MB Flash and 8 MB OPI
PSRAM. It integrates a 2.9-inch monochrome e-paper display with a resolution
of 296 × 128 pixels, an LR1121 radio, an AXP2602 battery gauge, a PCF85063A
RTC, a MicroSD card slot, and one WS2812 LED.

The e-paper display and LR1121 share an SPI bus with separate chip-select
signals. The MicroSD interface operates in 1-bit SDMMC mode. GPIO38 controls
the shared peripheral power supply and is active high.

## Main connections

| Interface | GPIO assignments |
| --- | --- |
| Shared peripheral power | Enable: 38 |
| Shared SPI | SCK: 40, MOSI: 45, MISO: 46 |
| E-paper display | CS: 18, DC: 17, RST: 9, BUSY: 8 |
| LR1121 | NSS: 42, RST: 39, BUSY: 41, DIO1: 6 |
| I2C | SDA: 13, SCL: 14 |
| RTC interrupt | IRQ: 7 |
| Battery gauge interrupt | IRQ: 48 |
| Charging status | Status: 47 |
| MicroSD (1-bit SDMMC) | CMD: 10, D0: 11, CLK: 12 |
| WS2812 | Data: 21 |
| Buttons | BOOT: 0, KEY: 1 |

## Schematic

[ESP32-S3-LR1121-ePaper-2.9-XF schematic, Rev1.1](hardware/schematics/ESP32-S3-LR1121-ePaper-2.9-XF-Schematics.pdf)
