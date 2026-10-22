# Onboard WS2812 test

This project tests the onboard WS2812 using ESP Board Manager. The application
acquires the `gpio_epaper_power` peripheral and `status_led` device from the shared
`esp32_s3_lr1121_epaper_2_9` board definition.

## Hardware and driver configuration

- Data GPIO: GPIO21, from the board-level `status_led` device.
- Power enable: GPIO38 (`gpio_epaper_power`), active high. Startup drives it
  to the YAML default level 1 through Board Manager and waits 100 ms before
  initializing the strip.
- LED count: 1, matching the single onboard status LED represented by the board
  definition.
- Pixel format: GRB, configured explicitly for the WS2812 through Espressif's
  `led_strip` driver, instantiated by Board Manager from the board YAML.
- Maximum channel value: 48/255 (about 19 percent) to limit glare and peak
  current.
- Backend: RMT at 10 MHz. The shared SPI bus remains available to the e-paper
  and LR1121 devices.

The GPIO, pixel count, wire order, and RMT settings come from
`../components/waveshare_esp_boards/esp32_s3_lr1121_epaper_2_9`. This migration
preserves the existing pin assignments. Confirm the single-pixel and GRB settings
by observing the RED, GREEN, and BLUE stages on the target board.

The example prepares the Board Manager registry and initializes only the power
GPIO and LED device. The power reference and LED handle are held for the lifetime
of the continuous demo. Color updates still use the native `led_strip` API on the
handle supplied by Board Manager.

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

## List boards, build and flash

Activate ESP-IDF and run from this example directory (Ubuntu Bash):

```sh
idf.py bmgr -l
idf.py build
idf.py -p PORT flash monitor
```

With `esp-bmgr-assist` installed, the first command downloads dependencies on a
fresh project without running `reconfigure`. `main/idf_component.yml` declares
`waveshare/waveshare_boards`, which brings in Board Manager transitively, and the
local `waveshare_esp_boards` package, which supplies this board's definition.

Generated board code is included in `components/gen_bmgr_codes`. If that directory
has been removed, regenerate it before building:

```sh
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9
```

Before committing regenerated files, restore the portable generated
`CMakeLists.txt` and `.gitignore`, and remove the environment-specific `board_path`
from `gen_board_metadata.yaml`, as described in the parent README. For ordinary
clean rebuilds, retain this source directory and use `idf.py fullclean`.

Replace `PORT` with the board's serial port. Confirm that the observed colors
match the RED, GREEN, and BLUE log messages before relying on the breathing
effect's hue sequence.

Verified: first-run board listing and a complete build with ESP-IDF 5.5.5,
Board Manager 0.7.4, Waveshare boards 0.1.2, and led_strip 3.0.3. Hardware color
and power sequencing tests remain to be run on the board.
