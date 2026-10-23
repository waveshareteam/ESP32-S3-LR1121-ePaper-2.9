# ESP32-S3-LR1121-ePaper-2.9-XF 综合测试

本示例是板卡的全功能、持续满负载和低功耗验证程序。运行态保持
ESP32-S3 双核 240 MHz、Wi-Fi APSTA、BLE 被动扫描、LR1121 持续接收、
双核计算与内存压力；只有 GPIO0 双击被确认后，才按依赖顺序停止任务并
进入 Deep-sleep。

## 原理图审计

| 功能 | 原理图连接 | 电源/共享关系 | 本示例测试 |
| --- | --- | --- | --- |
| GPIO0 按键 | GPIO0，低有效，外部 10K 上拉 | strapping/下载脚，也是 RTC IO | 清醒时双击休眠；Deep-sleep 时单击 EXT1 唤醒 |
| GPIO1 按键 | GPIO1，低有效，外部 10K 上拉 | 普通数字 GPIO | 消抖后每次稳定按下只触发一帧 LoRa TX |
| LR1121 | DIO1=6、NSS=42、RESET=39、BUSY=41 | 与电子纸共用 SCK=40、MOSI=45、MISO=46；LoRa_3V3 由 GPIO38 控制的 3V3 域经 R42 (0R) 供电，R43 (ESP_3V3) 未装 | 初始化、持续 RX、按键 TX、RSSI/SNR、错误计数、Sleep；断电后全部 ESP32 连接脚高阻隔离 |
| 2.9 寸电子纸 | BUSY=8、RESET=9、DC=17、CS=18；原理图功能块标题仍写作“2.13inch e-Paper” | GPIO38 经 Q3/Q1 控制共享 3V3 外设域；与 LR1121 共用 SPI | 按项目目标使用 SSD16xx 2.9 寸配置；首次/唤醒/关键状态全刷，运行状态使用快刷且每 10 次快刷插入一次全刷；Sleep、释放 framebuffer/LVGL；面板型号和波形需实物确认 |
| WS2812 | GPIO21 | 由 GPIO38 控制的共享 3V3 域供电 | 红/绿/蓝测试后保持板级限定白光负载；休眠前关闭并随共享电源域断电 |
| PCF85063A | I2C 0x51，INT=GPIO7 | I2C SDA=13、SCL=14；VDD 由 3V3/V_BAT 二极管或供电 | 时间无效或 VL 置位时写入固件编译时间并读回校验，已有有效时间保留；读取 INT 和 Control_2；休眠前停 1 Hz 定时器并清 IRQ |
| AXP2602 | I2C 0x62，BAT_STAT=47、IRQ=48 | 无板级负载开关 | 电压/电流/SOC/温度、充电脚、IRQ 事件和 Sleep；保留句柄时 Wake，深睡复位后 init 清 Sleep 并恢复 Normal |
| microSD | CMD=10、D0=11、CLK=12 | SDMMC 1-bit；由 GPIO38 控制的共享 3V3 域供电 | 不格式化、不写文件；挂载后只读前 1 MiB 并计算 CRC/吞吐，休眠前卸载并断电 |
| USB Type-C | D-=GPIO19、D+=GPIO20，经 ESD/共模器件 | VBUS 同时进入板上供电/充电路径 | USB Serial/JTAG 控制台已配置；枚举、线缆和信号完整性需实物确认 |
| EN 复位键 | EN，低有效，外部上拉和去抖电容 | 按下会复位整个 SoC | 不在自动流程中自触发；人工复位行为 NOT_TESTED |
| VBUS/VBAT/3.3 V | 独立充电管理、VBAT 电量计、ESP_3V3 稳压与 Q3/Q1 共享外设电源路径 | GPIO38 可关闭 3V3 外设域，但不能关闭 ESP_3V3 主域、按键上拉和电池备份域 | AXP2602/BAT_STAT 可间接观测；充电、稳压和轨电流需电源仪器验证 |
| ESP32-S3 | 双核、N16R8 | 16 MB Flash、8 MB Octal PSRAM | 芯片信息、双核压力、RAM/PSRAM、全 Flash 只读、RNG/PSA AES/SHA/定时器 |

改造前目标示例的有效覆盖集中在板载外设 smoke test；本次新增了 ESP32-S3
内部自检与持续负载、APSTA/BLE、SD 只读测试、事件驱动按键/LoRa、可重入状态机
以及有序关断和完整唤醒重建。

GPIO38 通过 Q3/Q1 控制共享 3V3 外设域。LR1121 的 R42 为 0R、R43 未装，
因此 LoRa_3V3 实际随该电源域关闭；SD、WS2812 和电子纸也使用此 3V3 域。
GPIO38 拉低后，任何仍输出高电平的 LR1121 引脚都会形成反向供电路径，所以
Deep-sleep 前必须把 DIO1、NSS、RESET、BUSY 和全部 SPI 引脚设为高阻并保持。
PCF85063A 的 V_BAT 备份路径以及 AXP2602/稳压器的静态功耗仍构成实测下限。
原理图中的 Q4 (AO3401) 属于 VBUS 到 LDO_IN 的自动电源路径，不是 LR1121
电源开关。

RTC 自动初始化使用固件中记录的编译日期和时间（编译主机本地时间），仅作为
出厂测试初始值，不等于烧录时的准确时间。需要启用 `CONFIG_APP_COMPILE_TIME_DATE`。
写入、读回、日期校验或 VL 清除失败仍判为 FAIL；有效时间在复位和唤醒后不重写。

## Wi-Fi 配置

凭据和射频参数集中在 **main/app_config.h**。不要提交真实凭据，只需修改：

~~~c
#define TEST_WIFI_STA_SSID       ""
#define TEST_WIFI_STA_PASSWORD   ""
#define TEST_WIFI_AP_SSID        "LR1121-S3-TEST"
#define TEST_WIFI_AP_PASSWORD    "lr1121test"
~~~

SoftAP 始终启动。STA 宏为空或仍是占位值时，日志显示
STA NOT_CONFIGURED，不影响 SoftAP；填写后自动连接并在断线时重连。日志
输出 IP、网关、RSSI、SoftAP 客户端 MAC/AID，但绝不输出密码。Wi-Fi 省电在
ACTIVE 状态关闭，以维持 APSTA 性能和负载。这里验证的是接口、连接和重连
生命周期，没有内置网络流量发生器，因此真实 Wi-Fi 吞吐为 NOT_TESTED。

## LoRa 配置与操作

默认参数沿用本项目现有 LR1121 示例：

- 868.0 MHz，125 kHz，SF7，CR 4/5；
- sync word 0x12，preamble 8；
- 22 dBm，TCXO 3.0 V。

868 MHz 和 22 dBm 不保证适用于所有国家、地区或子频段。烧录前必须按当地
法规、天线和实际硬件限制修改 **main/app_config.h**。程序上电不会自动发射；
只有 GPIO1 一次稳定按下才发送一帧：

~~~text
id=S3-LR1121-XF seq=<递增序号> ms=<启动后毫秒> crc=<CRC32>
~~~

TX 成功、超时或失败后都会立即恢复 RX。接收日志包含 payload、RSSI、SNR。
RadioLib 7.7.1 的 LR11x0 getFrequencyError() 尚未实现，因此频偏明确显示
NOT_AVAILABLE，不会伪造测量值。没有第二台同参数设备时，LoRa 空口收发为
NOT_TESTED。

## 状态机

~~~text
ACTIVE_SELF_TEST -> ACTIVE_STRESS -> LORA_RX
                                     | GPIO1
                                     v
                                  LORA_TX -> LORA_RX
                                     |
                         GPIO0 双击  v
                               PREPARE_SLEEP -> LOW_POWER
                                      GPIO0 单击
                                           v
                                      WAKE_REINIT
                                           |
                                           +-> ACTIVE_SELF_TEST
~~~

GPIO0 单击在清醒时只打开双击窗口，超时后不执行动作。进入睡眠前必须检测到
GPIO0/GPIO1 稳定释放，避免刚睡下就立即唤醒。GPIO0 是 strapping 脚，上电或
复位期间保持按下可能进入下载模式；程序只把它配置为输入，不改变硬件启动
假设。

## 满负载内容

- 上电和唤醒后先开启共享外设电源，执行红/绿/蓝灯测试并保持白灯；随后
  显示 `SELF TEST / Testing hardware...`。耗时自检由独立任务执行，与启动页
  刷新并行；外设检测按顺序进行，通过现有锁协调共享 SPI 和屏幕访问。
  完成后显示 `BOARD TEST` 结果页，刷新完成后停留 2 秒，再启动无线、按键
  和持续压力测试，进入运行页。若后台任务创建失败，会记录警告并串行完成自检。
- 两个固定核心任务持续执行可重复校验的整数、FPU、PSA SHA-256、内部 RAM 和
  PSRAM 工作负载，记录摘要、错误、迭代、带宽和栈最低余量。
- 每轮定期让出一个 tick，保留 CPU0/CPU1 Idle Task、任务看门狗和中断看门狗
  的运行机会；不会超频、关闭棕断或关闭看门狗。
- 启动自检完整只读 16 MB Flash 并记录 CRC/吞吐，不擦除或写入任何分区。
- Wi-Fi APSTA 禁用 power save，BLE Observer 持续被动扫描，LR1121 默认持续
  RX；这三项与双核计算共同形成正常运行负载。
- UART 每 30 秒输出一次状态；电子纸默认每 5 分钟刷新运行状态。LoRa 接收
  计数变化也会触发运行页刷新，连续收包合并为最新计数，两次接收触发的刷新
  之间至少间隔 2 秒（从上一次刷新结束计时）；发送完成后也会刷新。首次启动、
  唤醒、自检总结和休眠提示使用全刷，普通状态刷新调用 SSD16xx 驱动的
  `EPD_UPDATE_FAST` 全屏快刷；连续 10 次快刷后自动插入一次全刷清理残影。
- UI 使用 Terminus Bold 8x14 的 ASCII 1-bpp 点阵字模，不使用灰阶抗锯齿，
  以提高黑白墨水屏上的笔画锐度。生成后的字模和 SIL OFL 1.1 许可位于
  根目录公共组件 `components/epaper_ui_font/`；示例 3 至 8 复用这份唯一
  源码和 SIL OFL 1.1 许可。当前 UI 文本限定为 ASCII。

## 休眠与恢复顺序

PREPARE_SLEEP 依次执行并记录 PASS/FAIL：

1. 停止按键、双核压力任务，SD 同步卸载；
2. 停止电量监控、BLE、Wi-Fi APSTA、LR1121 服务和 NVS；
3. LR1121 停止收发、清 IRQ 并进入 Sleep；
4. 电子纸控制器先进入 Sleep，但此时保持共享 3V3 域供电；
5. WS2812 关闭，RTC 1 Hz 定时器停止并清 IRQ，AXP2602 IRQ 关闭并进入
   Sleep；这些动作全部完成后，才释放电子纸 LVGL timer/framebuffer、反初始化
   面板并由 GPIO38 关闭共享 3V3；
6. 反初始化 Board Manager、I2C、SPI、RMT 和相关设备；
7. GPIO38 拉低并保持，关闭共享 3V3 外设域；电子纸、SD、WS2812 使用安全
   低电平/高阻，LR1121 的 DIO1、NSS、RESET、BUSY 和共享 SPI 全部高阻并
   逐脚 hold，防止 ESP_3V3 向断电域反向供电；
8. GPIO0 仅使用原理图已有的 10K 外部上拉，配置 EXT1 ANY_LOW，明确关闭
   RTC_PERIPH 电源域并确认 GPIO0 为高电平后进入 Deep-sleep。

在 `CONFIG_ESP_NETIF_TCPIP_LWIP` 后端，程序会销毁本示例创建的 AP/STA
`esp-netif` 实例和默认事件循环；ESP-IDF 不支持在运行时反初始化全局 lwIP
TCP/IP 核心，因此该限制会被明确记录，但不判为关断失败。Deep-sleep 随后会
关闭数字域，不需要因此降级到 Light-sleep。

任一关键停止、释放、按键或唤醒配置步骤失败都会取消睡眠并进入
WAKE_REINIT。唤醒后释放 GPIO/RTC hold，报告 wake bitmap 和 EXT1 GPIO
mask，等待 GPIO0 稳定释放，然后重新执行全部自检、外设初始化、APSTA、BLE、
压力任务和 LoRa RX。

## 构建与烧录

本示例兼容 ESP-IDF 5.5 与 6.2，目标为 ESP32-S3 N16R8。
手册优先使用 5.5 稳定版；6.2 使用固定的开发版提交，详见
[版本兼容说明](../../../docs/idf-compatibility.md)。

先按[板型准备说明](../README.md#board-manager-integration)下载组件并补入板型。
Ubuntu Bash（已加载所选 ESP-IDF 环境）：

~~~bash
python -m pip install esp-bmgr-assist==0.8.3
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
~~~

源码级无硬件检查不会编译项目：

~~~bash
python static_contract_check.py
~~~

完成构建后，可在已加载 ESP-IDF 环境的 Ubuntu Bash 中运行 BLE 队列生命周期
回归测试（需要主机 C 编译器，不连接板卡）：

~~~bash
python tests/test_nimble_observer_deinit.py -v
~~~

## 预期日志与判定

验证使用的 ESP-IDF 5.5 和 6.2 SDK 在 BLE 仅 Observer 模式下，退出时会释放未初始化的
RX 数据队列，导致 `ble_mqueue_deinit / npl_eventq_queued_get_task` 空指针崩溃。
工程通过 `cmake/nimble_observer_deinit.cmake` 在构建目录生成带条件保护的
`ble_hs.c`，使释放条件与初始化一致；不修改已安装 SDK，仍执行完整的
`nimble_port_stop()` 和 `nimble_port_deinit()`。SDK 若已包含相同保护则不再应用；
源码结构变化时构建会提示重新审查此兼容修复。

休眠复测应先看到 `BLE stop/deinit: PASS`，随后
`entering deep sleep: GPIO0 RTC=HIGH, EXT1 ANY_LOW only`，并在松开按键后保持
休眠；单击 GPIO0 后应出现 `DEEP_SLEEP_WAKE` 和 `ext1_gpio_mask=0x0000000000000001`。
`SLEEP_CANCELLED` 表示取消休眠后恢复，`USB_RESET` 表示 USB 引发复位；
`Guru Meditation` 表示崩溃，均不能按正常 GPIO0 唤醒判定。

串口会输出 SELF_TEST 表、state= 状态切换、双核压力摘要、Wi-Fi/BLE 状态、
LoRa TX/RX 计数和每个 PREPARE_SLEEP 步骤。状态含义：

- PASS：本次运行得到可校验结果；
- FAIL：接口、数据校验或释放步骤失败；
- NOT_TESTED：缺少 SD 卡、LoRa 对端、STA 凭据或测量条件。

仅编译或源码检查通过不能证明硬件功能通过。没有实体板、LoRa 对端、Wi-Fi
凭据和功耗仪时，开机自检、射频、按键、Deep-sleep 唤醒以及正常/低功耗电流
均应保持 NOT_TESTED。复测时依次记录：启动日志与堆/栈余量、SoftAP 客户端、
STA RSSI、两板 LoRa 双向收发、GPIO0 双击休眠和单击唤醒、唤醒后的完整恢复，
最后在电源入口分别测量 ACTIVE_STRESS 与 LOW_POWER 稳态电流。
