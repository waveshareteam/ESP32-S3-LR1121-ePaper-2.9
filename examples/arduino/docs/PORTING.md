# ESP-IDF to Arduino coverage

| ESP-IDF source | Arduino counterpart | Preserved behavior | Evidence-based difference |
|---|---|---|---|
| `01_ws2812_led` | `01_ws2812_led` | GPIO38 power, GPIO21 GRB LED, brightness cap 48 | Arduino core RGB helper replaces `led_strip` |
| `02_i2c_scan` | `02_i2c_scan` | GPIO13/14 at 400 kHz, scan `0x03..0x77`, names `0x51`/`0x62` | Synchronous Wire probe |
| `03_epaper_2_9` | `03_epaper` | Landscape UI, initial full refresh, FAST refresh limit, sleep | Also supports 2.13; native text renderer replaces LVGL |
| `04_bat_axp2602` | `04_bat_axp2602` | AXP2602 ID, measurement enable, 350 ms SOC settle, main readings | IRQ enable/clear flow is not demonstrated |
| `05_rtc_pcf85063` | `05_rtc_pcf85063` | Address `0x51`, invalid-time fallback, date/time page | Polls once per second instead of using the RTC 1 Hz IRQ task |
| `06_lr1121_pingpong` | `06_lr1121_pingpong` | 868 MHz, BW125, SF7, CR5, 22 dBm, PING/PONG, RSSI/SNR page | Uses bounded blocking receive instead of the IDF event task |
| `07_sd_card` | `07_sd_card` | 1-bit SDMMC mount, card info, root listing, non-overwriting write/read/cleanup test | Arduino `SD_MMC` replaces the ESP-IDF VFS/FATFS API |
| `08_deep_sleep` | `08_deep_sleep` | Timer/GPIO0 wake, radio sleep, PMU sleep, display sleep, power off | Does not reproduce every IDF RTC GPIO isolation/hold operation; confirm sleep current on hardware |
| `09_comprehensive_demo` | `09_comprehensive_demo` | Shared board init, LED, I2C, battery, RTC, display, radio, buttons, Preferences | The IDF BLE/Wi-Fi/stress task orchestration is not ported; Arduino sketch is a board-health dashboard |
| `10_lr1121_lorawan` | `10_lr1121_lorawan` | EU868 OTAA Class A, credential gate, nonce/session persistence, counter-safe uplinks | UI/task scheduling is simplified to Arduino `loop()` |
| `11_LoRaWAN_ABP` | `11_LoRaWAN_ABP` | EU868 ABP, four LoRaWAN 1.1 session keys, GPIO1/manual or timed uplink, post-transaction FCnt persistence | The IDF pre-transmission FCntUp reservation and pending-transaction ambiguity lockout are not ported |
| `12_LoRaWAN_Class_C` | `12_LoRaWAN_Class_C` | OTAA restore, Class C switch, confirmed marker, continuous downlink polling | Recovery policy is simpler than the IDF state machine |
| `13_LoRaWAN_Multicast` | `13_LoRaWAN_Multicast` | OTAA unicast session, Class C multicast, replay counter persisted before payload exposure | Stores only the last multicast FCnt, not the IDF CRC-wrapped multicast schema |

## Display evidence

The local SSD16xx driver identifies the controller family as SSD1680/SSD1681
and configures BUSY as active high. Both boards use e-paper BUSY/RST/DC/CS on
GPIO8/9/17/18 and shared SPI on GPIO40/45/46 at 10 MHz. Power is GPIO38 active
high, with 100 ms after power-on and 10 ms after power-off. Reset is low/high
for 10 ms each.

The 2.9-inch panel is 128 x 296. The related 2.13-inch material identifies a
122 x 250 visible panel, while its draw path uses a 16-byte row stride. The
Arduino driver resolves that conflict explicitly as 122 visible pixels and
128 controller-RAM pixels. The existing 2.13 ESP-IDF registry calculates the
generic framebuffer from 122; that path may overrun when paired with the
16-byte drawing stride and should not be copied.

The current ESP-IDF demo uses FULL then FAST whole-screen refreshes, not
partial-window updates. The Arduino driver exposes the controller's PARTIAL
waveform but examples preserve the FULL/FAST policy: first FULL, at most ten
FAST updates, then FULL again.

The repository does not contain a board schematic or the two panel datasheets.
The exact controller marking, panel-revision timing limits, and hardware pin
correctness therefore remain TODOs for authoritative-document or hardware
validation. The controller-family, geometry, polarity, and timing statements
above are source-derived contracts, not schematic or panel qualification.

## LR1121 evidence conflict

Current executable sources for examples 06 and 10-13 pass **3.0 V** to
RadioLib for the LR1121 TCXO. Older prose in the 12/13 READMEs says 1.6 V, but
the same examples' current `board_lorawan_radio.cpp` constants and calls use
3.0 V. The Arduino adapter follows executable source and uses 3.0 V. Confirm
the board revision and measured TCXO rail during hardware validation.

## LoRaWAN persistence gap

RadioLib 7.7.1 reports the last transmitted counter through `getFCntUp()`, so
the public value remains zero both before and after the first uplink. The
Arduino storage adapter instead compares the little-endian internal next
FCntUp in RadioLib's session buffer and saves whenever it advances, including
when `sendReceive()` returns a radio error after a possible transmission.

The ESP-IDF ABP implementation additionally reserves the next FCntUp in NVS
before transmission and locks out an ambiguous pending transaction after a
reset. That stronger mechanism depends on its `PersistentAbpNode` subclass and
CRC-wrapped state schema and is not implemented in this small Arduino adapter.
Do not use the Arduino ABP example where sudden power loss during transmission
must be proven unable to reuse a counter.

## Validation boundary

No Arduino or ESP-IDF compile, configure, link, upload, serial session, or
hardware test was run for this delivery. Only static checks were performed.

