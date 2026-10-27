# ESP32-S3-LR1121-ePaper-2.9 Arduino Examples

Arduino examples for the Waveshare ESP32-S3-LR1121-ePaper-2.9 board, covering
its LED, I2C devices, 2.9-inch e-paper display, battery gauge, RTC, microSD,
LoRa/LoRaWAN communication, and deep sleep.

## Set up Arduino IDE

1. Install Arduino IDE 2.x.
2. Open **File > Preferences** and set **Sketchbook location** to the
   `examples/arduino` folder in this repository. Select the full path to that
   folder on your computer, then restart Arduino IDE. The IDE will use the
   supplied libraries in its `libraries` subfolder; no separate library download
   or copying is needed.
3. In **Preferences**, add the following **Additional Boards Manager URL**:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
4. Open **Boards Manager** and install **esp32 by Espressif Systems 3.3.11**.
   This board package provides the ESP32 toolchain and core libraries.

## Board settings

Select these options in **Tools**:

| Setting | Value |
| --- | --- |
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| USB Mode | Hardware CDC and JTAG |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Port | The connected board's serial port |

## Run an example

Open the `.ino` file inside the example folder, such as
`03_epaper/03_epaper.ino`. Click **Verify**, then **Upload**. Open the Serial
Monitor at **115200 baud** to view the results.

| Folder | Function |
| --- | --- |
| `01_ws2812_led` | Control the onboard RGB LED |
| `02_i2c_scan` | Scan the I2C bus and report detected devices |
| `03_epaper` | Display text and exercise e-paper refreshes |
| `04_bat_axp2602` | Read battery voltage, current, charge level, and temperature |
| `05_rtc_pcf85063` | Read and display the RTC date and time |
| `06_lr1121_pingpong` | Exchange LoRa PING/PONG packets between two boards |
| [07_sd_card](07_sd_card/README.md) | Test microSD file writing and reading, and show the result on e-paper |
| [08_deep_sleep](08_deep_sleep/README.md) | Shut down peripherals and enter deep sleep with timer or BOOT-button wake |
| [09_comprehensive_demo](09_comprehensive_demo/README.md) | Run board self-tests, Wi-Fi/BLE, LoRa communication, and button-controlled sleep |
| `10_lr1121_lorawan` | Join a LoRaWAN network using OTAA |
| `11_LoRaWAN_ABP` | Communicate over LoRaWAN using ABP |
| `12_LoRaWAN_Class_C` | Receive LoRaWAN Class C downlinks |
| `13_LoRaWAN_Multicast` | Receive LoRaWAN multicast messages |

For SD examples, insert a FAT16/FAT32 microSD card before starting the board.
For LoRaWAN examples, fill in the credentials in the example's configuration
header and select the frequency region used by your network before uploading.
