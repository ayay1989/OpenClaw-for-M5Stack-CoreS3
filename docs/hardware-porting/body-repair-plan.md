# CoreS3 身体修复计划

更新时间：2026-06-17

目标：同时守住舵机、灯环、屏幕触摸/按压，不再出现单项修好导致另一项回退。

## 工程拆分

### A. 板级时序

负责人：主控集成。

改动范围：

- `main/main.c`
- `main/py32driver.c`

交付：

- 保持 CoreS3 正确 I2C 引脚和 ESP-IDF v5.5 I2C master API。
- 不全局降到 100kHz。
- PY32 初始化 probe 更保守。
- `py32_init()` 成功或失败后都不阻塞 LCD/触摸/WiFi。
- PY32 初始化后增加 200ms 稳定窗口，只影响一次性启动。

### B. 舵机稳定

负责人：舵机 worker。

改动范围：

- `main/scservo_bus.c`
- `main/servodriver.c`
- `main/Kconfig.projbuild`

交付：

- UART RX/TX buffer 放大。
- 不启用未消费的 UART queue。
- VM_EN 后等待 200ms。
- yaw/pitch 检测最多 3 次，但保持降级非致命。
- 启动自动居中改成配置，默认关闭自动乱动，显式命令仍可 `center`。

### C. 灯环与触摸回归

负责人：触摸/LED worker。

改动范围：

- `main/leddriver.c`
- `main/py32driver.c`
- `main/main.c`

交付：

- PY32 LED 运行期写入使用短超时和节流，避免长期占用 I2C。
- FT6336 轮询靠近 HtSz 的 20ms。
- press/hold/release/tap/double_tap/swipe 事件保持协议一致。

### D. 验收

负责人：验收 agent。

检查依据：

- `docs/hardware-porting/htsz-extract-01-board-init.md`
- `docs/hardware-porting/htsz-extract-02-body-io.md`
- `docs/hardware-porting/qoder-v1-diff-analysis.md`

必须确认：

- 没有回滚 ESP-IDF v5.5 I2C API。
- qoder-v1 没有被整包合入。
- HtSz 事实和 qoder 实验项已分开。
- 舵机失败不会阻塞触摸、灯环、WiFi、TCP。
- LED 呼吸不会把 FT6336 触摸饿死。

## 真机验收清单

烧录后串口日志应关注：

- I2C scan 至少看到 `0x34`、`0x38`、`0x58`、`0x6F`。
- `PY32 initialized` 或明确 `PY32 not available`。
- `servo VM_EN power enabled`。
- `servos detected` 或明确 `servos not detected`，但系统继续启动。
- `FT6336 touch initialized`。
- `OpenClaw Stackchan CoreS3 firmware started`。

可见行为：

- 上电默认 happy 表情。
- 默认蓝色呼吸灯或 PY32 ring 可见。
- 触摸屏幕能上报 press/release；长按能上报 hold/long_press。
- 如果舵机实体存在，收到 `motion center/nod/shake/tilt` 能动作。
- 如果舵机不存在或没上电，OpenClaw 仍能连接、显示、触摸、发心跳。
