# Continuous integration

The `Build Examples and Firmware` GitHub Actions workflow uses
`scripts/discover_examples.py` and `config/ci.json`.

| Surface | Configured version | Target | Entries |
| --- | --- | --- | --- |
| ESP-IDF | `v5.5.6` | `esp32s3` | 13 |
| ESP-IDF experimental (compile only) | `v6.2.0-dev` / `08e0d30a` | `esp32s3` | 13 |
| Arduino CLI / Arduino-ESP32 | `1.5.1` / `3.3.11` | ESP32S3 Dev Module | 13 |

The full matrix contains 26 stable firmware build jobs and 13 experimental
compile checks. Development SDKs are pinned by full commit SHA in
`config/ci.json`, installed from official source, and kept out of release assets.
No `espressif/idf:v6.2.0` image or stable 6.2 release is assumed. The experimental
job reports its own result and is not a dependency of the stable release job.
Local verification is documented in [framework compatibility](idf-compatibility.md);
remote Actions execution must be checked separately.

Board definitions are loaded only from the downloaded `waveshare/waveshare_boards`
component. The downloaded 0.1.2 release does not contain this product yet; the
Board Manager jobs fail with an explicit missing-board message until a component
release includes it. Local manual board-folder copies are not supplied to CI.
Example 07 also requires that board package and board-code generation for its
shared power GPIO; its card operations use native SDMMC/FATFS.

Manual dispatch accepts `all`, an example name, or a repository-relative
example path. Bundled library examples are excluded.

Each successful stable build is packaged and validated before upload.
A `v*` tag is designed to publish the complete artifact set as a GitHub
Release. Supplied HF/LF reference firmware is excluded from this workflow.

CI proves compilation and packaging only after a real run. It does not prove
display, radio, power, RTC, battery, sleep, antenna, or regional compliance
behavior on physical hardware.
