# Contributing

Keep each numbered example independent and preserve the existing board and
component structure unless a separately reviewed migration requires a shared
change.

Before opening a change, report the product revision, affected framework and
example path, framework version, expected behavior, actual behavior, and the
validation level performed. Do not include LoRaWAN keys, tokens, credentials,
or private network logs.

A code change should identify whether it affects ESP-IDF, Arduino, generated
firmware packages, hardware pins, radio settings, or bundled third-party
components. Update documentation and license notices when applicable.

CI compilation and packaging are software validation only. Hardware-facing
changes require separate physical-board results.
