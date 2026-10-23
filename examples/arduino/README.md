# Arduino examples

This directory is the Arduino-ESP32 counterpart of the sibling `esp-idf`
examples. It supports the Waveshare ESP32-S3 LR1121 e-paper 2.13-inch and
2.9-inch boards without any Adafruit library.

## 1. Install the tools

1. Install Arduino IDE 2.x.
2. Open **File > Preferences**, add Espressif's official board-manager URL
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`, then
   install **esp32 by Espressif Systems 3.3.11** in Boards Manager.
3. Install **RadioLib 7.7.1** from Library Manager. Do not install GxEPD2 or
   Adafruit_GFX for these examples.
4. Copy `examples/arduino/libraries/Waveshare_LR1121_EPaper` into the Arduino
   sketchbook `libraries` directory, then restart Arduino IDE.

The bundled library declares only RadioLib as an external dependency. Arduino
core libraries such as SPI, Wire, Preferences, WiFi, and SD_MMC come with the
board package.

## 2. Select the display

Edit
`examples/arduino/libraries/Waveshare_LR1121_EPaper/src/WaveshareConfig.h` and keep
exactly one model enabled:

```cpp
// #define WAVESHARE_EPAPER_213
#define WAVESHARE_EPAPER_290
```

Use `WAVESHARE_EPAPER_213` for the 122 x 250 visible panel. Its controller RAM
is 128 x 250, so the driver deliberately allocates 4000 bytes instead of using
the visible width as the byte stride. Use `WAVESHARE_EPAPER_290` for the
128 x 296 panel. Both examples use a landscape logical canvas: 250 x 122 or
296 x 128.

## 3. Select the board and port

In **Tools**, select:

- Board: **ESP32S3 Dev Module**
- USB CDC On Boot: **Enabled**
- USB Mode: **Hardware CDC and JTAG**
- Flash Size: **16MB (128Mb)**
- PSRAM: **OPI PSRAM**
- Port: the COM port that appears when the board is connected

The hardware is N16R8. Keep the default partition scheme unless a sketch
explicitly needs another one. If no port appears, use a data-capable USB cable,
hold BOOT (GPIO0), tap RESET, and select the new port.

## 4. Open and run an example

Open the `.ino` whose name matches its directory, for example
`examples/arduino/03_epaper/03_epaper.ino`. Select the board and port, then use
Arduino IDE's normal Verify and Upload buttons. Open Serial Monitor at
**115200 baud** after upload.

The examples are compile-checked with the board package and settings above.
Compilation and host UI tests do not replace hardware validation: verify panel
refreshes, radio reception, button behavior and sleep current on your board.

All screen examples use the same crisp Terminus Bold 8x14 ASCII font as the
ESP-IDF examples (previously 5x7). Status rows share the available height;
long text ends in `...` instead of overflowing into the next cell. Example 09
uses the ESP-IDF two-column runtime layout and full-width radio counters.
Examples 01 and 02 remain LED-only and serial-only respectively.

See [08 deep sleep](08_deep_sleep/README.md) for the 600-second timer, GPIO0
EXT0 wake, radio pin isolation and shared-power GPIO hold matching ESP-IDF 08.

See [09 comprehensive test](09_comprehensive_demo/README.md) for Wi-Fi settings,
GPIO1 transmit, GPIO0 double-click sleep and single-click wake.

## 5. LoRaWAN credentials

Examples 10 through 13 contain conspicuous all-zero placeholders in a local
credentials header. Replace every required EUI, address, and key before use.
If any placeholder remains, the sketch prints `PROVISIONING ERROR`, displays
`CONFIG ERROR`, and stops before radio activation or network traffic. Keys are
never printed.

The LoRaWAN examples default to EU868. Confirm that EU868 and the configured
TX power are legal at the deployment location. RadioLib and the network manage
the channel mask; do not hard-code extra EU868 channels unless the network
provides them through CFList or MAC commands.

## 6. On-board connections

| Function | Connection |
|---|---|
| Shared peripheral power | GPIO38, active high |
| WS2812 | GPIO21, one LED, GRB |
| I2C | SDA GPIO13, SCL GPIO14, 400 kHz |
| PCF85063A | `0x51`, IRQ GPIO7 |
| AXP2602 | `0x62`, charge GPIO47, IRQ GPIO48 |
| Shared SPI | SCK GPIO40, MOSI GPIO45, MISO GPIO46 |
| microSD (SDMMC 1-bit) | CMD GPIO10, D0 GPIO11, CLK GPIO12 |
| E-paper | BUSY GPIO8, RST GPIO9, DC GPIO17, CS GPIO18 |
| LR1121 | DIO1 GPIO6, NSS GPIO42, RST GPIO39, BUSY GPIO41 |
| Buttons | BOOT GPIO0, radio-send GPIO1 |

### LR1121 HP antenna path

No sketch setting is required. `LR1121BoardRadio` uses a board-specific
`HighPowerLR1121` adapter that overrides both `setOutputPower()` and
`checkOutputPower()` with RadioLib's `forceHighPower=true`. This keeps the
sub-GHz HP PA selected when LoRaWAN reapplies the transmit power after startup.
The DIO5/DIO6 RF-switch table routes transmit mode as LOW/HIGH, matching the
ESP-IDF adapter.

HP selects the LR1121 PA path; it does not grant permission to transmit at
22 dBm. The requested power is still checked by RadioLib and limited by the
selected LoRaWAN region. Confirm local regulations, antenna gain, and the board
power supply before transmission.

The e-paper and LR1121 share SPI. Example 09 uses separate tasks with the
Arduino core SPI transaction lock; other radio examples complete their radio
operation before refreshing the display. Do not add display or Preferences work
inside LoRaWAN RX windows.

## 7. Expected serial output

| Example | Expected first useful output |
|---|---|
| 01 | `WS2812: GPIO21, one GRB LED` |
| 02 | `found 0x51 PCF85063A`, `found 0x62 AXP2602` |
| 03 | panel name, visible size, RAM size, `BUSY=HIGH` |
| 04 | battery voltage, current, SOC, temperature |
| 05 | date/time and `voltage_low` state |
| 06 | `LR1121 init: 0`, followed by PING/PONG traffic |
| 07 | card information, readback verification, unmount, `FINAL: PASS` |
| 08 | wake cause, radio state, then `start deep sleep` |
| 09 | self-test results, APSTA/BLE status, CPU stress and LoRa counters |
| 10 | OTAA state and increasing `FCntUp` |
| 11 | ABP state and increasing `FCntUp` |
| 12 | Class C ready and asynchronous downlink reports |
| 13 | multicast group and committed multicast counter |

RadioLib result `0` means success. A negative result after `sendReceive()` does
not prove that no uplink left the radio; the examples persist the session when
RadioLib's internal uplink counter advances.

## 8. Troubleshooting

- **`CONFIG ERROR`**: replace all placeholders in the example's credential
  header. Do not paste keys into serial logs or support tickets.
- **E-paper BUSY timeout**: confirm the selected 2.13/2.9 model, GPIO38 power,
  BUSY GPIO8, and that no other code holds shared SPI. BUSY is high while busy.
- **Image shifted on 2.13-inch panel**: confirm the driver still uses RAM width
  128 and visible width 122. Do not reduce the framebuffer to 122 x 250 bits.
- **I2C scan finds nothing**: verify GPIO38 is high, then check GPIO13/14 and
  pull-ups. The expected addresses are `0x51` and `0x62`.
- **SD mount fails**: insert a FAT16/FAT32 card before reset, then check
  GPIO38 power and the 1-bit SDMMC pins CMD=10, D0=11, CLK=12. The example
  never formats the card.
- **LR1121 chip not found**: check NSS/DIO1/RST/BUSY, SPI pins, 8 MHz SPI, RF
  switch setup, and TCXO 3.0 V.
- **OTAA never joins**: verify region, JoinEUI/DevEUI byte order, LoRaWAN MAC
  version, AppKey/NwkKey roles, gateway coverage, and network join logs.
- **Repeated join or frame-counter rejection**: do not erase Preferences on
  each boot. OTAA nonces are saved after every activation attempt and session
  state is saved whenever FCntUp advances.
- **`NVS ERROR` or `STATE ERROR`**: the example puts LR1121 to sleep and
  stops further radio work. Resolve the storage fault before reset. Do not
  erase an ABP session unless the network-side frame counters are also reset.

See [PORTING.md](docs/PORTING.md) for coverage and known gaps, and
[LIBRARY_SELECTION.md](docs/LIBRARY_SELECTION.md) for dependency evidence.

