# 使用手册编写交接

本分支整合已测试的本地示例改动与 GitLab 主分支的 SD 示例、编号调整和
SensorLib 电量计迁移。它是手册编写与代码评审基线，不是已发布的成品固件。

## 采用当前编号

ESP-IDF 与 Arduino 各有 13 个示例，目录见根 README 的对照表。
原本的 07 深睡移为 **08**，原本的 08 综合示例移为 **09**，原来的
09–12 LoRaWAN 示例移为 10–13；新增 **07 SD 卡**。截图、章节标题、下载
说明和命令均应使用当前编号。

## 推荐编写入口

- [Arduino 安装与示例说明](../examples/arduino/README.md)
- [Arduino 09 综合测试](../examples/arduino/09_comprehensive_demo/README.md)
- [ESP-IDF 环境与 Board Manager](../examples/esp-idf/README.md)
- [ESP-IDF 03 电子纸功能测试](../examples/esp-idf/03_epaper_2_9/README.md)
- [ESP-IDF 09 综合测试](../examples/esp-idf/09_comprehensive_demo/README.md)
- [SD 卡操作与测试文件清理](../examples/arduino/07_sd_card/README.md)
- [硬件原理图](../hardware/schematics/ESP32-S3-LR1121-ePaper-2.9-XF-Rev1.1.pdf)
- [HF/LF 参考固件及校验值](../firmware/README.md)

## 需要写清楚的操作

Arduino 选择 ESP32S3 Dev Module、16 MB Flash、OPI PSRAM、USB CDC On Boot
Enabled、Hardware CDC and JTAG。更新随仓库提供的 `Waveshare_LR1121_EPaper`
库后再编译；旧库副本会继续显示旧字体。所有墨水屏文字改为 Terminus Bold
8×14，长文本省略显示，综合示例采用双列状态格和底部 LoRa 计数。

综合示例中 GPIO1 稳定按下一次发送一帧；GPIO0 双击休眠，深睡时单击唤醒。
启动和唤醒均重新自检。Wi-Fi、LoRa 参数集中在各自的 `main/app_config.h`
或 `app_config.h`；截图和手册不包含真实凭据。2.13 寸配置采用较短的界面提示。

ESP-IDF 源码不包含 `build`、`managed_components`、`sdkconfig`、锁文件和
`components/gen_bmgr_codes`。在所选示例目录执行：

```bash
python -m pip install esp-bmgr-assist==0.8.3
idf.py bmgr -l -c ../boards
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9 -c ../boards
idf.py build
```

以上命令用于 Ubuntu Bash（已加载对应 ESP-IDF 环境）。07 SD 示例不使用
Board Manager。仅查看已发布微雪板型可直接运行 `idf.py bmgr -l`；本产品的
板定义随源码存于 `examples/esp-idf/boards/`，无需修改下载组件。

## 验证边界与待补材料

- 整合后的全部 13 个 Arduino 示例已使用 Arduino-ESP32 3.3.11、RadioLib
  7.7.1 编译通过（ESP32S3 Dev Module、16 MB Flash、OPI PSRAM）。综合示例
  在整合前也通过了 2.13 寸配置编译。
- 字体渲染、两种屏幕尺寸、文本边界、消抖和双击逻辑已通过主机测试。
- 全部 13 个 ESP-IDF 示例已在 5.5.6 和固定提交的 6.2 开发版下，从独立
  源码目录生成配置并编译通过，共 26 次构建。09 的源码契约检查通过，
  并在两个环境中分别通过 3 项 NimBLE 生命周期主机回归。应用分区余量在
  5.5.6 下为 16 KB（约 1%），6.2 下约 2%，增加功能前需要重新检查大小。
  用户此前报告 ESP-IDF 示例实板正常；本次兼容改动尚未进行实板复验。
- 全部 13 个 ESP-IDF 示例现声明 `>=5.5.0,<6.3.0`，包括 09 综合示例。
  手册使用 5.5 稳定版；6.2 以固定 SDK 提交作为开发版兼容验证。
  完整构建矩阵与验证边界见 [版本兼容说明](idf-compatibility.md)。
- 应补充当前固件的屏幕实拍、按键操作、双板 LoRa 收发、Wi-Fi/BLE 共存和
  深睡电流记录。编译、主机测试均不替代这些实板检查。
- 当前快照没有产品正反面照片和尺寸图；HF/LF 二进制的源码版本与烧录偏移
  未建立对应，不作为本 MR 的编译产物。
- `wiki.zh-CN.md` 等早期资料保留为历史草稿；其旧路径、旧编号和失败记录
  不应直接抄入手册。规格不确定项需要按原理图、BOM 或测量记录确认。
