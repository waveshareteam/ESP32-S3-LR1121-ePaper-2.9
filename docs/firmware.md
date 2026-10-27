# Firmware packaging and flashing

## Source-built example packages

Successful CI builds call `releases/package_firmware.py`.
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
