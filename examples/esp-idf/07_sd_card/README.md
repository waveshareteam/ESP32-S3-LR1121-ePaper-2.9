# 07 SD card test

This independent ESP-IDF project uses Board Manager for the shared power GPIO and e-paper
and validates the onboard microSD socket through
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

`/ws_sd/test.txt`

The example defaults to FATFS long filename support with heap-allocated buffers
and a maximum filename length of 255 characters. The test path also fits FAT 8.3
limits. Existing `sdkconfig` files retain their previous settings: use
`idf.py menuconfig` → `Component config` → `FAT Filesystem support` to select
long filename support with dynamic allocation on the heap and a maximum length
of 255. Creating longer names with long filename support disabled returns
`errno=22` (`EINVAL`).

The file is created without overwriting an existing file, written, synchronized,
read back byte-for-byte, and removed. The directory is removed only when this
run created it. If the test file already exists, the example stops and asks
you to remove or rename it rather than replacing unknown data. A reset or power
loss during the test can leave this explicit path on the card.

## Build and run

First follow the [board preparation instructions](../README.md#board-manager-integration).
This example now requires `waveshare/waveshare_boards` and generated
`components/gen_bmgr_codes`, just like the other numbered examples.

Ubuntu Bash, with the selected ESP-IDF environment loaded, from the repository root:

```bash
cd examples/esp-idf/07_sd_card
python -m pip install esp-bmgr-assist==0.8.3
idf.py bmgr -l
# If the component lacks this board, complete the manual preparation linked above.
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with the board's serial port. Board Manager supplies the e-paper
configuration, shared SPI bus, and `gpio_epaper_power` (GPIO38).
The example retains the power GPIO reference to keep the rail driven low after
the test. SDMMC/FATFS still perform the mount and file operations directly, with
CMD/D0/CLK pins specified in the application because the current board definition
does not include an SDMMC peripheral.

The project supports the repository's ESP-IDF v5.5 and v6 matrices. Its
component declaration includes FATFS, SDMMC, GPIO, and the split
`esp_driver_sdmmc` dependency required by ESP-IDF v6.

Expected serial milestones include card information, the root listing,
`write/read verification passed`, `filesystem synchronized and unmounted`,
and `FINAL: PASS`. Mount, I/O, cleanup, or unmount failures end with
`FINAL: FAIL`.

## E-paper status

The landscape display uses the same Terminus Bold 8x14 font as the other screen
examples. It shows `SD TEST` before mounting and while testing, then `SD PASS`
or `SD FAIL`, with card capacity, mount, read/write, and unmount status.
The filesystem is unmounted before the final refresh; the panel then sleeps
and the shared power rail turns off, leaving the result visible. Display
initialization/refresh failures are reported on serial and do not stop the SD
test. `DISPLAY` reports screen status separately from the SD `FINAL` result.

The project bundles the same runtime support, e-paper driver, and font components
as example 09; board definitions still come from `waveshare/waveshare_boards`.
Display and SD operations run in a dedicated task with a 16 KiB stack; `app_main`
only starts it and returns. Existing `sdkconfig` files with a smaller main-task
stack need no stack adjustment. Startup logs show the configured main stack and
the dedicated task stack; completion logs show its minimum free stack in bytes.
If task allocation fails, the example reports failure without running on main.

## 中文说明

本示例通过原生 1-bit SDMMC 测试板载 microSD 卡槽，使用 GPIO10/CMD、
GPIO11/D0、GPIO12/CLK。现已接入 Board Manager，必须先下载板级组件、
准备板型并执行 `idf.py bmgr -b esp32_s3_lr1121_epaper_2_9` 生成配置。
共享供电 GPIO 通过 Board Manager 的 `gpio_epaper_power` 获取，当前板型为
GPIO38，高电平打开卡槽所在的 3.3 V 供电域。SDMMC 挂载和文件操作仍使用
ESP-IDF 原生接口；当前组件尚无 SDMMC 外设定义，CMD/D0/CLK 保留在应用中。
请在上电前插入 FAT16 或 FAT32 格式的卡；程序不会格式化存储卡。

墨水屏使用 8×14 粗体横屏显示卡容量、挂载、读写校验和卸载状态，标题从
`SD TEST` 更新为 `SD PASS` 或 `SD FAIL`。卸载 SD 卡后才刷新最终结果，
随后让屏幕休眠、关闭共享供电；结果画面仍会保留。显示失败时仍继续串口
SD 测试，串口 `DISPLAY` 单独报告屏幕状态，`FINAL` 表示 SD 测试结果。
显示与 SD 操作在显式分配 16 KiB 栈的独立任务中执行，`app_main` 只启动
任务后返回；已有 `sdkconfig` 无需调整主任务栈。启动日志打印两个任务的
栈大小，结束时打印测试任务的最小剩余栈空间，便于实板检查栈余量。
若任务创建失败则直接报告失败，不回退到主任务执行显示。

示例默认启用 FATFS 长文件名支持，缓冲区从堆分配，最大文件名长度为 255。
已有 `sdkconfig` 不会被默认值覆盖：请运行 `idf.py menuconfig`，在
`Component config` → `FAT Filesystem support` 中选择长文件名使用堆动态分配，
并将最大长度设为 255。未开启长文件名时，创建超出 8.3 限制的名称会返回
`errno=22`（`EINVAL`）。

测试只使用 `/ws_sd/test.txt`，该路径也兼容 FAT 8.3 短文件名。
程序不会覆盖同名文件；
写入并逐字节读回校验后会删除测试文件，只在本次创建目录时删除目录。
若测试期间掉电或复位，该明确路径可能残留。串口出现 `FINAL: PASS`
表示本次软件流程完成；仓库原理图仍在待审目录，且编译不能替代 SD 实卡
和最终硬件资料确认。
