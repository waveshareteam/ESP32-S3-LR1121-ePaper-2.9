# LR1121 LoRaWAN ABP Class A 示例

本示例面向 ESP32-S3 + LR1121 + 2.9 英寸电子纸，使用 RadioLib 7.7.1 实现 LoRaWAN Class A ABP（Activation By Personalization，个性化激活）。设备和网络服务器预先配置 DevAddr 与会话密钥；启动后直接激活或恢复 ABP 会话，不发送 OTAA Join Request，也不等待 Join Accept。

> 当前默认配置延续已实板验证的 LR1121 引脚、8 MHz SPI3、EU868 单信道和射频参数，适合与同样固定在 868.1 MHz / DR3 的网关和网络服务器联调。电子纸驱动与布局沿用本仓库第 10 号电子纸 LoRaWAN 示例。它不是公共 LoRaWAN 网络的通用多信道配置。部署前必须确认当地频谱、占空比和发射功率法规。

## ABP 与 OTAA

| 项目 | ABP | OTAA |
| --- | --- | --- |
| 启动流程 | 本地载入 DevAddr 和会话密钥后直接激活/恢复会话 | 发送 Join Request，收到 Join Accept 后派生会话密钥 |
| 首次发包 | 无需等待 join，启动较快 | 必须先完成空中激活 |
| 凭据 | 静态 DevAddr 与会话密钥 | JoinEUI、DevEUI 和根密钥 |
| 密钥轮换 | 需要设备与服务器同步重新配置 | 可在重新 join 时产生新会话密钥 |
| 迁移 | 迁移服务器时必须同步地址、密钥和计数器状态 | 通常重新注册并 join |
| 安全性 | 长期静态会话材料，泄露和重放风险更高 | 会话密钥生命周期更容易控制 |

ABP 的优点是启动快、不依赖 join 覆盖和 Join Accept；限制是静态 DevAddr/会话密钥难以轮换，设备迁移和计数器同步更复杂，长期安全性通常弱于 OTAA。日志应使用“ABP 会话激活/恢复”，不能把 ABP 描述为“入网”。

## LoRaWAN 版本与 RadioLib 密钥映射

示例默认选择 LoRaWAN 1.0.x：

```c
#define LORAWAN_ABP_MAC_VERSION LORAWAN_ABP_MAC_VERSION_1_0
```

RadioLib 7.7.1 的 ABP 入口为：

```cpp
beginABP(devAddr, fNwkSIntKey, sNwkSIntKey, nwkSEncKey, appSKey)
```

网络服务器字段与本示例宏的对应关系如下。

### LoRaWAN 1.0.x（默认）

| 服务器字段 | 配置宏 | RadioLib 参数 |
| --- | --- | --- |
| DevAddr | `LORAWAN_ABP_DEV_ADDR` | `devAddr` |
| AppSKey | `LORAWAN_ABP_APP_S_KEY_BYTES` | `appSKey` |
| NwkSKey | `LORAWAN_ABP_NWK_S_KEY_BYTES` | `nwkSEncKey` |
| FNwkSIntKey | 不使用 | `NULL` |
| SNwkSIntKey | 不使用 | `NULL` |

在 1.0.x 模式中，RadioLib 将单一 `NwkSKey` 传入 `nwkSEncKey` 参数；两个 1.1 完整性密钥参数必须为 `NULL`。这不是把一个密钥盲目重复到三个角色。

### LoRaWAN 1.1（可选）

将 `LORAWAN_ABP_MAC_VERSION` 改为 `LORAWAN_ABP_MAC_VERSION_1_1`，并使用服务器提供的四个独立会话密钥：

| 服务器字段 | 配置宏 | RadioLib 参数 |
| --- | --- | --- |
| DevAddr | `LORAWAN_ABP_DEV_ADDR` | `devAddr` |
| AppSKey | `LORAWAN_ABP_APP_S_KEY_BYTES` | `appSKey` |
| FNwkSIntKey | `LORAWAN_ABP_F_NWK_S_INT_KEY_BYTES` | `fNwkSIntKey` |
| SNwkSIntKey | `LORAWAN_ABP_S_NWK_S_INT_KEY_BYTES` | `sNwkSIntKey` |
| NwkSEncKey | `LORAWAN_ABP_NWK_S_ENC_KEY_BYTES` | `nwkSEncKey` |

不要为了凑齐参数而把同一网络密钥复制到多个 1.1 角色。设备选择的 MAC 版本、密钥集合和网络服务器设备配置必须完全一致。

## 配置凭据

`main/abp_config.h` 内置全零占位值，因此没有凭据也可以完成依赖解析和编译。实际凭据应放在本地的 `main/abp_credentials.h`；该文件已被 `.gitignore` 排除。可从 `main/abp_credentials.h.example` 建立本地配置，然后只填写服务器分配给本设备的值。

需要配置的宏为：

- `LORAWAN_ABP_DEV_ADDR`
- `LORAWAN_ABP_APP_S_KEY_BYTES`
- `LORAWAN_ABP_NWK_S_KEY_BYTES`（LoRaWAN 1.0.x）
- `LORAWAN_ABP_F_NWK_S_INT_KEY_BYTES`（LoRaWAN 1.1）
- `LORAWAN_ABP_S_NWK_S_INT_KEY_BYTES`（LoRaWAN 1.1）
- `LORAWAN_ABP_NWK_S_ENC_KEY_BYTES`（LoRaWAN 1.1）

启动时会检查 DevAddr 和当前 MAC 版本需要的全部密钥。任一值仍为占位或全零时：

- 不初始化可发射的 ABP 流程，不进行无线发送；
- 串口提示需要配置凭据；
- 电子纸显示 `CONFIG REQUIRED`；
- 不打印任何密钥内容。

DevAddr 不是密钥，可以用 8 位十六进制显示在串口和电子纸上。

## 网络和发送参数

以下宏集中在 `main/abp_config.h`：

| 配置项 | 默认值 | 宏 |
| --- | --- | --- |
| 区域 | EU868 | `LORAWAN_ABP_REGION_OBJECT` / `LORAWAN_ABP_REGION_NAME` |
| 子频段 | 0 | `LORAWAN_ABP_SUB_BAND` |
| 单信道上行 | 868.1 MHz | `LORAWAN_ABP_SINGLE_CHANNEL_FREQUENCY_HZ` |
| 数据率 | DR3 | `LORAWAN_ABP_SINGLE_CHANNEL_DR` |
| 发射功率 | 16 dBm | `LORAWAN_ABP_TX_POWER_DBM` |
| ADR | 关闭 | `LORAWAN_ABP_ADR_ENABLED` |
| 应用上行端口 | 1 | `LORAWAN_ABP_UPLINK_PORT` |
| 周期 | 60 秒 | `LORAWAN_ABP_UPLINK_INTERVAL_SECONDS` |
| 上行确认 | unconfirmed | `LORAWAN_ABP_CONFIRMED_UPLINK` |

当前单信道模式强制关闭 ADR，避免网络下发的数据率或信道调整破坏固定链路。RX1 按本次上行参数和区域规则打开；RX2 不覆盖 RadioLib 的 EU868 默认值，即 869.525 MHz / DR0。服务器和网关必须使用相同的区域、信道、数据率、RX 参数和帧计数策略。若改用标准多信道网络，应同步修改设备与服务器配置，而不是只改频率宏。

## Class A 工作流程

1. 初始化 NVS、板级电源、电子纸和 LR1121。
2. 验证占位凭据；未配置时停在安全提示状态。
3. 从 NVS 恢复经版本、长度和 CRC 校验的最新 ABP 会话记录。
4. 使用 `beginABP()` 配置 DevAddr 与对应版本的会话密钥，再调用 `activateABP()` 激活或恢复会话；不会执行 OTAA join。
5. 周期性发送包含递增应用序号、运行时间和基本状态的上行，可通过宏选择 confirmed 或 unconfirmed。
6. 每次上行后由 RadioLib 按 Class A 时序等待 RX1 和 RX2。
7. 收到下行时输出端口、长度、HEX、安全文本、RSSI、SNR 和接收窗口；不可打印的文本字节会被转义或替换。
8. unconfirmed 上行无下行是正常结果；confirmed 上行只有在下行事件的 `confirming` 标志置位时才算收到 ACK。未收到 ACK 时串口和电子纸显示 `TX NO ACK` 或 `RX NO ACK`，并采用有上限的指数退避。射频、网络和 NVS 错误同样有限退避，状态日志和电子纸刷新均限速，不会高速重试刷屏。发送间隔使用与第 12 号一致的 FreeRTOS 任务延时，不额外改变 LR1121 的休眠/唤醒状态。

## 帧计数器与 NVS 持久化

ABP 没有 OTAA 重新 join 来自动建立新会话。网络服务器会拒绝回退的 `FCntUp`，并使用下行计数器抵御重放；如果设备掉电后从旧计数继续发送，服务器可能长期拒包。因此帧计数器持久化是 ABP 正确性和安全性的一部分，不是可选缓存。

本示例持久化 RadioLib 的 nonces 和 session 两个固定布局缓冲区，包括其中的上行/下行计数和 RX 参数，并采用以下保护：

- 记录包含格式版本、头部/结构长度、RadioLib 版本、DevAddr、MAC 版本、配置指纹、递增代次、状态标志和 CRC；任一身份、版本、长度或校验不匹配都不会恢复。
- NVS 使用 A/B 双槽交替提交；正常运行时两槽代次必须连续且轮换关系正确。只有首次提交后的 `A=1、B=未建立` 状态允许单槽恢复；任一槽损坏、投入使用后缺槽或代次关系异常都会停止发送，绝不回退到可能较旧的计数快照。
- 发送前先提交带“无线事务未完成”标志和已预留下一 `FCntUp` 的会话快照；只有预留成功才允许发射。
- `sendReceive()` 每次返回后保存 RadioLib 的最新会话状态，包括适用的网络/应用下行计数器。
- NVS 写入失败时停止本次无线发送并退避，不能用未持久化的旧计数器继续发包。

ABP 激活本身不使用 OTAA JoinNonce/DevNonce；本例仍完整保存 RadioLib 的 nonces 缓冲区，使持久化格式与当前库状态保持一致，它不作为 ABP 根凭据使用。若记录不可恢复，设备进入错误状态，不应把清空 NVS 当作普通重试。

RadioLib 7.7.1 在下行解析完成到 `sendReceive()` 返回之间没有公开持久化回调，因此不能在该窗口内原子提交新的下行计数。为避免静默回退，本示例采用故障关闭：如果设备在 TX/RX1/RX2 到最终 NVS 提交之间复位或掉电，下次启动会检测到“无线事务未完成”标志，显示 `SESSION AMBIGUOUS` 并禁止发射。此时必须先在网络服务器侧协调计数器或更换会话材料，再按下文受控流程清除本地状态；不能直接继续旧会话。

RadioLib 的 session 缓冲区不仅包含帧计数器和 RX 参数，也包含 DevAddr 与会话密钥，因此本示例写入 NVS 的会话记录属于敏感数据。生产设备除了 Flash Encryption 和 Secure Boot，还应评估并启用 ESP-IDF NVS Encryption；仅有记录 CRC 只能发现损坏，不能提供保密性或防篡改认证。

## 受控清除 ABP 状态

`LORAWAN_ABP_CLEAR_STATE_CONFIRM` 默认是 0。只有临时把它设置为 `LORAWAN_ABP_CLEAR_STATE_MAGIC`，重新编译并启动一次，程序才会清除本示例自己的 ABP NVS 命名空间，并在无线初始化前停止。

正确顺序是：

1. 停止设备发包，并在网络服务器侧记录或重置对应设备的帧计数状态；更安全的做法是分配新的 DevAddr/会话密钥。
2. 启用清除确认值，烧录并启动一次，确认设备只清除本地 ABP 状态且没有发射。
3. 将确认宏恢复为 0，配置与服务器一致的新会话状态，再重新编译和烧录。
4. 重新验证首次上行计数和服务器接收结果。

只清除设备 NVS 而不协调服务器会造成计数器回退、拒包或重放风险。整片 Flash 擦除、擦除 NVS 分区或更换模组也有相同风险；固件无法区分“首次部署”和“旧会话状态被擦除”。不要把清除会话作为网络故障、无下行或普通重试的处理手段。

## 电子纸和串口状态

电子纸沿用项目的 Terminus Bold 8x14 字体、首帧全刷和最多 10 次快刷后再全刷的策略。界面显示：

- `ABP`、`ACTIVE`、`CONFIG REQUIRED` 或 `ERROR`；
- DevAddr；
- `FCntUp`、网络/应用下行计数和应用发送序号；
- 最近一次 TX/RX 的端口、长度和 RX1/RX2 窗口；
- RSSI、SNR 和关键错误码。

界面和串口都不会显示会话密钥。动态行会整屏重绘，避免旧字段残留。

## 构建、烧录和监视

先按[板型准备说明](../README.md#board-manager-integration)下载组件，并在组件尚未发布此板型期间手动补入对应板型目录。Board Manager 由 `waveshare/waveshare_boards` 间接提供，本仓库不保存板型副本。

本示例不提交 `dependencies.lock`、下载组件或 `components/gen_bmgr_codes`；Component Manager 和 Board Manager 在本机重新生成所需文件。

Windows PowerShell 或已加载 ESP-IDF 环境的 Ubuntu Bash 均可在本示例目录执行：

```text
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

将 `PORT` 替换为设备串口。退出监视器使用 `Ctrl+]`。占位凭据不会阻止编译，只会在运行时阻止无线发送。

配置正确后的预期行为是看到“ABP 会话激活”或“ABP 会话恢复”，随后按周期上行；不会出现 Join Request/Join Accept。unconfirmed 上行无下行时仍会完成 RX1/RX2 等待并进入下一发送周期；confirmed 上行未收到带 ACK 位的下行时会报告 `NO ACK` 并退避。

## 安全注意事项

会话密钥即使放在被忽略的本地头文件中，仍会以明文形式进入固件镜像和设备 Flash。`.gitignore` 只能防止误提交，不能保护编译产物、烧录工具、Flash 读取或备份文件。生产设备应评估安全的凭据注入流程、Flash Encryption、Secure Boot、构建产物访问控制和密钥轮换机制；一旦固件或密钥可能泄露，应在服务器和设备两侧同步更换会话材料。

不要把密钥写入日志、电子纸、问题报告、截图或公开构建产物。始终连接合适的天线后再允许 LR1121 发射。

## 验证状态与实测步骤

- 设备烧录、启动和电子纸刷新：`PASS`（用户实板日志，2026-08-19）
- ChirpStack ABP 会话激活与应用下行：`PASS`
- Class A unconfirmed 上行：`PASS`
- RX1 应用下行及 HEX/安全文本解析：`PASS`
- USB-UART 复位后的 NVS/session 恢复：`PASS`
- USB-UART 复位后的 `FCntUp` 连续性：`PASS`（复位前 0..3，复位后从 4 继续）
- RX2 下行：`NOT_TESTED`
- confirmed 上行 ACK：`NOT_TESTED`
- 真实断电后的上下行计数连续性：`NOT_TESTED`
- 复位后的下行计数连续性：`NOT_TESTED`
- 无线事务中途掉电后的故障关闭：`NOT_TESTED`

剩余实测应分别安排 RX2 和 confirmed 上行，再在发送间隔内多次真实断电，确认 `FCntUp` 及下行计数器始终前进且 ChirpStack 未报告 replay 或 counter rollback。最后在 TX/RX 窗口中切断电源，确认下次启动停在 `SESSION AMBIGUOUS` 且不再发射。
