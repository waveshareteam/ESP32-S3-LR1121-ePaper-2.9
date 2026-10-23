# ESP32-S3-LR1121-ePaper-2.9

[English](README.md)

本仓库整理了 Waveshare ESP32-S3-LR1121-ePaper-2.9（产品版本
1.0.0）的 ESP-IDF 示例、Arduino 示例、产品图片和硬件资料。

## 当前状态

源码按对应的 ESP-IDF 与 Arduino 示例面组织，并包含独立的 1-bit SDMMC
存储卡测试。CI 与固件打包配置覆盖所有一方示例。编译和 CI 不能替代
烧录与实板验证。

原理图位于 `hardware/schematics/`，其中连接器块的屏幕尺寸标注仍需核对。
HF/LF 参考固件已放入 `firmware/`；产品图片和尺寸图在当前快照中缺失，
编写手册时需另行补齐。

## 快速入口

- 使用手册交接：[`docs/manual-handoff.zh-CN.md`](docs/manual-handoff.zh-CN.md)

- 中文 Wiki：[`docs/wiki.zh-CN.md`](docs/wiki.zh-CN.md)
- 硬件事实清单：[`docs/hardware-facts.zh-CN.md`](docs/hardware-facts.zh-CN.md)
- ESP-IDF 项目：[`examples/esp-idf/`](examples/esp-idf/)
- Arduino 示例和随仓库库文件：[`examples/arduino/`](examples/arduino/)
- 硬件资料：[`hardware/`](hardware/)
- CI 说明：[`docs/ci.md`](docs/ci.md)
- 固件打包：[`releases/`](releases/)

每个编号目录都是独立示例。构建或烧录前，请先阅读对应框架和示例目录
中的 README。

当前 Wiki 和详情页按“已确认 / 实现参考 / 用户提供待复测 / 待确认”管理
规格。用户提供的“低于 12 µA”仅作为特定条件下的阶段性目标或实测信息，
在补齐固件、供电、测量点、仪表和环境条件前不作为无条件产品保证。

## 示例

| 编号 | ESP-IDF | Arduino | 功能 |
| --- | --- | --- | --- |
| 01 | `01_ws2812_led` | `01_ws2812_led` | 板载 WS2812 |
| 02 | `02_i2c_scan` | `02_i2c_scan` | I2C 设备扫描 |
| 03 | `03_epaper_2_9` | `03_epaper` | 2.9 英寸电子纸 |
| 04 | `04_bat_axp2602` | `04_bat_axp2602` | AXP2602 电量计 |
| 05 | `05_rtc_pcf85063` | `05_rtc_pcf85063` | PCF85063A RTC |
| 06 | `06_lr1121_pingpong` | `06_lr1121_pingpong` | LR1121 Ping-Pong |
| 07 | `07_sd_card` | `07_sd_card` | microSD 1-bit SDMMC 读写测试 |
| 08 | `08_deep_sleep` | `08_deep_sleep` | 深度睡眠 |
| 09 | `09_comprehensive_demo` | `09_comprehensive_demo` | 综合示例 |
| 10 | `10_lr1121_lorawan` | `10_lr1121_lorawan` | LoRaWAN Class A OTAA |
| 11 | `11_LoRaWAN_ABP` | `11_LoRaWAN_ABP` | LoRaWAN Class A ABP |
| 12 | `12_LoRaWAN_Class_C` | `12_LoRaWAN_Class_C` | LoRaWAN Class C |
| 13 | `13_LoRaWAN_Multicast` | `13_LoRaWAN_Multicast` | LoRaWAN 组播 |

LoRaWAN 示例使用占位凭据。启用射频通信前必须按部署地区确认区域、
频率计划、天线和发射功率，并配置有效凭据。不得把真实密钥提交到源码
或支持日志中。

## CI 与 Releases

GitHub Actions 工作流会动态发现直接位于示例目录下的一方项目，当前
稳定版矩阵为 ESP-IDF v5.5.6 和 Arduino-ESP32 3.3.11，另设固定到 SDK
提交 `08e0d30a` 的 ESP-IDF 6.2 开发版兼容任务。全部 13 个 ESP-IDF 示例
（含 09 综合示例）均支持 5.5 和 6.2 验证环境，详见[版本兼容说明](docs/idf-compatibility.md)。
稳定版成功构建后，工作流生成可直接烧录的 ZIP，其中包含 manifest、
SHA-256 校验值、原始分段、合并固件以及 Windows/POSIX 烧录脚本。
推送 `v*` 标签时，工作流计划将这些包发布到 GitHub Releases。

在真实远程仓库运行之前，这些配置仅通过静态检查。CI 编译通过也不能
替代射频、电子纸、电源、休眠、时序或其他实板验证。

## 许可证

原创代码使用 Apache-2.0；原创图片、文档、原理图及硬件资料使用
CC BY 4.0。嵌套的第三方许可证继续优先适用。详见
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) 和
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。
