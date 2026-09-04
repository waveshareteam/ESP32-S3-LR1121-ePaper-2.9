# 07 SD card test

This standalone ESP-IDF example validates the onboard microSD socket through
the native 1-bit SDMMC interface:

- CMD: GPIO10
- D0: GPIO11
- CLK: GPIO12
- switched 3.3 V rail enable: GPIO38, active high

The pin map is consistent between the existing comprehensive example and the
supplied Rev1.1 schematic. The schematic remains under `pending-review`
because of an unrelated e-paper connector label, so compilation is not a
substitute for final schematic approval or a physical-card test.

## Card preparation and data impact

Insert a microSD card formatted as FAT16 or FAT32 before boot. The example
never formats the card. It lists at most 16 root entries, then uses only:

`/waveshare_sd_test/roundtrip.txt`

The file is created without overwriting an existing file, written, synchronized,
read back byte-for-byte, and removed. The directory is removed only when this
run created it. If the test file already exists, the example stops and asks
you to remove or rename it rather than replacing unknown data. A reset or power
loss during the test can leave this explicit path on the card.

## Build and run

```text
idf.py -C examples/esp-idf/07_sd_card set-target esp32s3
idf.py -C examples/esp-idf/07_sd_card build
idf.py -C examples/esp-idf/07_sd_card flash monitor
```

The project supports the repository's ESP-IDF v5.5 and v6 matrices. Its
component declaration includes FATFS, SDMMC, GPIO, and the split
`esp_driver_sdmmc` dependency required by ESP-IDF v6.

Expected serial milestones include card information, the root listing,
`write/read verification passed`, `filesystem synchronized and unmounted`,
and `FINAL: PASS`. Mount, I/O, cleanup, or unmount failures end with
`FINAL: FAIL`.

## 中文说明

本示例通过原生 1-bit SDMMC 测试板载 microSD 卡槽，使用 GPIO10/CMD、
GPIO11/D0、GPIO12/CLK，并由 GPIO38 高电平打开卡槽所在的 3.3 V 供电域。
请在上电前插入 FAT16 或 FAT32 格式的卡；程序不会格式化存储卡。

测试只使用 `/waveshare_sd_test/roundtrip.txt`。程序不会覆盖同名文件；
写入并逐字节读回校验后会删除测试文件，只在本次创建目录时删除目录。
若测试期间掉电或复位，该明确路径可能残留。串口出现 `FINAL: PASS`
表示本次软件流程完成；仓库原理图仍在待审目录，且编译不能替代 SD 实卡
和最终硬件资料确认。