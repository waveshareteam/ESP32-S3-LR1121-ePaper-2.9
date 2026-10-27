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

The e-paper screen uses the bundled `Waveshare_LR1121_EPaper` library and the
same Terminus Bold 8x14 font as the other examples. It shows `SD TEST`, then
`SD PASS` or `SD FAIL`, with card capacity, mount, read/write, and unmount status.
After `SD_MMC.end()` the final screen refresh completes, the panel sleeps, SPI
stops, and the shared rail turns off; the result remains visible. Arduino's
`SD_MMC.end()` has no result code, so the screen says `Unmount: done` rather
than claiming verified unmount success. Display failures are reported on serial
and the SD test continues; `DISPLAY` reports screen status separately from `FINAL`.

## 中文说明

本示例使用 Arduino-ESP32 自带的 `SD_MMC` 库，以 1-bit 模式测试板载
microSD 卡槽。引脚为 GPIO10/CMD、GPIO11/D0、GPIO12/CLK，GPIO38 高电平
打开卡槽所在的 3.3 V 供电域。

墨水屏使用随仓库提供的显示库和 8×14 粗体，横屏显示卡容量、挂载、读写
校验和卸载状态，标题从 `SD TEST` 更新为 `SD PASS` 或 `SD FAIL`。
先调用 `SD_MMC.end()`，再刷新最终结果、让屏幕休眠、停止 SPI 并关闭
共享供电，断电后仍保留结果画面。Arduino 的卸载接口无返回值，屏幕显示
`Unmount: done`。显示失败不会中断 SD 测试，串口 `DISPLAY` 单独报告屏幕
状态，`FINAL` 表示 SD 测试结果。

请在复位前插入 FAT16 或 FAT32 格式的卡，串口波特率为 115200。程序不会
格式化存储卡，只使用 `/waveshare_sd_test/roundtrip.txt`，且不会覆盖
已存在的同名文件。写入、刷新和逐字节读回校验后会清理测试文件、卸载
文件系统并关闭供电。掉电或复位可能留下该明确路径。`FINAL: PASS`
表示本次 SD 卡读写、清理和卸载流程完成。
