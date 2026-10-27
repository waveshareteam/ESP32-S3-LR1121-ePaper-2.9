# 使用手册编写交接

本说明列出使用手册的章节入口、示例操作及截图要点。产品介绍见
[使用说明](wiki.zh-CN.md)，硬件版本和引脚见[硬件说明](hardware-facts.zh-CN.md)。

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

板型由已发布的 `waveshare/waveshare_boards >=0.1.3` 组件自动提供，
无需手动复制。本仓库不携带 `boards`、`gen_bmgr_codes`、下载组件或构建残留。

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

`idf.py bmgr -l` 自动下载包含该板型的 `waveshare/waveshare_boards >=0.1.3`
组件。随后选择板型并编译：

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
调整方法见该示例 README。每个示例的下载组件各自独立；清理或更新组件后，
重新列出板型并生成配置。下载组件与生成代码均不提交到本仓库，
CI 使用相同的组件下载和板型生成流程。

## 版本与构建记录

Arduino 使用 ESP32 core 3.3.11 和随附 RadioLib 7.7.1。
ESP-IDF 使用 5.5.6；6.2 按固定 SDK 提交作为开发版兼容环境。
构建记录、适用范围和板型准备步骤见[版本兼容说明](idf-compatibility.md)。

手册截图使用当前示例界面和编号，07 显示 SD 状态，09 使用
`ACTIVE_READY` 与 `LORA_RX` 状态。日志中的 `PASS`、`FAIL`、`NOT_TESTED`
和 `UNAVAILABLE` 按示例 README 的定义解释。
