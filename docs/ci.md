# Continuous integration

The `Build Examples and Firmware` GitHub Actions workflow uses
`scripts/discover_examples.py` and `config/ci.json`.

| Surface | Configured version | Target | Entries |
| --- | --- | --- | --- |
| ESP-IDF | `v5.5.5` | `esp32s3` | 13 |
| ESP-IDF | `v6.0.2` | `esp32s3` | 13 |
| Arduino CLI / Arduino-ESP32 | `1.5.1` / `3.3.11` | ESP32S3 Dev Module | 13 |

The configured full matrix contains 39 firmware build jobs. This matrix has
not yet run for this repository. Version pins and board options are
machine-readable in `config/ci.json`.

Manual dispatch accepts `all`, an example name, or a repository-relative
example path. Bundled library examples are excluded.

Each successful build is designed to be packaged and validated before upload.
A `v*` tag is designed to publish the complete artifact set as a GitHub
Release. Factory firmware is not present and is therefore not built, validated,
or published by this workflow.

CI proves compilation and packaging only after a real run. It does not prove
display, radio, power, RTC, battery, sleep, antenna, or regional compliance
behavior on physical hardware.
