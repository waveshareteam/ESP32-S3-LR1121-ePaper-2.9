# I2C scan

Scan the shared I2C bus for the PCF85063A RTC and AXP2602 fuel gauge.

## Build

In an activated ESP-IDF terminal, run from this example directory:

```sh
idf.py build
```

The target is ESP32-S3. The generated board sources under
`components/gen_bmgr_codes` are included so a fresh checkout can build directly.
Verified with ESP-IDF 5.5.5, `esp_board_manager` 0.7.4 and
`waveshare/waveshare_boards` 0.1.2. Compilation does not validate hardware operation.

## Board definitions

With `esp-bmgr-assist` installed in the active ESP-IDF Python environment, run:

```sh
idf.py bmgr -l
```

On a fresh project, this downloads the dependencies and lists the five boards
from `waveshare/waveshare_boards` 0.1.2 without running CMake first. Board Manager
is intentionally a transitive dependency of `waveshare/waveshare_boards` in
`main/idf_component.yml`. Do not add a direct `espressif/esp_board_manager`
dependency to the local component manifests: that selects the helper's minimal
bootstrap, which downloads only Board Manager and its own dependencies.
The existing CMake `REQUIRES esp_board_manager` entries still declare the build
dependency. The Board Manager version constraint now comes from the Waveshare
package (`>=0.7.2` in 0.1.2); the resolved version used for validation was 0.7.4.

If Board Manager is already present but the Waveshare package is missing, the
helper reuses the installed Board Manager without resolving the full graph.
In that partial-download state, use `idf.py reconfigure` to fetch the remaining
dependencies. Without `esp-bmgr-assist`, use the regular ESP-IDF dependency
download workflow and load the Board Manager extension before listing boards.

For a clean rebuild, retain `components/gen_bmgr_codes` and run `idf.py fullclean`
followed by `idf.py build`. The generated board component is required project
source, not a downloaded package or a build cache.

`waveshare/waveshare_boards` 0.1.2 does not contain
`esp32_s3_lr1121_epaper_2_9`. This example therefore uses the existing YAML files
under `../components/waveshare_esp_boards/esp32_s3_lr1121_epaper_2_9` to generate
its board code. Downloading the registry component alone does not generate
`gen_bmgr_codes`.

After changing the board YAML, preserve any custom sdkconfig settings and regenerate
from this example directory (Ubuntu Bash):

```sh
export IDF_EXTRA_ACTIONS_PATH="$PWD/managed_components/espressif__esp_board_manager"
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9 -c ../components/waveshare_esp_boards
```

Before building or committing regenerated files, restore the portable
`components/gen_bmgr_codes/CMakeLists.txt` and `.gitignore` from version control,
and remove the environment-specific `board_path` entry from
`gen_board_metadata.yaml`. The shared board directory contains YAML only; the
generated component compiles its own five C source files.

If both the downloaded components and `gen_bmgr_codes` have been removed, run
`idf.py bmgr -l` to download dependencies, then generate the board code as above,
normalize the generated files, and run `idf.py build` again. Listing boards does
not recreate `gen_bmgr_codes`.
