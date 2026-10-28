# Continuous integration

The `Build Examples and Firmware` GitHub Actions workflow uses
`scripts/discover_examples.py` and `config/ci.json`.

| Surface | Configured version | Target | Entries |
| --- | --- | --- | --- |
| ESP-IDF | `v5.5.6` | `esp32s3` | 9 |
| ESP-IDF experimental (compile only) | `v6.2.0-dev` / `08e0d30a` | `esp32s3` | 9 |
| Arduino CLI / Arduino-ESP32 | `1.5.1` / `3.3.11` | ESP32S3 Dev Module | 9 |

The full matrix contains 18 stable firmware build jobs and 9 experimental
compile checks. Development SDKs are pinned by full commit SHA in
`config/ci.json`, installed from official source, and kept out of release assets.
No `espressif/idf:v6.2.0` image or stable 6.2 release is assumed. The experimental
job reports its own result and is not a dependency of the stable release job.
Local verification is documented in [framework compatibility](idf-compatibility.md);
remote Actions execution must be checked separately.

Board definitions are loaded only from the downloaded `waveshare/waveshare_boards`
component, version 0.1.3 or later, which includes `esp32_s3_lr1121_epaper_2_9`.
Board Manager jobs check that the board definition is present before generating
code. All ESP-IDF examples use this component download and generation flow.

The source inventory retains 13 examples per framework. `source_only_examples`
in `config/ci.json` excludes LoRaWAN examples 10–13 from both stable and
experimental builds and from release coverage. Their IDs and keys must be
configured locally before building; no prebuilt LoRaWAN firmware is published
by this workflow.

Manual dispatch accepts `all`, a CI-enabled example name, or a repository-relative
example path. `all` builds examples 01–09. Selecting a source-only LoRaWAN example
reports that it must be configured and built locally. Bundled library examples
are excluded.

Each successful stable build is packaged and validated before upload.
A `v*` tag is designed to publish the complete artifact set as a GitHub
Release. Supplied HF/LF reference firmware is excluded from this workflow.

CI proves compilation and packaging only after a real run. It does not prove
display, radio, power, RTC, battery, sleep, antenna, or regional compliance
behavior on physical hardware.
