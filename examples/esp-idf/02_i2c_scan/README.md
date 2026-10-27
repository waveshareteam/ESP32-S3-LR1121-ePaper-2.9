# I2C scan

Scan the shared I2C bus for the PCF85063A RTC and AXP2602 fuel gauge.

## Build

Follow the [board preparation instructions](../README.md#board-manager-integration).
The published `waveshare/waveshare_boards >=0.1.3` component includes
`esp32_s3_lr1121_epaper_2_9`. `idf.py bmgr -l` downloads it automatically.

In an activated ESP-IDF terminal, run from this example directory (Ubuntu Bash):

```sh
idf.py bmgr -l
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9
idf.py build
```

The target is ESP32-S3. Board selection generates `components/gen_bmgr_codes`;
listing boards alone does not. Generated sources and downloaded components are
not committed. See [framework compatibility](../../../docs/idf-compatibility.md)
for the 5.5 and 6.2 results and validation boundaries.

## Dependency bootstrap

Install `esp-bmgr-assist==0.8.3` in the active ESP-IDF Python environment.
On a fresh project, `idf.py bmgr -l` downloads dependencies without running CMake
first. Board Manager is intentionally a transitive dependency of
`waveshare/waveshare_boards` in `main/idf_component.yml`. Do not add a direct
`espressif/esp_board_manager` manifest dependency: that selects the helper's
minimal bootstrap, which downloads only Board Manager and its own dependencies.
The existing CMake `REQUIRES esp_board_manager` entries still declare the build
dependency.

If Board Manager is already present but the Waveshare package is missing, the
helper reuses the installed Board Manager without resolving the full graph.
In that partial-download state, use `idf.py reconfigure` to fetch the remaining
dependencies. Without `esp-bmgr-assist`, use the regular ESP-IDF dependency
download workflow and load the Board Manager extension before listing boards.

For an ordinary clean rebuild, retain `components/gen_bmgr_codes` and run
`idf.py fullclean` followed by `idf.py build`. After removing generated sources,
select the board again. After removing or updating downloaded components,
list and select the board again using the commands above.
