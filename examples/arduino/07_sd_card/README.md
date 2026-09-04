# 07 SD card test

This standalone Arduino-ESP32 sketch tests the onboard microSD socket with the
core `SD_MMC` library in 1-bit mode. It uses GPIO10/CMD, GPIO11/D0,
GPIO12/CLK, and enables the switched 3.3 V rail with GPIO38.

Use the repository's documented ESP32S3 Dev Module options and Arduino-ESP32
3.3.11. Insert a FAT16 or FAT32 microSD card before reset, open
`07_sd_card.ino`, then verify/upload and monitor at 115200 baud.

The sketch never formats the card. It lists at most 16 root entries and uses
only `/waveshare_sd_test/roundtrip.txt`. It refuses to overwrite an existing
file, writes and flushes a known payload, reads it back byte-for-byte, removes
the file, unmounts the filesystem, and turns off the switched rail. It removes
the directory only when this run created it. A reset or power loss during the
test can leave this explicit path on the card.

Expected milestones are card type/capacity, the root listing,
`Write/read verification passed`, `Filesystem synchronized and unmounted`,
and `FINAL: PASS`.

## 中文说明

本示例使用 Arduino-ESP32 自带的 `SD_MMC` 库，以 1-bit 模式测试板载
microSD 卡槽。引脚为 GPIO10/CMD、GPIO11/D0、GPIO12/CLK，GPIO38 高电平
打开卡槽所在的 3.3 V 供电域。

请在复位前插入 FAT16 或 FAT32 格式的卡，串口波特率为 115200。程序不会
格式化存储卡，只使用 `/waveshare_sd_test/roundtrip.txt`，且不会覆盖
已存在的同名文件。写入、刷新和逐字节读回校验后会清理测试文件、卸载
文件系统并关闭供电。掉电或复位可能留下该明确路径。`FINAL: PASS`
仅表示本次软件流程完成，不代表当前待审原理图已最终确认或所有存储卡
均已通过实板验证。