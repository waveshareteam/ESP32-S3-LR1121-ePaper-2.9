# 13 LoRaWAN Multicast

本示例面向 ESP32-S3 + LR1121 + 2.9 英寸电子纸，保留
已验证的设备单播 OTAA 会话，在此基础上增加一个
静态预配置的 LoRaWAN Class C 组播下行会话。

## Multicast 是什么

普通单播下行使用某一台终端自己的 DevAddr 和单播会话密钥，服务器要给
多台设备发送同一内容时，需要分别发送多次。Multicast 则让多台终端共享
一个 McAddr、McNwkSKey 和 McAppSKey，服务器只需向该组发送一份下行，
组内设备即可接收同一内容。它适合批量配置、广播通知和固件分片的承载
通道等一对多场景，可减少重复下行。

Multicast、入网方式和设备类别是三个不同维度：

- OTAA/ABP 决定设备自己的单播会话如何建立；组播不是入网方式。
- Class A/B/C 决定接收窗口；组播通常依赖 Class B 或 Class C。
- 本示例的设备单播会话使用 **OTAA + Class A**，组播会话使用
  **静态配置 + Class C**。组播不会取代设备自己的单播会话。
- 普通上行始终通过设备自己的单播会话发送，设备不会向 McAddr 发送上行。

## 当前 RadioLib 7.7.1 的能力边界

本示例的 manifest 和 `dependencies.lock` 均精确固定 `jgromes/radiolib`
7.7.1，并与第 12 号基线的锁定版本、该版本 `LoRaWAN_Multicast` 官方示例
和 `LoRaWAN.h/.cpp` 实现逐项核对：

| 能力 | 本示例状态 |
| --- | --- |
| 静态预配置组播会话 | 支持 |
| Class C 组播 | 支持 |
| Class B 组播 | 不支持，7.7.1 明确返回 `RADIOLIB_ERR_UNSUPPORTED` |
| 同时可用组播 slot | RadioLib 最多 4 个；本示例只启用 slot 0 |
| Remote Multicast Setup Package | 未实现 |
| Application Layer Clock Synchronization | 未实现 |
| Fragmented Data Block Transport | 未实现 |
| FUOTA 完整流程 | 未实现 |

因此，本示例不能远程创建或更新组播组，也不提供 Class B beacon/ping-slot、
时钟同步、固件分片重组、镜像校验或升级安装。Multicast 只是 FUOTA 可能使用
的一条下行承载路径，不等同于 FUOTA。

## 源码结构

示例按职责拆分，`main.cpp` 只保留顶层状态机和模块装配。建议阅读顺序如下：

| 文件 | 职责 |
| --- | --- |
| `main/main.cpp` | 配置门禁、板级与 NVS 初始化、单播恢复/入网以及顶层重入网流程 |
| `main/lorawan_profile.h/.cpp` | EU868 单信道区域、凭据占位检查、OTAA 初始化参数和静态组播参数 |
| `main/radio_runtime.h/.cpp` | 无线 mutex、事件组、单调时钟/延时和异步电子纸状态快照 |
| `main/multicast_session.h/.cpp` | 组播 NVS 状态、McFCntDown 恢复以及 Class C slot 的启动/停止/重建 |
| `main/lorawan_flow.h/.cpp` | OTAA 激活、普通单播上行、下行分类/边界检查和 Class C 主接收循环 |
| `main/multicast_store.h/.cpp` | RadioLib 单播 buffer 与 32 字节组播计数器 blob 的严格持久化 |
| `main/multicast_ui.h/.cpp` | 单元素覆盖队列、显示限频和电子纸文本格式化 |
| `main/multicast_config.h` | 非敏感的区域、组播、端口、周期、日志和显示参数 |
| `main/lorawan_secrets.h` | 每台设备本地创建的 EUI 与密钥，不提交、不打印 |
| `components/esp32_s3_lr1121_epaper_2_9_support/` | LR1121 RadioLib HAL、共享 SPI/电源所有权和电子纸适配 |

各手写模块使用英文注释说明 API、所有权和关键顺序约束；配置和操作说明仍集中
在本 README，避免在 `main.cpp` 重复堆叠实现细节。

## 运行状态机

1. 若 EUI、OTAA 密钥、McAddr 或组密钥仍为占位值，进入
   `CONFIG REQUIRED`，不会启动 LR1121，也不会发射或接收。
2. 初始化 NVS、共享 SPI、电源和 LR1121。
3. 恢复第 12 号兼容的单播 nonce/session；没有可恢复状态时执行 OTAA。
4. 按静态配置创建组播 slot 0，并立即进入 Class C 持续接收。
5. `getDownlinkClassC()` 处理 RxC 下行，通过事件中的 `multicast` 和
   `mcGroupId` 区分 `UNICAST` 与 `MULTICAST`。
6. 每 60 秒发送普通上行前，显式停止并移除组播 slot，使 RadioLib 只执行
   设备自身的 Class A 发射和 RX1/RX2；返回后先保存单播 session，再用 NVS
   中最后接受的 McFCntDown 和静态组参数重建 slot，立即恢复组播 RxC。
7. `RADIOLIB_ERR_DOWNLINK_MALFORMED`（-1102）和
   `RADIOLIB_ERR_NO_RX_WINDOW`（-1105）发生在本次发射之后的接收流程；本次
   上行已计入，代码会保存 FCntUp、恢复 RxC，但不会把它们累计为发送失败或
   重入网。
8. 单播会话明确失效时重新 OTAA；其他连续硬错误仍按阈值进入会话恢复。
   确定性的组播频率、DR、slot、Class 或计数器配置错误进入
   `CONFIG REQUIRED`，不会无限重试。

## 配置

敏感值放在被 `.gitignore` 忽略的 `main/lorawan_secrets.h`。仓库只提供
`main/lorawan_secrets.h.example` 全零模板；不要提交或打印实际密钥。

### 单播配置

| 宏 | 默认值/用途 | 位置 |
| --- | --- | --- |
| `LORAWAN_JOIN_EUI` | OTAA JoinEUI，占位值必须替换 | `main/lorawan_secrets.h` |
| `LORAWAN_DEV_EUI` | 每台设备独立的 DevEUI | `main/lorawan_secrets.h` |
| `LORAWAN_APP_KEY_BYTES` | 每台设备的 AppKey | `main/lorawan_secrets.h` |
| `LORAWAN_NWK_KEY_BYTES` | 每台设备的 NwkKey | `main/lorawan_secrets.h` |
| `LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ` | 868100000 Hz | `main/multicast_config.h` |
| `LORAWAN_SINGLE_CHANNEL_DR` | EU868 DR3 | `main/multicast_config.h` |
| `LORAWAN_SUB_BAND` | 0 | `main/multicast_config.h` |
| `LORAWAN_ADR_ENABLED` | 0，保持 ADR 关闭 | `main/multicast_config.h` |
| `LORAWAN_INITIAL_POWER_DBM` | 初始发射功率 10 dBm | `main/multicast_config.h` |
| `LORAWAN_UPLINK_PORT` | FPort 1 | `main/multicast_config.h` |
| `LORAWAN_UNICAST_DOWNLINK_PORT` | 应用单播下行 FPort 2 | `main/multicast_config.h` |
| `LORAWAN_UPLINK_INTERVAL_SECONDS` | 60 秒 | `main/multicast_config.h` |
| `LORAWAN_JOIN_MAX_ATTEMPTS` | 每批最多 5 次 OTAA | `main/multicast_config.h` |
| `LORAWAN_JOIN_RETRY_SECONDS` | 同批入网重试至少间隔 15 秒 | `main/multicast_config.h` |

区域不是凭据项；实际 `EU868` 选择和单信道 band 构造位于
`main/lorawan_profile.cpp` 的 `make_single_channel_region()`。

这些区域、单信道、DR、ADR 关闭、sub-band、发射功率和 OTAA/session 流程
沿用第 12 号基线。RadioLib EU868 的单播 RX2 默认保持 869.525 MHz/DR0；
组播默认也使用 869.525 MHz/DR0，但两者仍是独立配置。单信道配置不是通用
EU868 部署模板；网络服务器和网关必须与第 12 号部署保持一致。

### LR1121 板级基线

| 项目 | 固定值 | 源码位置 |
| --- | --- | --- |
| 共享 SPI | SPI3_HOST，SCK40/MISO46/MOSI45 | `components/esp32_s3_lr1121_epaper_2_9_support/src/board_support.c` 与 `components/gen_bmgr_codes/gen_board_periph_config.c` |
| LR1121 控制 | DIO1/IRQ6、NSS/CS42、RST39、BUSY41、8 MHz | `components/esp32_s3_lr1121_epaper_2_9_support/src/board_support.c` 与 `components/gen_bmgr_codes/gen_board_device_config.c` |
| 共享电源 | GPIO38 高有效，上电后等待 100 ms，Class C 期间保持高 | `components/esp32_s3_lr1121_epaper_2_9_support/src/board_support.c` |
| TCXO | 1.6 V | `components/esp32_s3_lr1121_epaper_2_9_support/src/board_lorawan_radio.cpp` |
| RF switch/PA | DIO5/DIO6，LR1121 高功率 PA | `components/esp32_s3_lr1121_epaper_2_9_support/src/board_lorawan_radio.cpp` |

板级值不是部署参数，不在 `multicast_config.h` 重复定义，避免形成未被
驱动使用的第二配置源。

### 组播配置

| 宏 | 默认值/用途 | 位置 |
| --- | --- | --- |
| `MULTICAST_GROUP_ID` | RadioLib slot 0 | `main/multicast_config.h` |
| `MULTICAST_MC_ADDR` | McAddr，全零时禁止启动 | `main/lorawan_secrets.h` |
| `MULTICAST_MC_APP_S_KEY_BYTES` | McAppSKey，全零时禁止启动 | `main/lorawan_secrets.h` |
| `MULTICAST_MC_NWK_S_KEY_BYTES` | McNwkSKey，全零时禁止启动 | `main/lorawan_secrets.h` |
| `MULTICAST_DEVICE_CLASS` | `RADIOLIB_LORAWAN_CLASS_C` | `main/multicast_config.h` |
| `MULTICAST_FREQUENCY_HZ` | 869525000 Hz | `main/multicast_config.h` |
| `MULTICAST_DATARATE` | EU868 DR0 | `main/multicast_config.h` |
| `MULTICAST_DOWNLINK_PORT` | 应用组播下行 FPort 10 | `main/multicast_config.h` |
| `MULTICAST_MC_FCNT_MAX` | `UINT32_MAX`，允许的组下行计数器上限 | `main/multicast_config.h` |
| `MULTICAST_CONFIG_GENERATION` | 1，组配置版本，替换组时递增 | `main/multicast_config.h` |
| `MULTICAST_CLEAR_STATE_ON_BOOT` | 0，显式清理组播 NVS 的维护开关 | `main/multicast_config.h` |

### 运行与显示参数

| 宏 | 默认值/用途 | 位置 |
| --- | --- | --- |
| `MULTICAST_CLASS_C_POLL_MS` | 20 ms，检查已触发的 RxC 包 | `main/multicast_config.h` |
| `MULTICAST_RX_ERROR_RESTART_COUNT` | 连续 3 次解析错误后重建组播会话 | `main/multicast_config.h` |
| `MULTICAST_ERROR_BACKOFF_SECONDS` | 错误退避起点 5 秒 | `main/multicast_config.h` |
| `MULTICAST_MAX_BACKOFF_SECONDS` | 退避上限 300 秒 | `main/multicast_config.h` |
| `MULTICAST_LOG_HEX_ENABLED` | 1，输出分段 HEX；设 0 关闭 | `main/multicast_config.h` |
| `MULTICAST_LOG_TEXT_ENABLED` | 1，输出安全 ASCII；设 0 关闭 | `main/multicast_config.h` |
| `MULTICAST_LOG_SAFE_TEXT_LIMIT` | 安全文本日志最多 96 字节 | `main/multicast_config.h` |
| `MULTICAST_DISPLAY_MIN_INTERVAL_MS` | 电子纸最短刷新间隔 5000 ms | `main/multicast_config.h` |
| `MULTICAST_DISPLAY_TASK_STACK` | 显示任务栈 8192 字节；LVGL/电子纸刷新链路不得低于此值 | `main/multicast_config.h` |
| `MULTICAST_DISPLAY_TASK_PRIORITY` | 显示任务优先级 3 | `main/multicast_config.h` |

多台设备要加入同一组，必须匹配 McAddr、McAppSKey、McNwkSKey、Class C、
频率、DR、应用 FPort 和服务器组下行计数器状态。每台设备仍应有不同的
DevEUI、OTAA 根密钥和单播会话。

## 网络服务器配置

不同服务器界面名称不同，但配置关系相同：

1. 保留每台设备原有的普通 OTAA 注册，让每台设备先建立自己的单播会话。
2. 创建静态 Class C multicast group，设置 McAddr、McAppSKey 和 McNwkSKey。
3. 将需要接收的终端加入该组；不要删除其普通设备/单播会话配置。
4. 将组播频率设为 869.525 MHz、数据率设为 EU868 DR0，并与设备端宏逐项核对。
5. 以 FPort 10 发送 **unconfirmed** 组播下行。LoRaWAN 组播不接受 confirmed
   downlink，也不存在终端向组地址返回 ACK 的流程。
6. 确认服务器发送的下一个 McFCntDown 严格大于设备 NVS 中最后接受的值，
   或在双方明确同步后从新的计数器窗口开始。
7. 先用一台设备核对日志、频率、DR、FPort 和计数器，再扩展到多台设备。

本示例不会通过无线命令把设备加入组；服务器端创建组后，设备端仍需预先
配置完全相同的静态参数和密钥。

## 计数器、NVS 与安全

RadioLib 7.7.1 在内存中负责组播 MIC、解密、McFCntDown 检查和每组接收计数，
但单播 session buffer 不包含组播组及其 McFCntDown。为防止重启后计数器回退：

- 单播仍使用 NVS namespace `lorawan` 中的 `nonce` 和 `sess_sc`；读取或
  恢复 RadioLib nonce/session buffer 时同时持有 radio mutex。
- RadioLib 在完成发射后才处理 RX1/RX2，并会在这些接收步骤返回错误时照样
  推进内部 FCntUp。公开 `getFCntUp()` 在首次上行前后都可能返回 0，因此代码
  不能只比较前后值；只要实际调用过 `sendReceive()`，无论返回值正负，都先
  将完整单播 session buffer 提交到 NVS，避免重启后上行计数器回退。
- 组播使用独立 32 字节 `mc_state_v1` blob，包含 magic、schema、结构长度、
  slot、配置版本、McAddr、频率、DR、计数器有效位、最后成功事件的
  `event.fCnt` 和 CRC32。
- 恢复时严格检查长度、版本、CRC、组标识和配置；损坏或不匹配不会被静默
  覆盖，而是进入 `CONFIG REQUIRED`。
- 应用层拒绝 `event.fCnt <= 已持久化值`。只有新计数器已写入，或写错误后
  通过重新打开 NVS 并读回确认当前在途计数器已经持久化，才处理载荷。
- `getRxFCntMulticast()` 只是接收条数，不是可用于持久化的 McFCntDown；
  本示例使用成功下行事件的 `event.fCnt`。

`MULTICAST_CLEAR_STATE_ON_BOOT=1` 只清除组播 blob，随后停在
`CONFIG REQUIRED`，不会启动无线。清除后必须同步或重建服务器端组计数器，
再把开关恢复为 0。若更换组密钥、McAddr 或计数器窗口，建议同时递增
`MULTICAST_CONFIG_GENERATION`。仅清除设备状态而让服务器继续旧计数器，或让
设备以更小计数器重新接收，会造成拒收窗口、计数器混乱，并扩大重放风险。

组内设备共享组密钥；任意一台设备泄露都可能危及整个组。生产部署应限制
密钥访问、按组隔离权限、规划轮换/撤销，并避免把高权限设备和低信任设备
放在同一组。Multicast 没有每台设备独立确认，服务器也不能仅凭一次发送
证明所有终端都已收到。

## 数据路径、日志和边界

- 日志明确标记 `UNICAST` 或 `MULTICAST`，输出 FPort、长度、FCntDown、频率、
  DR、RSSI、SNR、分段 HEX 和最多 96 字节的安全 ASCII 文本。
- RadioLib 7.7.1 的 LR1121 频偏接口在此路径不能提供有效值，因此本示例
  明确不输出伪造的频偏数据。
- 接收缓冲区固定为 `RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE`；代码检查空载荷、
  超长长度、未知 slot、未知 FPort、重复帧和解析错误。
- 未知 FPort 的帧仍会推进并持久化安全计数器、写入诊断日志，但不会进入
  对应应用处理路径，避免同一帧被重复利用。
- 普通上行期间可看到 `MULTICAST RxC paused`、`unicast session saved` 和
  `MULTICAST RxC restored`。若出现 -1102/-1105，日志会明确写成 TX 已完成、
  RX1/RX2 处理错误，而不是 `UNICAST TX hard error`。
- `UNICAST TX completed` 只表示本地 RadioLib 已走完发射路径，不证明网关或
  网络服务器已经收到该帧；仍应在 ChirpStack 中核对对应的 uplink 事件。
- `MULTICAST ACTIVE` 表示 RadioLib 已接受组配置并报告 Class C slot；它不是
  网关下发成功或 LR1121 已收到真实组播帧的硬件验证结论。

## 无线所有权、功耗和电子纸

所有可能访问 LR1121 的 `LoRaWANNode` 操作，以及 NVS session/nonce buffer
getter/setter，只在主无线任务执行并通过同一个 mutex；`isActivated()` 和
`timeUntilUplink()` 等只读元数据也只由该主任务读取。event group 标记
`JOINING`、`RXC`、`TX`、`RECOVERY`。电子纸由独立队列任务
刷新，共享 SPI 由板级 mutex 串行化；单元素覆盖队列和 5 秒最小刷新间隔
避免显示阻塞持续接收。

Class C 会话有效后，LR1121 除普通上行的 Class A TX/RX1/RX2 阶段外持续监听；
上行前临时移除组播 slot，RX1/RX2 结束且单播 session 已保存后立即重建 slot
并恢复组播 RxC。示例不会让 LR1121 sleep，也不会让 ESP32 进入
Deep-sleep。GPIO38 是电子纸和 LR1121 的共享电源，使能后保持高电平；电子纸
控制器可进入自身 sleep，但不能拉低该共享电源。因此 Class C 功耗明显高于
Class A，不适合依赖长时间电池休眠的终端。

从暂停 slot 到恢复 RxC 之间存在短暂的组播接收空档，这是普通上行及其
RX1/RX2 窗口的必要代价；服务器不应假定设备在这段时间仍能接收组播下行。

电子纸显示：

- `JOINING`、`MULTICAST ACTIVE`、`RX`、`TX`、`ERROR`、`CONFIG REQUIRED`；
- 单播/组播接收数和上行数；
- slot 与可公开显示的 McAddr；
- 最近 FPort、RSSI、SNR；
- 单播 session 是否恢复以及最近错误码。

## 静态检查

在本示例目录下可执行以下 Windows PowerShell 命令：

```powershell
python .\static_contract_check.py
```

脚本只读取第 11/14 号源码、manifest、依赖锁和 RadioLib 7.7.1 参考实现，
不会调用 `idf.py`、CMake、Ninja、编译器、烧录工具或串口。它检查目录结构、
CMake 源文件与组件、依赖版本、真实组播 API、LR1121 引脚/区域、单播与组播
边界、Class C 恢复、NVS/计数器、缓冲区、端口、敏感信息和 README 宏映射。

本次交付按要求只完成静态检查：**未编译、未烧录、未进行网络服务器联调，
也未进行单设备或多设备实板组播验证**。静态检查不能证明 RF 参数、天线、
网关、服务器配置、真实功耗或电子纸时序在硬件上正确。
