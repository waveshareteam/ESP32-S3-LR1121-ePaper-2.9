# 03 — 电子纸功能测试

显示屏幕分辨率、状态与更新计数，每 30 秒更新一次，采用一次 FULL 后十次
FAST 的刷新策略。默认使用黑白文字界面。

## 构建与运行

Ubuntu Bash（先加载 ESP-IDF 环境，并进入本示例目录）：

```bash
python -m pip install esp-bmgr-assist==0.8.3
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9 -c ../boards
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

串口设备需按实际连接替换。板型源文件位于 `../boards/`，生成的
`components/gen_bmgr_codes/` 不提交到仓库。

## 显示模式

在 `03 e-Paper display → Display mode` 中选择：

| 选项 | 行为 |
| --- | --- |
| `Functional (customer default)` | 默认模式，显示状态并定时更新计数 |
| `Clear screen (full white, then sleep)` | 启动后全刷为纯白屏，然后让屏幕休眠 |

功能模式串口输出 `e-paper test screen shown`。清屏模式每次启动执行一次，
不显示文字，也不定时刷新；完成后输出
`screen cleared to white; panel sleeping; periodic refresh disabled`。

模式保存在本地 `sdkconfig` 中。恢复功能界面时，选择 `Functional`，重新
编译并烧录。升级已有工程时也请重新检查该选项。
