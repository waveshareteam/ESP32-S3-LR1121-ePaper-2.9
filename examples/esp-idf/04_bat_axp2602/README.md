# AXP2602 battery gauge example

This example initializes the AXP2602 on the board I2C bus, monitors its IRQ
signal on ESP32-S3 GPIO48, and refreshes the e-paper display and serial log at
a configurable interval.

## Registry result and driver choice

An exact ESP Component Registry search found no Espressif-owned or
Espressif-maintained AXP2602 component. The only exact result was the
community-maintained
[`lewisxhe/sensorlib` 0.4.1](https://components.espressif.com/components/lewisxhe/sensorlib/versions/0.4.1).

Registry metadata names Lewis He as the maintainer, reports support for all
targets and ESP-IDF `>=4.4`, and documents installation with
`idf.py add-dependency "lewisxhe/sensorlib^0.4.1"`. Its `GaugeAXP2602` API
provides `begin(i2c_master_bus_handle_t)`, `refresh()`, and getters for voltage,
current, temperature, SOC, TTE, TTF, and inferred charge/discharge state.

Version 0.4.1 has a much broader build footprint than this board needs. Its
CMake file unconditionally depends on the split `esp_driver_gpio`,
`esp_driver_i2c`, and `esp_driver_spi` components, while its CI only tests the
latest ESP-IDF image. Its stated ESP-IDF 4.4 compatibility is therefore not
substantiated by its build metadata and should be treated as ESP-IDF 5.3+ in
practice. Its `refresh()` implementation also does not propagate every
register-read failure.

The shared Board Manager adapter therefore contains a small AXP2602 driver
based directly on the
[X-Powers AXP2602 datasheet](https://github.com/Xinyuan-LilyGO/T-Display-C5/blob/master/doc/AXP2602_Datasheet_V1.0_cn%20(1).pdf).
It does not substitute an AXP192, AXP202, or AXP2101 driver. The adapter implements:

- fixed 7-bit I2C address `0x62` and chip ID `0x1C`;
- wake-up, Normal operating mode, VBAT/IBAT/Tdie measurement, shunt selection,
  and an internal-MCU reset only when software actually changes that selection;
- battery voltage, signed current, SOC, IC temperature, TTE, and TTF;
- IRQ enable, status read, and status clear for low-SOC, SOC-update,
  over-temperature, and watchdog-overflow events;
- an explicit sleep API for applications willing to suspend gauge estimation.

Every I2C failure is returned to the application, and each multi-byte value is
read in one I2C transaction. The driver waits 350 ms after wake-up before using
SOC data. It does not report battery temperature, capacity in mAh, SOH, or a
charger status because the implemented AXP2602 registers do not provide those
semantics.

## Interrupt test

The example uses GPIO48 for the AXP2602 IRQ signal. Per the AXP2602 datasheet,
IRQ is an open-drain, active-low output. The example enables the ESP32-S3
internal pull-up, configures AXP2602 for low-level reporting, clears stale
status, and then enables all four documented IRQ sources.

The GPIO ISR only notifies the application task. The task reads register `0x20`,
logs the source bits, and clears the asserted status with the datasheet's
zero-to-one write sequence. Expected serial output has this form:

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
to select the polling interval; its default remains 30000 ms. The shunt value is
board data rather than an example setting: `board_devices.yaml` currently
declares 10 mOhm. Confirm the fitted shunt, its CSP/CSN orientation, and that all
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

The deep-sleep example intentionally leaves AXP2602 in Normal mode so its SOC
and time estimation continue while the ESP32-S3 sleeps. Applications that value
the lowest gauge current over estimate continuity can explicitly request sleep.

## Build

Each example keeps its generated `components/gen_bmgr_codes` sources in the
project so a normal checkout is complete. A normal build does not require a
Board Manager regeneration. After changing the shared board YAML files,
regenerate the code from each affected example directory:

```text
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9
```

`esp_board_manager` 0.6.1 can rewrite the generated `.gitignore`, insert an
environment-specific absolute board path, and replace `sdkconfig`. Before
committing regenerated output, apply the portability and consistency checks in
the repository root README.

```text
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

GPIO47 is the authoritative charge-status input: high means charging and low
means not charging. Internal pulls are disabled because the board signal is
expected to provide a valid 0/1 level. The serial log shows GPIO47 together
with battery voltage, signed current, SOC, IC temperature, and the
state-relevant TTE or TTF estimate. AXP2602 is a fuel gauge, not a battery
charger.

On cold boot, the example retries AXP2602 initialization ten times at 500 ms
intervals. If the gauge is detected later during normal polling, GPIO48 IRQ
setup is completed at that point. The success page uses three 24-pixel rows so
SOC, voltage, current, temperature, charge state, and the GPIO47 level remain
readable.

Hardware behavior still requires an AXP2602-equipped board. A successful build
only validates software integration.
