# ESP32-S3-LR1121-ePaper-2.9

> 历史资料草稿：部分路径、编号和测试状态已变化。编写使用手册时以 [当前交接说明](manual-handoff.zh-CN.md)、示例 README 和源码为准；本草稿中的旧构建失败记录不代表当前状态。

> 文档状态：预发布草案，基于仓库提交 `e272df5` 整理。硬件事实、冲突项和
> 待补资料分别见 [硬件事实清单](hardware-facts.zh-CN.md) 与
> [待补资料清单](pending-materials.zh-CN.md)。

## 产品简介

ESP32-S3-LR1121-ePaper-2.9 是一款集成 ESP32-S3、LR1121 射频芯片和
2.9 英寸黑白电子纸的低功耗物联网开发板。板上同时提供 RTC、电量计、
TF 卡、WS2812、按键和扩展接口，适合周期采集、远程状态显示、LoRa/LoRaWAN
终端和电池供电的信息牌等应用。

产品面向“短时唤醒、采集/通信/刷新、长时间休眠”的工作模式：电子纸完成
刷新后可保持画面，LR1121、AXP2602 和显示控制器可进入睡眠，共享外设电源
可在 ESP32-S3 Deep-sleep 前关闭。

用户提供的阶段性目标/实测信息为：**在特定条件下，整板最低电流可低于
12 µA**。由于测试固件、供电、测量点和仪表条件尚未补齐，本文不把该值作为
无条件保证。复测方法见[低功耗原理与测试](#低功耗原理与测试)。

### 当前资料提示

- 仓库现有原理图处于待审状态，其中电子纸连接器块存在 2.13/2.9 英寸标注冲突。
- 机械尺寸图尚未提供，本文不从照片估算长宽、孔位或高度。
- 正面实物图的显示内容包含 Join EUI，公开发布前需脱敏或确认标识符可公开。
- 仓库整理时未完成编译、烧录和实板验证；本文会区分“代码支持”和“硬件实测”。
- ESP-IDF 08 及多个 Board Manager 示例当前存在依赖/生成文件缺口，见
  [已知构建限制](#已知构建限制)。

## 产品特点

- ESP32-S3-WROOM-1-N16R8，16 MB Flash 和 8 MB OPI PSRAM。
- 2.9 英寸黑白电子纸，296 × 128 像素，支持全刷和 FAST 全屏刷新策略。
- 集成 LR1121，示例覆盖 LoRa Ping-Pong、LoRaWAN OTAA/ABP、Class C 和组播。
- Sub-GHz 高功率 PA 路径由板级适配自动选择；实际功率受区域法规和供电条件限制。
- 板载 PCF85063A RTC、AXP2602 电量计、TF 卡座、1 颗 WS2812 和 3 个按键。
- 电子纸与 LR1121 共用 SPI，通过独立片选和总线互斥实现顺序访问。
- GPIO38 控制共享外设电源，深睡示例包含外设睡眠、断电保持和 IO 隔离流程。
- 同时提供 Arduino-ESP32 与 ESP-IDF 示例，编号和功能一一对应。

## 产品参数

除“芯片能力”外，下表只列仓库可核对内容。射频、功耗和机械数据的完整规格
需等待 BOM、修正版原理图和实测报告。

| 项目 | 参数 | 证据状态 |
| --- | --- | --- |
| 主控 | ESP32-S3，双核 Xtensa LX7 | 芯片能力 |
| 模组 | ESP32-S3-WROOM-1-N16R8 | 已确认 |
| 存储 | 16 MB Flash、8 MB OPI PSRAM | 已确认 |
| Wi-Fi / 蓝牙 | 2.4 GHz Wi-Fi、Bluetooth LE | ESP32-S3 芯片能力 |
| 电子纸 | 2.9 英寸，296 × 128，黑白 1 bpp | 代码/丝印一致 |
| 射频芯片 | Semtech LR1121 | 已确认 |
| 当前示例区域 | EU868；Ping-Pong 默认 868.0 MHz | 示例基线，不等于全球配置 |
| 芯片调制能力 | LoRa、(G)FSK、LR-FHSS | LR1121 芯片能力 |
| 天线 | LoRa 与 2.4G 两路射频接口 | 接口型号/匹配范围待确认 |
| RTC | PCF85063A，I2C 地址 0x51 | 实现参考 |
| 电量计 | AXP2602，I2C 地址 0x62 | 实现参考 |
| 外部存储 | TF 卡座 | 软件支持状态待确认 |
| USB | USB Type-C，示例使用 USB CDC/JTAG | 实现参考 |
| 工作电压/温度 | 待正式规格确认 | 待确认 |
| 整板尺寸 | 待机械图 | 待确认 |
| 休眠电流 | 用户提供目标/实测低于 12 µA，测试条件待补 | 用户提供，待复测 |

LR1121 芯片可覆盖 Sub-GHz、1.9–2.1 GHz S 频段和 2.4 GHz ISM，但板上
射频匹配网络决定整板实际可用范围。本文不直接套用 ESP32-S3-LR1121-HF/LF
相近模组的频段、灵敏度、距离或耗流。

## 资源与接口说明

### 板载资源

1. **USB Type-C**：供电、下载和串口调试。
2. **BOOT / RESET / KEY**：进入下载模式、复位和应用按键。
3. **ESP32-S3-WROOM-1-N16R8**：主控、Flash/PSRAM、Wi-Fi/BLE。
4. **LR1121**：远距离低功耗射频；板上显示两路天线接口。
5. **2.9 英寸电子纸**：296 × 128 黑白显示。
6. **TF 卡座**：外部存储接口，当前教程不承诺已有完整文件系统示例。
7. **PCF85063A**：RTC 和中断输出。
8. **AXP2602**：电池电压、电流、SOC 和温度读取。
9. **BAT / RTC BAT**：主电池和 RTC 后备电池接口，极性与范围待正式规格确认。
10. **14 Pin 扩展座**：引出供电、UART、GPIO 和 I2C 等信号。

### 总线共享注意事项

电子纸和 LR1121 共用 SCK/MOSI/MISO。显示刷新时间较长，无线接收窗口具有
严格时序，因此应用应：

- 使用独立 CS，空闲时保持片选无效；
- 在射频操作完成后再刷新电子纸；
- 不在 RadioLib RX 窗口或回调中执行显示刷新、NVS 写入等耗时操作；
- 多任务场景同时使用 SPI 互斥和“无线对象单一所有者”策略。

## 引脚定义

| 资源 | ESP32-S3 引脚 | 方向/说明 |
| --- | --- | --- |
| BOOT | GPIO0 | 输入，低有效；可作为深睡唤醒 |
| KEY | GPIO1 | 输入，示例用作无线发送 |
| WS2812 | GPIO21 | 输出，1 颗 GRB LED |
| 共享外设电源 | GPIO38 | 输出，高有效 |
| I2C SDA | GPIO13 | 双向，400 kHz |
| I2C SCL | GPIO14 | 输出，400 kHz |
| RTC IRQ | GPIO7 | 输入 |
| AXP2602 CHG/IRQ | GPIO47 / GPIO48 | 输入，命名待正式图纸确认 |
| SPI SCK | GPIO40 | 输出，共享 |
| SPI MOSI | GPIO45 | 输出，共享 |
| SPI MISO | GPIO46 | 输入，共享 |
| ePaper BUSY | GPIO8 | 输入，高电平忙 |
| ePaper RST | GPIO9 | 输出 |
| ePaper DC | GPIO17 | 输出 |
| ePaper CS | GPIO18 | 输出 |
| LR1121 DIO1 | GPIO6 | 输入，中断 |
| LR1121 NSS | GPIO42 | 输出，低有效 |
| LR1121 RST | GPIO39 | 输出 |
| LR1121 BUSY | GPIO41 | 输入 |
| TF CMD | GPIO10 | 原理图实现参考 |
| TF D0 | GPIO11 | 原理图实现参考 |
| TF CLK | GPIO12 | 原理图实现参考 |

## 尺寸与结构

当前仓库没有可发布的机械图。正式尺寸章节需要包含：

- PCB 外形长宽和公差；
- 4 个安装孔的孔径和孔中心坐标；
- USB、天线、电池、RTC 电池、扩展座和 TF 卡的相对位置；
- 屏幕外形/可视区和 FPC 方向；
- 正反面最大元件高度。

在资料补齐前，产品详情页只展示真实实物图和接口位置参考，不提供可用于
机壳开孔的像素测量值。

## 低功耗原理与测试

### 低功耗状态路径

```text
上电/唤醒
  -> 采集 RTC/电量/传感数据
  -> LR1121 通信
  -> 电子纸刷新
  -> LR1121 SET_SLEEP
  -> AXP2602 sleep
  -> 电子纸 sleep
  -> 隔离射频与总线 GPIO，GPIO38 断开共享电源并保持
  -> ESP32-S3 Deep-sleep
  -> 定时器或 BOOT(GPIO0) 唤醒
```

电子纸的画面保持能力降低了持续显示耗电，但刷新期间仍需要电源和完整波形。
LR1121 在关断共享电源前先进入睡眠，可避免异常总线状态；随后应将与无电源
器件相连的 GPIO 置为高阻，防止 IO 反向供电。

### 低功耗示例差异

- Arduino 示例默认 30 秒定时唤醒，并允许 BOOT(GPIO0) 唤醒。
- ESP-IDF 示例默认 600 秒定时唤醒，并额外执行 LR1121 IO 隔离、GPIO38
  拉低/保持和 RTC GPIO 配置，更适合作为低功耗复测基线。

### 复测固件

建议以 `examples/esp-idf/08_deep_sleep` 为起点。在当前依赖缺口修复、构建
通过并确认硬件后，固定 Git 提交与 `sdkconfig`，不要在测量期间连接串口监视器
或让 USB 主机周期唤醒设备。

### 测试条件记录表

| 项目 | 本次记录 |
| --- | --- |
| 固件与提交 | 待补 |
| ESP-IDF/Arduino 版本 | 待补 |
| 供电入口与电压 | 待补 |
| 测量点 | 待补 |
| 仪表/量程/采样率 | 待补 |
| 屏幕/LR1121/Wi-Fi/BLE/TF/WS2812 状态 | 待补 |
| RTC/AXP2602 状态 | 待补 |
| 唤醒源与周期 | 待补 |
| 环境温度与稳定时间 | 待补 |
| 最低/平均/峰值/采样时长 | 待补 |
| 板卡数量与离散度 | 待补 |

发布写法应为：“在上述固件、供电和测量条件下，样本板的深睡电流为……”。
不要只写“整板功耗 <12 µA”。

## Arduino 环境搭建

### 版本与依赖

- Arduino IDE 2.x
- Arduino-ESP32 3.3.11
- RadioLib 7.7.1
- 仓库内 `Waveshare_LR1121_EPaper` 库

在 Arduino IDE 的“首选项”中加入官方开发板索引：

```text
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

安装 `esp32 by Espressif Systems 3.3.11` 和 `RadioLib 7.7.1`，再把
`examples/arduino/libraries/Waveshare_LR1121_EPaper` 复制到 Arduino
sketchbook 的 `libraries` 目录并重启 IDE。

### 开发板配置

| 选项 | 值 |
| --- | --- |
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| USB Mode | Hardware CDC and JTAG |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Serial Monitor | 115200 baud |

屏幕选择文件为：
`examples/arduino/libraries/Waveshare_LR1121_EPaper/src/WaveshareConfig.h`。
本产品应只启用 `WAVESHARE_EPAPER_290`。

## ESP-IDF 环境搭建

仓库 CI 配置计划覆盖 ESP-IDF v5.5.5 和 v6.0.2，但部分示例的实际依赖与该
矩阵不一致。首次构建前请先阅读[已知构建限制](#已知构建限制)。

### Ubuntu Bash

已安装并激活 ESP-IDF 后，从仓库根目录执行：

```bash
idf.py -C examples/esp-idf/01_ws2812_led set-target esp32s3
idf.py -C examples/esp-idf/01_ws2812_led build
idf.py -C examples/esp-idf/01_ws2812_led -p /dev/ttyACM0 flash monitor
```

按 `Ctrl+]` 退出监视器。实际串口名可用 `ls /dev/ttyACM* /dev/ttyUSB*` 检查。

### Windows PowerShell

在 ESP-IDF PowerShell 环境中执行：

```powershell
idf.py -C examples/esp-idf/01_ws2812_led set-target esp32s3
idf.py -C examples/esp-idf/01_ws2812_led build
idf.py -C examples/esp-idf/01_ws2812_led -p COM5 flash monitor
```

把 `COM5` 换成设备管理器中实际出现的端口。

## 示例说明

Arduino 与 ESP-IDF 使用相同编号。除特别说明外，串口波特率为 115200。

| 编号 | 示例 | 作用与预期结果 | 使用前检查 |
| --- | --- | --- | --- |
| 01 | WS2812 LED | 红、绿、蓝和呼吸测试 | GPIO21、共享电源、亮度限制 |
| 02 | I2C Scan | 发现 0x51 PCF85063A、0x62 AXP2602 | GPIO38 高、SDA13/SCL14 |
| 03 | ePaper | 显示面板信息并测试全刷/FAST 刷新 | 2.9 英寸选择、BUSY 高有效 |
| 04 | BAT AXP2602 | 输出电压、电流、SOC、温度 | 电池接法、采样电阻/方向待确认 |
| 05 | RTC PCF85063 | 设置/读取日期时间与中断 | 0x51、IRQ GPIO7、后备电池 |
| 06 | LR1121 Ping-Pong | 两板之间交替 PING/PONG | 两块板、匹配天线、合法频率/功率 |
| 07 | Deep Sleep | 外设睡眠、断电保持、定时/按键唤醒 | 低功耗测量条件、依赖缺口 |
| 08 | Comprehensive | 电量、RTC、显示、无线、存储综合状态 | Wi-Fi 凭据只放本地；IDF >=6.2 依赖冲突 |
| 09 | LoRaWAN OTAA | Class A OTAA，显示/输出 FCntUp | EUI/密钥、EU868、网关与服务器 |
| 10 | LoRaWAN ABP | Class A ABP，持久化帧计数器 | DevAddr/会话密钥、计数器同步 |
| 11 | LoRaWAN Class C | 持续接收与周期上行 | 持续供电，不进入深睡；TCXO 文档冲突 |
| 12 | LoRaWAN Multicast | OTAA 单播 + 静态 Class C 组播 | 组地址/组密钥、频率/DR/计数器 |

### LoRaWAN 安全与法规

09–12 的真实 EUI、地址和密钥只能保存在示例指定的本地 secrets 文件中，
不得提交、截图或粘贴到支持日志。全零占位未替换时，示例应在射频激活前停止。

仓库默认围绕 EU868 示例开发。部署前必须按所在地法规选择区域、频率计划、
发射功率和天线；高功率 PA 路径不代表允许以 22 dBm 发射。

## 烧录与运行

1. 使用支持数据传输的 USB Type-C 线连接开发板。
2. 选择正确的 ESP32-S3 目标、N16R8 存储配置和实际串口。
3. 编译成功后再烧录；不要把 CI 静态检查当成可烧录证明。
4. 若没有串口，按住 BOOT(GPIO0)，点按 RESET，松开 BOOT 后重新检查端口。
5. 打开 115200 串口监视器，核对首条有效日志和外设状态。
6. 射频测试必须先接匹配天线，再配置合法的区域和发射功率。
7. 电子纸刷新期间不要断电；完整刷新完成后再进入睡眠。

## 已知构建限制

截至仓库提交 `e272df5`，以下限制仍需修复和重新验证：

1. `09_comprehensive_demo` 声明 ESP-IDF `>=6.2,<6.3`，与计划的 v5.5.5、
   v6.0.2 CI 矩阵不兼容。
2. ESP-IDF README 声称 02–12 已提交 `components/gen_bmgr_codes`，但干净检出
   中 02、07–12 缺少该目录，相关 CMake/Board Manager 配置可能无法完成。
3. 11/12 的 README 写 TCXO 1.6 V，可执行适配代码使用 3.0 V；必须以修正版
   原理图和实板确认后统一。
4. 某些 README 把共享总线写成 SPI3_HOST，而板级 YAML/生成配置为 SPI2_HOST。
5. 本轮没有可用的物理开发板，无法完成显示、射频、充电和深睡电流复测。

本轮已使用 CI 对应的 Arduino-ESP32 3.3.11 配置完成 Arduino 01 示例编译；
其余 Arduino 示例未逐项编译。仓库自带 Arduino 静态检查在当前环境解析
RadioLib 的框架头 `Arduino.h` 时失败，因此不能把该检查标为通过。当前主机
未安装可用的 `idf.py`，全部 ESP-IDF 示例仍待依赖修复后的真实构建。

因此，本文只把 01–12 列为“仓库已提供示例”，不把所有示例标成“已验证可用”。

## FAQ

### 编译时找不到 Board Manager 生成组件怎么办？

先确认所选示例是否包含 `components/gen_bmgr_codes`。不要从其他编号示例直接
复制生成目录；不同示例可能有不同配置。应使用该示例声明的 ESP-IDF 和
`esp_board_manager` 版本重新生成，并核对生成文件中没有本机绝对路径。

### 电子纸没有刷新或 BUSY 超时怎么办？

确认启用 2.9 英寸型号、GPIO38 为高、BUSY=GPIO8 且高电平表示忙；检查
RST/DC/CS 和共享 SPI 是否被 LR1121 长时间占用。不要按待审原理图中的
“2.13inch”标签切换驱动。

### I2C 扫描不到设备怎么办？

确认 GPIO38 已上电，SDA/SCL 为 GPIO13/GPIO14，预期地址为 0x51 和 0x62。
再检查上拉、电池和焊接状态。

### LR1121 初始化失败怎么办？

断电后先连接匹配天线，再检查 NSS42、DIO1=6、RST39、BUSY41、共享 SPI、
RF switch 和 TCXO。当前 TCXO 电压资料有冲突，不要反复盲改电压试错。

### OTAA 一直无法入网怎么办？

检查区域、网关频率计划、JoinEUI/DevEUI 字节序、AppKey/NwkKey、MAC 版本和
覆盖情况。查看网络服务器日志，但不要把密钥贴到工单或串口日志。

### ABP/OTAA 出现帧计数器拒绝怎么办？

不要在每次启动时擦除 Preferences/NVS。只有在设备端和服务器端同时规划
计数器重置时才清除会话状态。

### 如何测到最低休眠电流？

使用 ESP-IDF 07 作为基线，关闭无线和显示电源域并隔离 IO，在 BAT 或明确的
整板输入测量点供电。断开 USB 串口后等待稳定，再记录平均值和唤醒峰值。
没有完整条件时，单个瞬时最低读数不能作为产品规格。

## 资料下载

| 资料 | 仓库路径 | 状态 |
| --- | --- | --- |
| Arduino 示例 | `examples/arduino/` | 已提供，待编译/实板验证 |
| ESP-IDF 示例 | `examples/esp-idf/` | 已提供，存在已知依赖缺口 |
| 产品正反面图 | `assets/product/` | 已提供，正面图待标识符审核 |
| 原理图 | `hardware/schematics/pending-review/` | 待审，不作为最终公开图 |
| 尺寸图 | `hardware/dimensions/` | 待补 |
| 工厂/恢复固件 | `firmware/` | 待补 |
| 固件打包工具 | `releases/` | 已提供，未发布成品 |
| 来源与事实边界 | `docs/source-register.zh-CN.md` | 已提供 |

## 技术支持前应准备的信息

- 产品版本、PCB 丝印和仓库 Git 提交；
- 使用的框架、版本、示例编号和完整编译错误；
- 供电方式、天线/电池/屏幕连接和复现步骤；
- 115200 串口日志（删除所有密钥、EUI 和服务器地址）；
- 射频问题的地区/频段/网关配置，功耗问题的完整测量条件。
