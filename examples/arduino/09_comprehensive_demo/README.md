# 09 综合测试（Arduino）

本示例与 ESP-IDF 09 对齐主要功能和操作：上电/唤醒自检 → `BOARD TEST`
结果页（停留 2 秒）→ `RUNTIME` 持续负载；GPIO1 每次稳定按下发送一帧 LoRa，
GPIO0 双击进入深睡，深睡时 GPIO0 单击唤醒并重新初始化。

## 编译与配置

沿用上级 README 的 Arduino-ESP32 3.3.11、RadioLib 7.7.1，以及
`Waveshare_LR1121_EPaper` 库。选择 ESP32S3 Dev Module、16 MB Flash、
OPI PSRAM、USB CDC On Boot Enabled、Hardware CDC and JTAG。
默认分区可编译；增加其他功能导致空间不足时，可选
`16M Flash (3MB APP/9.9MB FATFS)`。

修改 `app_config.h` 配置 STA 凭据、SoftAP 和 LoRa 参数。STA 默认不配置，
SoftAP 默认开启，关闭 Wi-Fi 省电并自动重连已配置的 STA。程序不打印密码。
LoRa 默认 868 MHz / BW125 / SF7 / CR4/5 / 22 dBm，烧录前按所在地区和天线
调整；上电不会自动发射，需要按 GPIO1。

Ubuntu Bash 编译命令（在仓库根目录执行，输出保存在忽略的构建目录）：

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi \
  --libraries examples/arduino/libraries \
  --build-path .ci-build/arduino/09_comprehensive_demo \
  examples/arduino/09_comprehensive_demo
```

## 自检和运行

- LED 红/绿/蓝后保持限定亮度白光；实际发光效果由人工确认。
- RAM、PSRAM 写入/读回校验；全 Flash 只读 CRC 检查；RNG、AES/SHA-256
  已知答案测试和定时器回调检查。不擦除 Flash，不写入 SD。
- I2C 检测 RTC 和 AXP2602，读取电量、电流、温度。RTC 时间无效或 VL 置位
  时设为固件编译时间并读回确认；有效时间保留。
- SDMMC 1-bit 使用 CLK12/CMD10/D0 11，挂载 FAT 卡后读取前 1 MiB 并计算
  CRC；不格式化、不写文件，无可挂载卡显示 `N/T`。
- 两个核心保持 240 MHz，执行整数、浮点、内部 RAM、PSRAM 和 SHA/AES
  压力任务；定期让出 CPU，保留看门狗。串口报告迭代数、错误和栈余量。
- Wi-Fi APSTA、BLE 被动扫描、LoRa RX 同时运行。BLE 计数为收到的广播次数，
  包含重复广播。LoRa TX 后立即恢复 RX；串口显示 RSSI/SNR，频偏明确为
  `NOT_AVAILABLE`。没有对端设备时，空口收发不能据初始化成功判定通过。

串口默认每 30 秒报告状态，墨水屏每 5 分钟刷新；收发计数变化可提前刷新，
两次刷新之间至少间隔 2 秒。首次、结果页、唤醒和睡眠提示全刷，其余采用快刷
并按驱动上限自动插入全刷。

界面使用与 ESP-IDF 相同的 Terminus Bold 8x14 黑白字模，正文比原先的 5x7
更清楚。运行页为三行双列状态格和底部 LoRa 十六进制计数（T=成功发送，
F=发送失败/请求拒绝，R=接收成功）。2.13 寸屏使用缩写，完整日志在串口。

## 按键和休眠

按键独立消抖 40 ms，GPIO0 的双击窗口为 600 ms；单击超时没有动作。
GPIO1 长按只触发一次。启动时已按住的按键须先释放再按下。

屏幕刷新、LoRa 和按键分别运行，SPI 使用 Arduino 核心事务锁协调。
休眠时先确认两键释放，再依次停止工作任务、卸载 SD、停止 BLE/Wi-Fi、
让 LoRa 和屏幕控制器休眠、关闭 LED、关闭 RTC 定时器/中断、清除 AXP2602
中断并让其休眠，然后释放 I2C/SPI、隔离共享电源域引脚、关闭并保持 GPIO38。
最后配置 GPIO0 EXT1 ANY_LOW 唤醒，再检查释放状态；不启用定时唤醒。

若开始关断后某个关键步骤失败，串口记录错误，软件复位重建全部服务，
避免在仅部分设备已关闭时继续运行。GPIO0 是启动配置脚，复位时不要一直按住。

## 与 ESP-IDF 的实现差异和验证边界

- Arduino 使用原生屏幕渲染、Arduino 外设库和 FreeRTOS；不使用 Board Manager/LVGL。
- 启动自检顺序执行，完成后才启动运行服务；ESP-IDF 可并行刷新启动页与自检。
- 使用 Arduino 核心的 mbedTLS 完成 AES/SHA 测试；ESP-IDF 版本使用 PSA。
- LED、USB 枚举、复位键、电源电流及真实 Wi-Fi 吞吐仍需人工或仪器验证。
- 编译和主机测试不能验证射频共存、真实按键时序、面板波形或深睡电流。

本示例不再写入旧仪表盘的 NVS 启动次数。
