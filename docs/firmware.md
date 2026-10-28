# Firmware packaging and flashing

## Source-built example packages

CI builds examples 01–09 for ESP-IDF and Arduino, producing 18 stable firmware
packages through `releases/package_firmware.py`. LoRaWAN examples 10–13 require
your own IDs and keys; configure their source and build locally. They are excluded
from automated builds and release packages.
Each ZIP contains a manifest, SHA-256 checksums, flash arguments, Windows and
POSIX flash helpers, a combined image at offset `0x0`, and the original
binary segments.

Install esptool before using a package:

```text
python -m pip install esptool
flash.bat COMx
./flash.sh /dev/ttyUSB0
```

Read `manifest.json` before flashing and confirm that the target, example,
framework version, and source commit match the intended device.

## Supplied reference binaries

The [firmware directory](../firmware/README.md) contains HF/LF reference binaries
and their SHA-256 checksums. To flash the examples, build the chosen project or
use its source-built package with the included manifest and flash helpers.
