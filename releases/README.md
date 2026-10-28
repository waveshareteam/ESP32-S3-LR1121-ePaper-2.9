# Firmware archives

The `Build Examples and Firmware` workflow packages examples 01–09 as directly
flashable ZIPs: 9 ESP-IDF packages and 9 Arduino packages per stable release.
LoRaWAN examples 10–13 are source only because they require your own IDs and keys.
Configure them and build locally; CI does not compile or publish their firmware.
The release validator uses the same `source_only_examples` configuration as CI
and rejects archives for those excluded examples.

Each archive contains:

```text
README.md
SHA256SUMS
flash.sh
flash.bat
flash_args.txt
manifest.json
bin/*.bin
```

Install esptool before flashing:

```text
python -m pip install esptool
flash.bat COMx
./flash.sh /dev/ttyUSB0
```

The packager reads ESP-IDF `flasher_args.json` or the Arduino exported
binaries, preserves the source segments, and creates a combined image at
offset `0x0`.

Generated archives belong under `releases/dist/` or CI artifact storage and
must not be committed. The HF/LF reference binaries in `firmware/` are separate
inputs and are not repackaged as source-build artifacts.

Download artifacts from a successful GitHub Actions run with
`python3 releases/download_artifacts.py`. Select the GitHub repository with `--repo OWNER/REPOSITORY`.
