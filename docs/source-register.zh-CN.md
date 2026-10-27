# 内容来源登记

> 历史资料草稿：部分路径、编号和测试状态已变化。编写使用手册时以 [当前交接说明](manual-handoff.zh-CN.md)、示例 README 和源码为准；本草稿中的旧构建失败记录不代表当前状态。

更新时间：2026-08-22（Asia/Shanghai）

## 一方仓库资料

| 来源 | 用途 | 当前边界 |
| --- | --- | --- |
| `README_ZH.md` / `README.md` | 产品名、版本、示例矩阵、支持框架 | 明确声明尚未编译、烧录或实板验证 |
| `config/ci.json` | N16R8、ESP-IDF/Arduino 版本矩阵 | CI 配置尚未在真实远端运行 |
| `examples/arduino/README.md` | Arduino 环境、引脚、依赖、预期日志 | 实现参考，不替代正式规格 |
| `examples/esp-idf/README.md` | Board Manager、刷新策略、构建命令 | IDF 分支仍需真实构建验证 |
| `examples/esp-idf/components/waveshare_esp_boards/.../*.yaml` | 总线、设备和 GPIO | 由现有示例迁移，未证明实装料号 |
| `hardware/schematics/ESP32-S3-LR1121-ePaper-2.9-XF-Schematics.pdf` | 核对网络与器件关系 | Rev1.1，2.9 英寸产品原理图 |
| `assets/product/front.png` / `back.png` | 真实产品外观 | 正面图含 Join EUI，公开前需标识符审核 |
| 工作区根目录 `log.json` | ChirpStack EU868 实测背景 | 用户文件，不纳入仓库，不公开设备标识 |

## 参考 Wiki

以下页面只用于栏目组织、术语和信息密度；相近产品数值不直接移植。

- https://docs.waveshare.net/ESP32-S3-DEV-KIT-N8R8/
- https://docs.waveshare.net/ESP32-S3-LR1121-XF/
- https://docs.waveshare.net/ESP32-S3-ePaper-3.97/
- https://docs.waveshare.net/ESP32-S3-ePaper-1.54/

可沿用的组织规律：产品简介、产品特性、板载资源、参数表、接口/尺寸、开发
方式，以及拆分的 Arduino、ESP-IDF、固件烧录、FAQ、相关资料和技术支持页。

Wiki 首页保持中等信息密度，环境、逐例程、FAQ 和下载在同一草案中保留独立
章节，后续接入 Docusaurus 时可拆成侧栏子页面。硬件版本或示例不兼容提示应
放在简介后，而不是埋在 FAQ。

## 参考商品页

以下页面只用于微雪官网详情页的信息顺序与视觉密度；不复制价格、库存、
SKU、认证、包装或规格。

- https://www.waveshare.net/shop/ESP32-S3-ePaper-1.54.htm
- https://www.waveshare.net/shop/ESP32-S3-ePaper-3.97.htm
- https://www.waveshare.net/shop/ESP32-S3-LR1121-LF.htm
- https://www.waveshare.net/shop/ESP32-S3-DEV-KIT-N8R8.htm

商品页共同采用工程信息优先的长滚动布局：真实产品/工程图、单一卖点的全宽
内容带、紧凑参数表、接口与尺寸图、开发资源和包装说明。当前详情页沿用这一
信息顺序，并删除与本产品无关的 AI、音频、触控、价格和售后模块。

## 外部芯片能力参考

微雪 `ESP32-S3-LR1121-XF` Wiki 说明 LR1121 芯片覆盖 Sub-GHz
150–960 MHz、S 频段 1.9–2.1 GHz 和 2.4 GHz ISM，并支持 LoRa、(G)FSK、
LR-FHSS。这些是 LR1121/相近模组能力，不自动等于本整板的射频匹配规格。

## 使用规则

1. 仓库一方资料优先于相近产品页面。
2. 原理图与代码冲突时，不自行选择“更合理”的数值。
3. 相近产品的尺寸、频段、灵敏度、功率、距离、耗流和包装均不可套用。
4. 用户给出的低于 12 µA 只作为待补条件的阶段性结果。
5. 所有 LoRaWAN 凭据、设备标识和内部服务器信息在公开内容中脱敏。
