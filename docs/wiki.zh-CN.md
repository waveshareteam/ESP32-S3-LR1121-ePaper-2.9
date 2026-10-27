# ESP32-S3-LR1121-ePaper-2.9 使用说明

ESP32-S3-LR1121-ePaper-2.9 集成 ESP32-S3、LR1121 射频芯片和 2.9 英寸
黑白电子纸，提供 RTC、电量计、microSD 卡槽、WS2812 和按键，适合周期
采集、LoRa/LoRaWAN 通信及电池供电的信息显示应用。

![ESP32-S3-LR1121-ePaper-2.9](images/ESP32-S3-LR1121-ePaper-2.9-details-5-2.webp)

## 产品参数

| 项目 | 参数 |
| --- | --- |
| 硬件版本 | 1.1 |
| 主控模组 | ESP32-S3-WROOM-1-N16R8 |
| 存储 | 16 MB Flash、8 MB OPI PSRAM |
| 无线连接 | 2.4 GHz Wi-Fi、Bluetooth LE |
| 电子纸 | 2.9 英寸，296 × 128 像素，黑白显示 |
| 射频芯片 | Semtech LR1121 |
| RTC | PCF85063A，I2C 地址 0x51 |
| 电量计 | AXP2602，I2C 地址 0x62 |
| 存储接口 | microSD，1-bit SDMMC |
| 状态灯与按键 | 1 颗 WS2812；BOOT、KEY、RESET |
| USB | USB Type-C，供电、下载和串口调试 |

引脚表及共享供电说明见[硬件说明](hardware-facts.zh-CN.md)，电路连接见
[Rev1.1 原理图](../hardware/schematics/ESP32-S3-LR1121-ePaper-2.9-XF-Schematics.pdf)。

## Arduino 环境

安装 Arduino IDE 2.x 和 `esp32 by Espressif Systems` 3.3.11。开发板索引：

```text
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

将 IDE 首选项的“项目文件夹位置”设置为本仓库的 `examples/arduino`，
重启 IDE，使用其中随附的 RadioLib 和 Waveshare 显示库。

| 选项 | 值 |
| --- | --- |
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| USB Mode | Hardware CDC and JTAG |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Serial Monitor | 115200 baud |

打开所选示例的 `.ino`，选择串口后编译上传。完整说明见
[Arduino 示例入口](../examples/arduino/README.md)。

## ESP-IDF 环境

使用 ESP-IDF 5.5.6。6.2 开发版使用固定 SDK 提交，版本信息见
[框架兼容说明](idf-compatibility.md)。

所有示例通过 `waveshare/waveshare_boards` 使用 `esp_board_manager`。
先按[板型准备说明](../examples/esp-idf/README.md#board-manager-integration)
准备组件中的 `esp32_s3_lr1121_epaper_2_9` 板型，再生成板级代码。

在已加载 ESP-IDF 环境的 Ubuntu Bash 中，以 LED 示例为例：

```bash
cd examples/esp-idf/01_ws2812_led
python -m pip install esp-bmgr-assist==0.8.3
idf.py bmgr -l
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

将串口替换为实际设备端口，按 `Ctrl+]` 退出监视器。使用 Windows 时，在
ESP-IDF PowerShell 中执行相同的构建命令，串口使用 `COM5` 等设备端口名。

## 示例

| 编号 | ESP-IDF | Arduino | 功能 |
| --- | --- | --- | --- |
| 01 | `01_ws2812_led` | `01_ws2812_led` | 板载 WS2812 |
| 02 | `02_i2c_scan` | `02_i2c_scan` | I2C 设备扫描 |
| 03 | `03_epaper_2_9` | `03_epaper` | 2.9 英寸电子纸 |
| 04 | `04_bat_axp2602` | `04_bat_axp2602` | AXP2602 电量计 |
| 05 | `05_rtc_pcf85063` | `05_rtc_pcf85063` | PCF85063A RTC |
| 06 | `06_lr1121_pingpong` | `06_lr1121_pingpong` | LR1121 Ping-Pong |
| 07 | `07_sd_card` | `07_sd_card` | microSD 1-bit SDMMC 读写测试与墨水屏状态显示 |
| 08 | `08_deep_sleep` | `08_deep_sleep` | 深度睡眠 |
| 09 | `09_comprehensive_demo` | `09_comprehensive_demo` | 综合示例 |
| 10 | `10_lr1121_lorawan` | `10_lr1121_lorawan` | LoRaWAN Class A OTAA |
| 11 | `11_LoRaWAN_ABP` | `11_LoRaWAN_ABP` | LoRaWAN Class A ABP |
| 12 | `12_LoRaWAN_Class_C` | `12_LoRaWAN_Class_C` | LoRaWAN Class C |
| 13 | `13_LoRaWAN_Multicast` | `13_LoRaWAN_Multicast` | LoRaWAN 组播 |

运行 LoRaWAN 示例前，请在配置头文件中填写自己的网络凭据，并按部署环境
设置区域、频率计划、天线和发射功率。

## 使用要点

- SD 测试前插入 FAT16/FAT32 卡。07 不格式化卡、不覆盖同名测试文件，
  读回校验后清理测试文件并卸载，在电子纸上显示 `SD PASS` 或 `SD FAIL`。
- 电量测试前连接电池。I2C 扫描的设备地址为 RTC 0x51 和电量计 0x62。
- 无线测试前连接匹配天线。Ping-Pong 使用两块板，参数保持一致。
- 综合示例中 GPIO1 单击发送 LoRa 帧，GPIO0 双击休眠，深睡时单击唤醒。
- LoRaWAN 示例按各自 README 配置区域、频率计划及凭据。密钥仅保存在
  本地配置文件中，日志和截图不包含真实密钥。

### 总线共享注意事项

电子纸和 LR1121 共用 SCK/MOSI/MISO。显示刷新时间较长，无线接收窗口具有
严格时序，因此应用应：

- 使用独立 CS，空闲时保持片选无效；
- 在射频操作完成后再刷新电子纸；
- 不在 RadioLib RX 窗口或回调中执行显示刷新、NVS 写入等耗时操作；
- 多任务场景同时使用 SPI 互斥和“无线对象单一所有者”策略。

## 低功耗操作

08 示例在完成显示后让 LR1121、AXP2602 和电子纸休眠，隔离无线引脚，
拉低 GPIO38 并保持共享供电关闭，再进入 ESP32-S3 Deep-sleep。
Arduino 08 默认 600 秒定时唤醒，也支持 BOOT(GPIO0) 唤醒。
09 综合示例包含 Wi-Fi/BLE、SD 和任务资源的有序释放流程。

测量电流时记录固件版本、供电入口和电压、仪表、测量点、外设状态及
唤醒周期。断开 USB 串口后等待休眠稳定，分别记录平均电流和唤醒峰值。

## 常见问题

### 编译时找不到 Board Manager 生成组件怎么办？

在所选示例目录完成板型准备，并执行
`idf.py bmgr -b esp32_s3_lr1121_epaper_2_9` 生成 `components/gen_bmgr_codes`。
下载组件和生成代码保留在本地，不提交到仓库。

### 电子纸没有刷新或 BUSY 超时怎么办？

检查 GPIO38 供电、BUSY GPIO8、RST GPIO9、DC GPIO17、CS GPIO18，以及
共享 SPI 的占用情况。BUSY 为高电平时表示忙。

### I2C 扫描不到电量计怎么办？

连接电池，检查 SDA GPIO13、SCL GPIO14 和供电，重新运行 02 示例。

### OTAA 一直无法入网怎么办？

检查区域、网关频率计划、JoinEUI/DevEUI 字节序、AppKey/NwkKey、MAC 版本和
覆盖情况。查看网络服务器日志，但不要把密钥贴到工单或串口日志。

### ABP/OTAA 出现帧计数器拒绝怎么办？

不要在每次启动时擦除 Preferences/NVS。只有在设备端和服务器端同时规划
计数器重置时才清除会话状态。

## 资料入口

- [Arduino 示例](../examples/arduino/README.md)
- [ESP-IDF 示例](../examples/esp-idf/README.md)
- [硬件说明与引脚](hardware-facts.zh-CN.md)
- [产品原理图](../hardware/schematics/ESP32-S3-LR1121-ePaper-2.9-XF-Schematics.pdf)
- [固件打包与烧录](firmware.md)
- [技术支持](../SUPPORT.md)
