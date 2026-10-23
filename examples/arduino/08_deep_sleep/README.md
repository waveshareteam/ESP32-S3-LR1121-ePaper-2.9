# 08 — 深度休眠

本示例对齐 ESP-IDF 08 的主要低功耗步骤：启动时释放上次休眠保持的 GPIO，
初始化外设并显示状态，然后关闭外设并进入 ESP32-S3 深睡。

- 定时唤醒：600 秒（10 分钟），可修改 `kWakeupSeconds`。
- 按键唤醒：BOOT / GPIO0 低电平触发 EXT0；进入深睡前等待按键稳定松开。
- LR1121：检查初始化和休眠命令结果，再隔离 DIO1、NSS、RESET、BUSY 与
  SPI 的 SCK、MISO、MOSI；这些引脚禁用输入输出和上下拉，对支持保持的
  引脚启用 GPIO hold，与 ESP-IDF 08 的处理一致。
- AXP2602：检测到电量计后发送休眠命令，并回读休眠位。未检测到时打印提示，
  与 ESP-IDF 08 一样允许继续，但不能据此确认电量计已经休眠。
- 屏幕：先让控制器休眠，暂时保留共享供电；停止总线、隔离无线引脚后，再将
  GPIO38 拉低并设置深睡保持，避免断电后继续从无线信号脚供电。

休眠准备失败时打印 `SLEEP aborted` 并保持主控唤醒；排查后按 RESET 重试。
成功时输出 `radio pins isolated; shared power held off` 和
`start deep sleep: timer ... / GPIO0 EXT0`。唤醒后会重新运行整个示例。

使用仓库随附的 `Waveshare_LR1121_EPaper` 和 RadioLib 库，开发板设置见
[Arduino 使用说明](../README.md)。基础休眠步骤已对齐 ESP-IDF 08；两套框架
仍使用不同驱动，实际整板电流需实测。包含 Wi-Fi、BLE、SD 和并发任务关断的
完整流程见 [09 综合示例](../09_comprehensive_demo/README.md)。

## 验证

已使用 Arduino-ESP32 3.3.11、RadioLib 7.7.1、ESP32S3 Dev Module 的 N16R8
配置编译通过。Ubuntu Bash 下可运行主机回归：

```bash
python3 examples/arduino/tools/test_deep_sleep.py
```

该命令在仓库根目录执行，需要 Python 3 和 C++ 编译器。测试直接运行示例
代码，模拟两次休眠/唤醒、按键抖动、缺失电量计和各关断步骤失败，检查供电与
引脚状态的操作顺序。实板需另外验证定时/按键唤醒及休眠电流。
