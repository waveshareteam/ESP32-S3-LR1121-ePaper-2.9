# Onboard WS2812 test

This project replaces the former single-color PWM demo with a test for the
onboard WS2812. Its normalized project directory is `01_ws2812_led`.

## Hardware and driver configuration

- Data GPIO: GPIO21, from the current board-level `status_led` peripheral.
- Power enable: GPIO38 (`gpio_epaper_power`), active high. Startup drives it
  to level 1 and waits 100 ms before initializing the strip.
- LED count: 1, matching the single onboard status LED represented by the board
  definition.
- Pixel format: GRB, configured explicitly for the WS2812 through Espressif's
  `led_strip` driver.
- Maximum channel value: 48/255 (about 19 percent) to limit glare and peak
  current.
- Backend: RMT at 10 MHz. The shared SPI bus remains available to the e-paper
  and LR1121 devices.

The project files do not include the carrier-board schematic or a pixel-count
field. GPIO21 and the active-high GPIO38 power control are backed by the current
board definition, while the single-pixel and GRB settings must still be
confirmed by observing the RED, GREEN, and BLUE stages on the target board.

## Test sequence

At startup, the example enables `gpio_epaper_power` before the visible stages:

1. Pure red for 1.2 seconds.
2. Pure green for 1.2 seconds.
3. Pure blue for 1.2 seconds.
4. Continuous color breathing. Hue advances one degree every 20 ms while a
   cosine envelope fades smoothly from dark to bright and back to dark.

The breathing loop delays every frame, so it yields to FreeRTOS and does not
busy-loop the main task. The serial log reports every startup stage and each
completed breathing cycle.

## Build and flash

```text
idf.py -C 01_ws2812_led set-target esp32s3
idf.py -C 01_ws2812_led reconfigure
idf.py -C 01_ws2812_led build
idf.py -C 01_ws2812_led -p PORT flash monitor
```

Replace `PORT` with the board's serial port. Confirm that the observed colors
match the RED, GREEN, and BLUE log messages before relying on the breathing
effect's hue sequence.


