# AXP2602 battery gauge example

This example initializes the AXP2602 on the board I2C bus, monitors its IRQ
signal on ESP32-S3 GPIO48, and refreshes the e-paper display and serial log at
a configurable interval.

## SensorLib integration

Examples 04, 08, and 09 depend directly on
[`lewisxhe/sensorlib` 0.4.1](https://components.espressif.com/components/lewisxhe/sensorlib/versions/0.4.1)
from their `main` component. Their AXP2602 source files are C++, include
`<math.h>` before `<GaugeAXP2602.hpp>`, acquire the Board Manager I2C bus, and
own `GaugeAXP2602` directly. There is no board-level AXP2602 wrapper, custom
device callback, legacy `axp2602_*` API, or local register-level driver.

The C++ application calls SensorLib's `GaugeAXP2602` API directly for:

- I2C initialization at the AXP2602 address and chip-ID check;
- the board's configured 10 mOhm current-sense selection;
- battery voltage, signed current, SOC, die temperature, TTE, and TTF refresh;
- IRQ enable, status refresh, and status clear;
- Normal mode, sleep, wake-up, and object cleanup where required.

SensorLib initialization resets the gauge configuration, wakes the device, and
selects Normal mode before the application reapplies the configured sense
resistor and measurement enables. It does not write a custom 128-byte battery
model, so SOC and time estimates continue to use the model already stored in
the AXP2602.

SensorLib 0.4.1 has two error-reporting limits that callers must account for.
Its configuration setters return `void`, and `refresh()` can retain a previous
field value when an individual register read fails. The application reports the
Boolean failures exposed by SensorLib but cannot identify every partial refresh.
Also, SensorLib's `clearIRQStatus()` clears all four AXP2602 IRQ status bits;
clearing is not selective.

## Interrupt test

The example uses GPIO48 for the AXP2602 IRQ signal. Per the AXP2602 datasheet,
IRQ is an open-drain, active-low output. The example enables the ESP32-S3
internal pull-up, configures AXP2602 for low-level reporting, clears stale
status, and then enables all four documented IRQ sources.

The GPIO ISR only notifies the application task. The task refreshes SensorLib's
IRQ status, logs the source bits, and clears all asserted AXP2602 status bits.
Expected serial output has this form:

```text
AXP2602 IRQ count=1 status=0x02: low_soc=0 soc_update=1 over_temp=0 watchdog=0
AXP2602 IRQ status 0x02 cleared, GPIO48=1
```

SOC update is normally the easiest event to observe without changing alarm
thresholds. Connecting or disconnecting a charger is not a separate AXP2602
IRQ source. The GPIO48 logic voltage must be compatible with the fitted
ESP32-S3 variant.

## Configuration

Open `Component config -> AXP2602 Battery Gauge Example` in `idf.py menuconfig`
to select the polling interval; its default remains 30000 ms. The C++ source
selects 10 mOhm to match the current `board_devices.yaml` value. Confirm the
fitted shunt, its CSP/CSN orientation, and that all
battery current crosses it before relying on current or charge-state readings.

The first display update uses the full waveform. The next 10 successful updates
use the controller's full-screen fast waveform, followed by another full update
to clear accumulated ghosting; a failed panel flush also makes the next attempt
full. This `FAST` mode is not the lower-contrast `EPD_UPDATE_PARTIAL` waveform.
The serial log remains on the same configured polling interval. If the e-paper
display cannot be initialized, AXP2602 sampling and serial output continue for
the lifetime of the task.

No battery calibration curve is written. SOC and time estimates therefore use
the model already stored in the AXP2602 and may be inaccurate for an unmatched
cell.

The deep-sleep and comprehensive examples explicitly request SensorLib's
AXP2602 sleep mode before ESP32-S3 deep sleep. The comprehensive example then
destroys the SensorLib object before Board Manager releases the I2C bus. An
ESP32-S3 deep-sleep boot creates a new object during normal initialization.

## Build

Follow the [board preparation instructions](../README.md#board-manager-integration)
to download `waveshare/waveshare_boards >=0.1.3`, which includes the product board definition.
Run from this example directory in Ubuntu Bash with ESP-IDF loaded:

```bash
python -m pip install esp-bmgr-assist==0.8.3
idf.py bmgr -l
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with the device serial port. Board selection generates
`components/gen_bmgr_codes` locally. Keep generated sources, downloaded components
and local board copies out of version control. Preserve deliberate `sdkconfig`
changes before regenerating board code.

GPIO47 is the authoritative charge-status input: high means charging and low
means not charging. Internal pulls are disabled because the board signal is
expected to provide a valid 0/1 level. The serial log shows GPIO47 together
with battery voltage, signed current, SOC, IC temperature, and the
state-relevant TTE or TTF estimate. AXP2602 is a fuel gauge, not a battery
charger.

On cold boot, the example retries AXP2602 detection ten times at 500 ms
intervals before continuing with an error display and log. The success page
uses three 24-pixel rows so
SOC, voltage, current, temperature, charge state, and the GPIO47 level remain
readable.
