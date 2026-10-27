# LR1121 LoRaWAN Class C 示例

本示例面向 ESP32-S3 + LR1121 + 2.9 英寸电子纸板，演示 LoRaWAN 1.1 OTAA 会话激活后进入 Class C、持续接收服务器异步下行，并按固定周期发送上行。

## Class C 是什么

LoRaWAN 设备类别描述设备何时打开接收窗口，并不决定设备如何激活。Class C 设备在没有发送时几乎持续保持接收窗口 RXC 打开；发送以及发送后的 RX1/RX2 窗口结束后，设备立即回到 RXC。网络服务器因此可在多数时间直接下发数据，延迟通常明显低于 Class A。

持续接收的代价是 LR1121 长期处于接收状态，ESP32-S3 也必须持续运行处理无线事件，功耗显著高于 Class A。本示例正常运行时不会让 LR1121 sleep，也不会让 ESP32-S3 进入会停止接收的 Deep-sleep。若应用以后增加低功耗模式，必须先显式退出 Class C；休眠期间不能接收即时下行。

### Class A、Class B、Class C 对比

| 类别 | 接收时机 | 典型下行延迟 | 网络侧额外要求 | 功耗与适用场景 |
| --- | --- | --- | --- | --- |
| Class A | 每次上行后只打开 RX1 和 RX2 | 服务器通常要等设备下一次上行 | 基本 LoRaWAN 支持 | 最低功耗，适合电池传感器和长时间休眠设备 |
| Class B | 在 Class A 窗口之外，按网络信标同步的 Ping Slot 定时接收 | 低于 Class A，但下行仍要等待下一个时隙 | 网关和服务器必须支持信标、时间同步及 Class B 调度 | 中等功耗，适合需要可预测下行时间的设备 |
| Class C | 除发送及必要的射频切换外，几乎持续打开 RXC；上行后仍先处理 RX1/RX2，再返回 RXC | 最低，服务器可在 RXC 可用时异步下发 | 服务器必须把会话按 Class C 管理，并使用一致的 RX2/RXC 参数 | 最高功耗，适合有稳定供电的执行器、控制器、网关辅助终端和低延迟控制 |

Class C、Class A、Class B 与 OTAA、ABP 是两组不同概念：前者是设备类别，后者是激活方式。本示例没有把第 12 号改成 ABP，而是继续使用第 12 号已经成功入网的 **LoRaWAN 1.1 OTAA** 流程；只有会话成功激活或有效会话恢复后才进入 Class C。

## 硬件与软件基线

- 射频芯片：LR1121，高功率 PA 路径。
- SPI：由 Board Manager 配置的共享总线，SCK GPIO40、MISO GPIO46、MOSI GPIO45，8 MHz。
- LR1121 控制引脚：NSS GPIO42、DIO1 GPIO6、RESET GPIO39、BUSY GPIO41。
- 共享外设电源：GPIO38 保持高电平。Class C 活动期间电子纸休眠不会关闭该共享电源。
- LR1121 RF switch：沿用 DIO5/DIO6 表和第 12 号高功率路径。
- TCXO：RadioLib 初始化使用 3.0 V。
- LoRaWAN 库：`jgromes/radiolib` 7.7.1，版本在 `main/idf_component.yml` 中锁定，并由 `main/lorawan_config.h` 的版本检查保护。
- ESP-IDF：目标为 ESP32-S3，`main/idf_component.yml` 声明 `>=5.5.0,<6.3.0`；框架版本见[兼容说明](../../../docs/idf-compatibility.md)。

电路连接见 [Rev1.1 原理图](../../../hardware/schematics/ESP32-S3-LR1121-ePaper-2.9-XF-Schematics.pdf)。

## 源码结构

为便于客户阅读和后续维护，示例按职责拆分，`main.cpp` 只保留 ESP-IDF 入口：

| 文件 | 职责 |
| --- | --- |
| `main/main.cpp` | 调用应用启动接口，不包含业务状态机 |
| `main/lorawan_app.*` | 首屏状态、凭据占位检查、显示任务和唯一无线任务的启动 |
| `main/lorawan_activation.cpp` | LoRaWAN 1.1 OTAA 激活、恢复会话检查和 Class C 协商 |
| `main/lorawan_node.*` | RadioLib 7.7.1 Class C 适配层、凭据检查及网络配置 |
| `main/lorawan_runtime.*` | 公共运行状态、持续接收、上行及短周期服务循环 |
| `main/lorawan_state.cpp` | NVS/射频初始化、Class C 活动循环、退避和重入网编排 |
| `main/lorawan_persistence.*` | nonce/session 保存协调及 FCntUp 高水位预留 |
| `main/lorawan_session.*` | 独立的 NVS 存取后端 |
| `main/lorawan_ui.*` | 电子纸快照队列、刷新合并、限频和显示/无线协调 |
| `main/lorawan_config.h` | 集中的区域、端口、周期、ADR、功率和任务配置 |

源码中的英文注释重点说明公开接口、并发所有权、持久化边界和 RadioLib 时序约束；常规赋值和直观控制流不做逐行复述。

## LoRaWAN 与区域基线

默认配置保持第 12 号的网络参数：

- LoRaWAN 1.1 OTAA；
- EU868；
- 默认使用标准多信道计划；可选单信道兼容参数为 868.1 MHz、DR3；
- sub-band 0；
- ADR 关闭；
- 发射功率 16 dBm；
- 普通上行 FPort 1、周期 60 秒、默认 unconfirmed。

单信道模式是与既有测试网关兼容的受限配置，不等同于标准的多信道 EU868 网络。当前默认值已经是 0，即使用标准多信道计划；只有明确连接相同的受限测试网关时才应把 `LORAWAN_SINGLE_CHANNEL_MODE` 改为 1，并同步核对服务器和网关信道。

启用该兼容模式时，代码与第 12 号一样先复制 EU868 band，再把默认上行信道 1、2 清空并只保留配置的信道 0。fresh OTAA、session 恢复以及每次显式上行前都会重新应用单信道、DR、ADR 和功率基线，避免 JoinAccept 或后续 LinkADR 让应用上行静默偏离既有单信道网关。

## 配置

所有用户配置集中在 `main/lorawan_config.h`，凭据放在独立的本地文件 `main/lorawan_secrets.h`。仓库只提供 `main/lorawan_secrets.h.example`，真实文件已由 `.gitignore` 排除。

### OTAA 凭据

从示例文件创建 `main/lorawan_secrets.h`，并在其中配置以下宏：

| 宏 | 用途 |
| --- | --- |
| `LORAWAN_JOIN_EUI` | OTAA JoinEUI |
| `LORAWAN_DEV_EUI` | OTAA DevEUI |
| `LORAWAN_APP_KEY_BYTES` | LoRaWAN 1.1 AppKey，16 字节 |
| `LORAWAN_NWK_KEY_BYTES` | LoRaWAN 1.1 NwkKey，16 字节 |

不要把真实凭据写入 README、日志或提交记录。未提供本地 secrets 文件时，配置头使用明确的占位值；应用检测到占位凭据后停在 `CONFIG REQUIRED`，不会使用无效密钥高速重复 OTAA。

### 区域与无线参数

| 宏 | 默认值或作用 |
| --- | --- |
| `LORAWAN_REGION_SELECTION` | 默认 `LORAWAN_REGION_EU868`；配置 RadioLib 区域对象 |
| `LORAWAN_SUB_BAND` | 默认 0；US915/AU915/CN470 等固定信道区域按配置头约束选择 |
| `LORAWAN_SINGLE_CHANNEL_MODE` | 默认 0；设为 1 才启用第 12 号的 EU868 单信道兼容模式 |
| `LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ` | 默认 868100000 Hz；仅用于单信道兼容上行信道 |
| `LORAWAN_SINGLE_CHANNEL_DR` | 默认 DR3；仅用于单信道兼容模式 |
| `LORAWAN_ADR_ENABLED` | 默认 0；单信道兼容模式强制关闭 ADR |
| `LORAWAN_TX_POWER_DBM` | 默认 16 dBm |

### 上行与 Class C

| 宏 | 默认值或作用 |
| --- | --- |
| `LORAWAN_UPLINK_PORT` | 默认 FPort 1 |
| `LORAWAN_UPLINK_INTERVAL_SECONDS` | 默认 60 秒 |
| `LORAWAN_CONFIRMED_UPLINK` | 默认 0，即周期上行使用 unconfirmed；Class C 切换确认帧不受此选项替代 |
| `LORAWAN_CLASS_C_POLL_INTERVAL_MS` | 默认 10 ms；非阻塞轮询异步 RXC 下行 |
| `LORAWAN_SAFE_TEXT_MAX_BYTES` | 日志中安全文本预览的最大字节数，默认 96 |

周期上行负载固定为 9 字节：字节 0..3 是大端递增序号，字节 4..7 是大端运行秒数，字节 8 的高 4 位是格式版本，低位依次表示恢复会话、Class C 活动和待持久化状态。端口、周期、confirmed/unconfirmed、ADR 和功率均由上述宏集中控制。

### 重试、退避与任务

| 宏 | 作用 |
| --- | --- |
| `LORAWAN_JOIN_MAX_ATTEMPTS` | 单批 OTAA 最大尝试次数 |
| `LORAWAN_JOIN_BACKOFF_BASE_SECONDS` | OTAA 指数退避初始值 |
| `LORAWAN_JOIN_BACKOFF_MAX_SECONDS` | 单次 OTAA 退避上限 |
| `LORAWAN_JOIN_BATCH_COOLDOWN_SECONDS` | 一批 OTAA 失败后的冷却时间 |
| `LORAWAN_CLASS_C_MAX_ATTEMPTS` | Class C 协商最大尝试次数 |
| `LORAWAN_RESTORED_SESSION_CHECK_ATTEMPTS` | 恢复 Class C session 后，confirmed 有效性检查的最大次数 |
| `LORAWAN_CLASS_C_RETRY_SECONDS` | Class C 协商失败后的重试间隔 |
| `LORAWAN_SEND_FAILURES_BEFORE_BACKOFF` | 连续上行失败后保留会话并进入退避的阈值 |
| `LORAWAN_RADIO_INIT_RETRY_SECONDS` | LR1121 初始化失败后的重试间隔 |
| `LORAWAN_PERSIST_RETRY_MS` | NVS 保存失败后的重试间隔 |
| `LORAWAN_FCNT_UP_RESERVATION_WINDOW` | FCntUp 写前高水位窗口，默认 32 帧；重启最多跳过该窗口内未使用的计数 |
| `LORAWAN_DISPLAY_MIN_INTERVAL_MS` | 电子纸两次刷新尝试的最小间隔，默认 30000 ms |
| `LORAWAN_DISPLAY_FIRST_TIMEOUT_MS` | 启动无线前等待首次电子纸刷新尝试的超时 |
| `LORAWAN_DISPLAY_TASK_STACK` | 电子纸任务栈大小 |
| `LORAWAN_DISPLAY_TASK_PRIORITY` | 电子纸任务优先级 |
| `LORAWAN_RADIO_TASK_STACK` | 单一无线任务栈大小 |
| `LORAWAN_RADIO_TASK_PRIORITY` | 单一无线任务优先级 |

`LORAWAN_USE_VERSION_1_1` 固定为 1，用于防止把本示例静默改成其他 MAC/激活基线。`LORAWAN_MAC_VERSION_NAME` 和 `LORAWAN_REG_PARAMS_REVISION_NAME` 是日志说明字段，不是运行时切换项。

## RX2 与 RXC 参数

普通 Class C 单播接收使用会话的 RXC 通道。RadioLib 7.7.1 在内部用 `RADIOLIB_LORAWAN_RX_BC` 管理该通道：

1. 初始值来自所选区域的 RX2 默认值；EU868 默认是 869.525 MHz、DR0。
2. JoinAccept 或后续在 RX1/RX2 收到的 `RXParamSetupReq` MAC 命令可以更新 RX2/RXC。
3. 有效 session buffer 会在重启时恢复这些会话参数。

因此代码不会把其他区域示例中的 RX2/RXC 频率和数据率硬编码进 Class C 流程。单信道兼容上行的 868.1 MHz/DR3 也不代表 RX2 必须使用相同参数。电子纸和日志显示的是当前会话实际使用的 RX2/RXC 频率与数据率。

网络服务器必须给设备、网关和下行调度器配置一致的区域、RX2 频率和数据率。参数不一致时，服务器即使接受了上行，设备也可能无法收到异步 Class C 下行。

## RadioLib 7.7.1 Class C 流程

本示例只使用当前依赖源码中实际存在的接口。

### 会话激活或恢复

1. 通过 `beginOTAA()` 建立与当前 LoRaWAN 1.1 凭据对应的节点配置。
2. 从 NVS 先恢复 nonces，只有 nonces 有效时才恢复 session。
3. 调用 `activateOTAA()`。`RADIOLIB_LORAWAN_NEW_SESSION` 和 `RADIOLIB_LORAWAN_SESSION_RESTORED` 都表示成功结果，不能简单按负数判定失败。
4. 新会话进入 Class A 激活态后，再执行 Class C 协商；恢复出的 Class C 会话先显式重新打开 RXC，再发送有界次数的 confirmed 检查帧。有效单播下行或 ACK 可确认服务器仍接受该 session；连续无响应只会标记为 `RESTORED UNVERIFIED` 并保留本地激活会话，不会把暂时无网关、无下行或无 ACK 误判成 session 失效。

### DeviceModeInd 与 DeviceModeConf

LoRaWAN 1.1 下，`setClass(RADIOLIB_LORAWAN_CLASS_C)` 只会把 `DeviceModeInd` 排入 MAC 命令队列，不代表设备已经完成 Class C 切换。本示例在每次有界协商尝试前幂等排队该命令，再执行 confirmed `sendReceive()` 等待服务器返回 `DeviceModeConf`，并以 `getClass() == RADIOLIB_LORAWAN_CLASS_C` 作为最终确认。这样普通 ACK 或无关 Class A 下行清空上行 MAC 队列后，下一次重试仍会携带 `DeviceModeInd`。

网络服务器必须支持并确认这组 MAC 命令。若未收到 `DeviceModeConf`，设备按配置退避重试，不会把普通下行或发送成功误报成 `CLASS C ACTIVE`。

### 持续接收与周期上行

- 异步下行通过 `getDownlinkClassC()` 非阻塞检查；收到有效包或解析错误后，都在同一无线关键区内立即调用受保护 `receiveClassC()` 的公开包装，按 session 当前 RX2/RXC 参数重开连续接收。
- 返回 `RADIOLIB_LORAWAN_RX_BC`（RadioLib 7.7.1 中数值为 3）表示收到有效 RXC 下行；返回 0 表示当前没有待处理包；负值表示解析、MIC、端口或其他错误。
- 接收缓冲区按 `RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE` 分配，当前版本为 242 字节。长度参数是输出值，不是容量输入。
- 有效下行输出 FPort、长度、十六进制和经过字符过滤、长度受限的安全文本；空包、重复帧计数和错误码分别处理。
- RSSI 与 SNR 在成功收包后通过 LR1121 的 `getRSSI()`、`getSNR()` 读取。
- RadioLib 7.7.1 的 LR1121 `getFrequencyError()` 不提供有效测量并固定返回 0，因此电子纸和日志明确显示 `N/A`，不会把 0 Hz 误报为实测频偏。
- 到达上行周期时，同一无线任务暂停 RXC 并调用 `sendReceive()`。无论返回成功、无下行还是错误，应用都会通过受保护 `receiveClassC()` 的公开包装再次启动并检查 RXC，不依赖 RadioLib 内部未向上传递的重开结果。发送与重开保持在同一无线关键区内，所有返回路径都会请求保存 session。
- confirmed 上行不能只按 `sendReceive()` 的非负返回判断成功；返回 0 仅表示没有下行。本示例读取 `LoRaWANEvent_t::confirming` 确认真实 ACK，连续缺少 ACK 会计入失败阈值并触发退避，但不会单凭无 ACK 清除仍处于激活状态的 session。

所有 LR1121/RadioLib 操作只由一个无线任务执行。电子纸任务只接收状态快照，不调用 RadioLib；板级共享 SPI 互斥量只负责总线访问，不能替代无线对象的单一所有权。无线循环只使用短轮询间隔，不用长时间 `vTaskDelay()` 阻断 Class C 接收。

## 状态机与错误恢复

预期状态转换如下：

```text
CONFIG REQUIRED
       |
       v
RADIO INIT -> SESSION RESTORE / JOINING -> CLASS C NEGOTIATION
                                      |             |
                                      |             v
                                      +------> CLASS C ACTIVE
                                                   |  ^
                                          RX / TX -+  |
                                                   |  |
                                     ERROR / BACKOFF -+
```

- `CONFIG REQUIRED`：凭据仍为占位值，停止无线激活。
- `RADIO INIT`：打开 GPIO38、初始化共享 SPI、LR1121、RF switch 和 TCXO。
- `JOINING`：恢复有效会话或执行 LoRaWAN 1.1 OTAA；失败使用有上限的指数退避和批次冷却。
- `CLASS C ACTIVE`：服务器确认 Class C 后持续监听 RXC。
- `RX`：处理异步服务器下行并更新计数、FPort、RSSI、SNR。
- `TX`：发送周期上行，结束后立即恢复 RXC。
- `ERROR`：记录 RadioLib/ESP-IDF 错误码并按配置退避。confirmed 上行缺少 ACK、恢复会话暂时无服务器响应或一般发送错误只保留 session 并退避；RadioLib 明确报告 `RADIOLIB_ERR_NETWORK_NOT_JOINED`、节点 `isActivated()` 已为 false、当前会话的 MAC 版本不符合 LoRaWAN 1.1 基线、FCntUp 无法再预留完整安全窗口或检测到窗口超限时，才清 session、保留 nonces，等待退避后回到正常 OTAA 激活流程。

本示例不使用多个任务同时操作 LR1121，也不在正常 Class C 状态调用 LR1121 sleep 或 ESP32 Deep-sleep。

## NVS、会话与帧计数器

`main/lorawan_session.cpp` 保持第 12 号的兼容布局：

- NVS namespace：`lorawan`；
- nonces key：`nonce`；
- session key：`sess_sc`；
- NVS 句柄名：`s_nvs_handle`，避免 ESP-IDF 5.5 中与 `nvs_handle` 标识符冲突。

恢复顺序为 nonces 在前、session 在后。nonces 缺失、长度错误或被 RadioLib 拒绝时，不加载 session，并只清除已保存 session；这可避免恢复不一致状态。重新激活时同样只清 session、保留 nonces，避免 DevNonce 倒退。

只有“未找到”或尺寸/内容明确无效时才走上述回退。其他 NVS 读取错误会原样返回上层，保持无线暂停并按 `LORAWAN_PERSIST_RETRY_MS` 重试，避免在暂时无法读取历史 DevNonce 或帧计数器时继续发包。

每次 OTAA 尝试后保存 nonces；新会话或恢复会话成功后保存完整 session。每次应用层 `sendReceive()` 前，代码都会先确保 FCntUp 高水位窗口已经持久化。它读取 RadioLib 内部“下一帧将使用”的 FCntUp，生成提高 `LORAWAN_FCNT_UP_RESERVATION_WINDOW` 帧的 session 高水位快照，让 RadioLib 重算快照校验和，并在恢复运行时 FCntUp 后再次调用 `getBufferSession()` 复原内部缓冲一致性。只有该高水位成功提交 NVS 后，才允许发起上行。

发送返回后，无论成功还是错误，代码都会再保存当前完整 MAC/session 状态，但快照中的 FCntUp 至少保持已提交高水位；NVS 层也会拒绝把高水位写低。这样常规应用上行以及 RadioLib 在同一次 `sendReceive()` 的 RX1/RX2 解析中可能触发的 MAC-only 上行，都可以在配置窗口内跨越“已发射、尚未来得及常规保存”的掉电点而不复用已发送计数。重启恢复时运行时 FCntUp 从高水位继续，因此会跳过窗口内尚未使用的计数；网络服务器若配置了最大 FCnt gap，必须允许至少该窗口大小。每个有效异步 RXC 下行后也保存 session，以持久化下行帧计数器；RadioLib 7.7.1 明确拒绝 RXC 中的 FPort 0 和 FOpts MAC 命令，因此 RXC 解析本身不会触发上行。

默认窗口为 32 帧，配置头限制在 2..16384。窗口越大，单次显式 `sendReceive()` 内可覆盖的 RX1/RX2 内部 MAC-only 上行越多，但意外重启后的计数跳跃也越大。RadioLib 7.7.1 的内部 MAC-only 路径可被服务器连续携带大量 MAC 命令的 RX1/RX2 下行再次触发，因此任何有限窗口都不能对恶意或无界递归作数学上的绝对保证：正常返回时若发现 FCntUp 已超过持久化高水位，代码会先保存实际状态并强制建立新 OTAA session；若恰好在超过窗口后的内部递归中断电，仍存在残余风险。部署时应保持网络服务器 MAC 命令行为有界，并按实际服务器的 FCnt gap 策略选择窗口。接近 `UINT32_MAX`、无法再预留完整窗口时，代码会在发射前停止并建立新 OTAA session，避免回绕或永久普通退避。

重启恢复出有效 Class C session 后，应用会显式重新打开 RXC。日志不会输出 session key、AppKey、NwkKey 或其他凭据。

## 电子纸与日志

电子纸沿用当前板级字体和 7 行布局，显示：

- `CONFIG REQUIRED`、`JOINING`、`CLASS C ACTIVE`、`RX`、`TX`、`ERROR`；
- 上行和下行计数；
- FCntUp；
- 最近一次 FPort；
- RSSI、SNR，频偏显示 `N/A`；
- session 是 `RESTORED` 还是新建；
- 当前 RX2/RXC 频率和数据率；
- 最近错误码和简短状态信息。

显示任务使用长度为 1 的覆盖队列，只保留最新快照，并按 `LORAWAN_DISPLAY_MIN_INTERVAL_MS` 合并和限频刷新。启动阶段等待首次电子纸刷新尝试后才初始化无线。运行阶段另有高层无线关键区门：整帧电子纸刷新不能插入 `activateOTAA()`、`sendReceive()`、RXC 启动或下行解析等多步 RadioLib 操作；电子纸传输期间 LR1121 硬件保持 RXC，释放共享 SPI 后无线任务立即继续轮询。第 12 号目录内的本地 `components/esp_epaper` 把 SSD16xx BUSY 等待限制为 8000 ms，并把超时作为 `ESP_ERR_TIMEOUT` 返回，防止面板故障永久占用无线关键区和共享 SPI。

电子纸正常刷新仍会暂时占用共享 SPI，并推迟软件读取 LR1121 已接收的数据；覆盖队列和 30 秒默认限频用于降低这种影响，但高密度连续异步下行仍可能超过射频芯片单次待处理能力。部署时按应用下行速率设置刷新间隔，避免连续下行超出处理能力。

串口日志预期包含状态转换、激活结果、Class C 协商、RX2/RXC 参数、上行结果、异步下行 FPort/长度/十六进制/安全文本、RSSI/SNR、持久化结果和错误退避。日志不打印 OTAA 或 session 凭据。

## 网络服务器要求与适用场景

服务器侧至少需要：

1. 按 LoRaWAN 1.1 OTAA 配置与设备一致的标识和密钥。
2. 使用与设备一致的区域、信道计划和 RX2 频率/数据率。
3. 支持 `DeviceModeInd` / `DeviceModeConf`，并在设备激活后按 Class C 调度下行。
4. 在设备显示或记录 `CLASS C ACTIVE` 后再测试异步下行，并使用有效的应用 FPort。
5. 若服务器限制上行 FCnt 跳跃，允许至少 `LORAWAN_FCNT_UP_RESERVATION_WINDOW` 的 gap，以便掉电恢复后接受高水位计数。
6. 遵守区域占空比、网关发射资源和网络容量限制；Class C 低延迟不代表服务器可以无限制连续发射。

Class C 适合市电或稳定外部供电的远程控制、执行器、告警终端和需要低延迟下行的设备。不适合要求电池长期休眠、主要上报传感数据且很少接收命令的节点；这类设备通常应优先使用 Class A，或在网络支持且需要可预测时隙时评估 Class B。
