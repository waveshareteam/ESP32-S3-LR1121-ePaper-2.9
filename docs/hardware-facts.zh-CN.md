# ESP32-S3-LR1121-ePaper-2.9 硬件说明

硬件版本：1.1。电路连接见
[Rev1.1 原理图](../hardware/schematics/ESP32-S3-LR1121-ePaper-2.9-XF-Schematics.pdf)。

## 板载资源

| 项目 | 说明 |
| --- | --- |
| 主控模组 | ESP32-S3-WROOM-1-N16R8，双核 Xtensa LX7 |
| Flash | 16 MB |
| PSRAM | 8 MB OPI PSRAM |
| 无线连接 | 2.4 GHz Wi-Fi、Bluetooth LE |
| 电子纸 | 2.9 英寸，296 × 128 像素，黑白显示 |
| LoRa 射频芯片 | Semtech LR1121 |
| RTC | PCF85063A，I2C 地址 0x51 |
| 电量计 | AXP2602，I2C 地址 0x62 |
| 外部存储 | microSD 卡槽，1-bit SDMMC |
| 状态灯 | 1 颗 WS2812 |
| 按键 | BOOT、KEY、RESET |
| USB | USB Type-C，供电、下载和串口调试 |

## 引脚与总线

| 功能 | GPIO / 信号 |
| --- | --- |
| 共享外设电源 | GPIO38，高电平有效 |
| 共享 SPI | SCK=40、MOSI=45、MISO=46 |
| 电子纸 | BUSY=8、RST=9、DC=17、CS=18 |
| LR1121 | DIO1=6、NSS=42、RST=39、BUSY=41 |
| I2C | SDA=13、SCL=14 |
| RTC 中断 | GPIO7 |
| 充电状态 | GPIO47 |
| 电量计中断 | GPIO48 |
| microSD | CMD=10、D0=11、CLK=12 |
| WS2812 | GPIO21 |
| BOOT / KEY | GPIO0 / GPIO1 |
| RESET | EN，低电平复位 |

电子纸与 LR1121 共用 SPI，使用独立片选。GPIO38 控制电子纸、LR1121、
WS2812 和 microSD 所在的共享外设电源域。显示与无线操作按顺序访问总线。

## 显示与存储

电子纸横屏显示尺寸为 296 × 128。显示示例使用全刷和 FAST 全屏刷新，
首次显示使用全刷，连续快刷后再次全刷以清理残影。刷新完成后，屏幕在
控制器休眠和共享电源关闭后保留画面。

07 示例使用 FAT16/FAT32 microSD 卡，执行挂载、写入、读回校验、清理和
卸载，并在电子纸上显示结果。ESP-IDF 示例默认启用 FATFS 长文件名支持。

## 电量与低功耗

AXP2602 提供电池电压、电流、SOC 和温度数据；充电状态通过 GPIO47 读取。
电池测试前连接电池，按原理图和接口标识接线。

08 深睡示例在进入 ESP32-S3 Deep-sleep 前让外设休眠、隔离 LR1121 信号脚、
关闭并保持 GPIO38 共享供电，支持定时器和 BOOT 按键唤醒。09 综合示例还
负责停止 Wi-Fi/BLE、卸载 SD 卡和释放任务资源。

## 无线配置

LoRa 和 LoRaWAN 参数在各示例配置文件中设置，包括区域、频率计划、
发射功率和网络凭据。运行无线示例前连接匹配天线，并按部署地区设置参数。
