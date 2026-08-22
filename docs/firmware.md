# Firmware and factory recovery

## Source-built example packages

Successful CI builds are designed to call `releases/package_firmware.py`.
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

## Factory recovery firmware

No factory or recovery image has been supplied for publication. The
`firmware/` directory is reserved for a future image and manifest containing
the product revision, firmware version, release date, target, flash offset,
flash size, SHA-256 digest, flashing parameters, recovery steps, and applicable
license.

Source-built CI artifacts must not be presented as factory firmware.
