# 使用手册编写交接

本分支整合已测试的本地示例改动与 GitLab 主分支的 SD 示例、编号调整和
SensorLib 电量计迁移。它是手册编写与代码评审基线，不是已发布的成品固件。

## 2026-10-23 更新：文档编写注意事项

本次在已有 ESP-IDF 5.5 / 6.2 兼容分支上同步以下修复。文档与截图以当前
源码行为为准；不要继续使用修改前的压力测试说明或 07 的纯串口界面。

| 范围 | 当前行为 | 手册需要更新 |
| --- | --- | --- |
| ESP-IDF 01–13 | 支持 5.5，固定提交的 6.2 开发版用于兼容验证 | 5.5 作为推荐稳定环境，勿将 6.2 开发版写成正式发行版 |
| ESP-IDF 07 | 现使用 Board Manager 管理共享供电、SPI 和墨水屏配置 | 删除“07 不需要 bmgr”的说明，采用与其他示例相同的组件准备和板型生成步骤 |
| 两套框架的 07 | 墨水屏显示卡容量、挂载、读写和卸载状态，最终为 `SD PASS` / `SD FAIL` | 新增屏幕截图；SD 卸载、结果刷新完成后屏幕休眠并关共享供电，画面保留 |
| ESP-IDF 07 | 默认支持最长 255 字符的 FATFS 长文件名；测试使用 `/ws_sd/test.txt` | 旧 `sdkconfig` 仍需在 menuconfig 开启堆分配的长文件名支持；Arduino 测试路径仍为 `/waveshare_sd_test/roundtrip.txt` |
| ESP-IDF 07 | 显示与 SD 测试使用显式 16 KiB 独立任务栈 | 删除“必须手动把主任务栈调成 8192”的临时操作；日志会打印任务栈大小和最小剩余空间 |
| ESP-IDF 09 | 移除持续 CPU/RAM/PSRAM 压力任务，保留开机和唤醒后的一次自检 | 删除“持续双核满负载”、压测迭代/吞吐、`ACTIVE_STRESS` 及 `STRESS` 截图；新状态为 `ACTIVE_READY`，随后进入 `LORA_RX` |
| ESP-IDF 09 | RAM/PSRAM 自检按可用内存调整测试大小，分配失败显示 `NOT_TESTED` | 自检字节数是实际测试范围，不是整片内存容量；未测仍会影响总体自检结果 |
| ESP-IDF 09 | GPIO ISR 服务集中安装，共享 SPI 复用已有总线；LVGL 改为系统堆按需分配 | 使用新启动日志，旧项目按 09 README 切换 LVGL allocator，不能只更新 defaults |
| ESP-IDF 09 | 修正 AXP2602 温度日志与无效样本处理，启用采样后有限等待 ADC 就绪 | 无效温度是 `UNAVAILABLE`，不能把旧日志中的异常整数或伪造 0°C 写入规格 |

07 不格式化卡、不覆盖已有测试文件，只清理本轮创建的数据。Arduino
`SD_MMC.end()` 无返回值，因此屏幕显示 `Unmount: done`；ESP-IDF 根据卸载
返回值显示 `Unmount: OK` 或 `FAIL`。两者串口的 `DISPLAY` 单独报告显示结果，
`FINAL` 报告 SD 测试结果，显示故障不会阻止串口 SD 测试。

09 保留 Wi-Fi/BLE、LoRa、按键、电量监控和低功耗流程。删除持续压力测试
不等于关闭所有内存自检，也没有屏蔽真实错误或关闭棕断、看门狗保护。

板型仍由独立的 `waveshare/waveshare_boards` 组件提供，本仓库不携带
`boards`、`gen_bmgr_codes`、下载组件或构建残留。组件缺板型时继续使用下文的
手动准备流程；不要在使用手册中要求把板型添加到产品仓库。

## 采用当前编号

ESP-IDF 与 Arduino 各有 13 个示例，目录见根 README 的对照表。
原本的 07 深睡移为 **08**，原本的 08 综合示例移为 **09**，原来的
09–12 LoRaWAN 示例移为 10–13；新增 **07 SD 卡**。截图、章节标题、下载
说明和命令均应使用当前编号。

## 推荐编写入口

- [Arduino 安装与示例说明](../examples/arduino/README.md)
- [Arduino 09 综合测试](../examples/arduino/09_comprehensive_demo/README.md)
- [ESP-IDF 环境与 Board Manager](../examples/esp-idf/README.md)
- [ESP-IDF 03 电子纸功能测试](../examples/esp-idf/03_epaper_2_9/README.md)
- [ESP-IDF 09 综合测试](../examples/esp-idf/09_comprehensive_demo/README.md)
- [ESP-IDF 07 SD 卡、显示与长文件名设置](../examples/esp-idf/07_sd_card/README.md)
- [Arduino 07 SD 卡操作与显示](../examples/arduino/07_sd_card/README.md)
- [硬件原理图](../hardware/schematics/ESP32-S3-LR1121-ePaper-2.9-XF-Schematics.pdf)
- [HF/LF 参考固件及校验值](../firmware/README.md)

## 需要写清楚的操作

Arduino 选择 ESP32S3 Dev Module、16 MB Flash、OPI PSRAM、USB CDC On Boot
Enabled、Hardware CDC and JTAG。将 IDE 首选项中的“项目文件夹位置”设置为
本仓库的 `examples/arduino` 目录并重启 IDE，直接使用 `libraries` 下随附的
库，无需另行下载或复制。所有墨水屏文字使用 Terminus Bold
8×14，长文本省略显示，综合示例采用双列状态格和底部 LoRa 计数。

综合示例中 GPIO1 稳定按下一次发送一帧；GPIO0 双击休眠，深睡时单击唤醒。
启动和唤醒均重新自检。Wi-Fi、LoRa 参数集中在各自的 `main/app_config.h`
或 `app_config.h`；截图和手册不包含真实凭据。

ESP-IDF 源码不包含 `build`、`managed_components`、`sdkconfig`、锁文件和
`components/gen_bmgr_codes`，也不保存本地 `boards` 副本。先在所选示例目录执行：

```bash
python -m pip install esp-bmgr-assist==0.8.3
idf.py bmgr -l
```

当前下载的 `waveshare/waveshare_boards` 0.1.2 尚无该板型。按现阶段流程，
将板级库维护者提供的 `esp32_s3_lr1121_epaper_2_9` 文件夹手动复制到本示例的
`managed_components/waveshare__waveshare_boards/boards/`，再执行：

```bash
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9
idf.py build
```

以上命令用于 Ubuntu Bash（已加载对应 ESP-IDF 环境），适用于全部 01–13 示例。
07 SD 示例现也使用 Board Manager 获取共享供电 GPIO、SPI 和墨水屏配置，
需要生成板级配置；SDMMC 挂载与文件读写继续使用原生接口。
Arduino 与 ESP-IDF 的 07 均通过墨水屏显示卡容量、挂载、读写和卸载状态，
最终显示 `SD PASS` 或 `SD FAIL`，完成刷新后休眠屏幕并关闭共享供电。
ESP-IDF 07 默认启用 FATFS 长文件名，显示和 SD 操作使用独立的 16 KiB
任务栈，不依赖已有配置中的主任务栈大小；已有 `sdkconfig` 的长文件名
调整方法见该示例 README。后续板级组件发布该板型后，省略
手动复制步骤。每个示例的下载组件各自独立；清理或更新组件后需要重新确认
板型是否存在。板型与
生成代码均不提交到本仓库，自动 CI 需等待板型进入已发布组件。

## 验证边界与待补材料

- 整合后的全部 13 个 Arduino 示例已使用 Arduino-ESP32 3.3.11、RadioLib
  7.7.1 编译通过（ESP32S3 Dev Module、16 MB Flash、OPI PSRAM）。综合示例
  在整合前也通过了 2.13 寸配置编译。
- 字体渲染、两种屏幕尺寸、文本边界、消抖和双击逻辑已通过主机测试。
- 移除仓库内板型副本前，全部 13 个 ESP-IDF 示例已在 5.5.6 和固定提交的
  6.2 开发版下编译通过，共 26 次构建。移除后，01 示例按“下载组件后
  手动复制板型”的流程完成双版本生成与编译，详见版本兼容说明。
  本轮修改后的 07、09 均已重新通过双版本编译。09 源码契约、RAM/ISR、
  SPI/温度主机检查通过，两个环境各通过 3 项 NimBLE 生命周期回归。
  移除持续压测后，09 应用分区余量在 5.5.6 下为 22720 字节（约 1%），
  6.2 下为 38608 字节（约 3%），增加功能前需要重新检查大小。
- Arduino 07 的新增显示通过 3.3.11 编译、两种尺寸的 UI 检查，以及 11 种
  正常/故障流程模拟，覆盖卸载后关电和已有文件保护。
- 用户已在本轮 ESP-IDF 07 栈溢出修复后确认实板运行正常；这不代表已完成
  Arduino 07、全部 09 场景或 6.2 实板验证。09 的 USB 串口断连原因仍不能仅凭
  原有日志确定，不应写为已经修复的 Wi-Fi 或 USB 硬件问题。
- 全部 13 个 ESP-IDF 示例现声明 `>=5.5.0,<6.3.0`，包括 09 综合示例。
  手册使用 5.5 稳定版；6.2 以固定 SDK 提交作为开发版兼容验证。
  完整构建矩阵与验证边界见 [版本兼容说明](idf-compatibility.md)。
- 应补充当前固件的屏幕实拍、按键操作、双板 LoRa 收发、Wi-Fi/BLE 共存和
  深睡电流记录。编译、主机测试均不替代这些实板检查。
- 当前快照没有产品正反面照片和尺寸图；HF/LF 二进制的源码版本与烧录偏移
  未建立对应，不作为本 MR 的编译产物。
- `wiki.zh-CN.md` 等早期资料保留为历史草稿；其旧路径、旧编号和失败记录
  不应直接抄入手册。规格不确定项需要按原理图、BOM 或测量记录确认。
