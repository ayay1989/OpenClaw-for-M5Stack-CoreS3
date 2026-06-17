# qoder-v1 差异分析

更新时间：2026-06-17

## 输入来源

- 当前主线：`main`，提交 `e4abc20`
- qoder 分支：`origin/qoder-v1`，提交 `a3d046f`
- 用户反馈：qoder 版本曾让舵机可用，但等待状态、灯带、按压触碰后来又不可用。
- qoder 清单：12 项硬件稳定性参数，重点是 I2C 降频、PY32 延时、Servo 重试。

## qoder-v1 改动范围

`origin/qoder-v1` 相比当前 `main` 只改了以下文件：

- `CHANGES_v9.md`
- `main/main.c`
- `main/py32driver.c`
- `main/leddriver.c`
- `main/scservo_bus.c`
- `main/servodriver.c`

没有改动：

- `main/protocol.c`
- `main/button.c`
- `main/body_service.c`
- `main/emotions_data.c`
- `windows_bridge/*`

这说明 qoder 分支主要是硬件时序/缓冲参数修补，不是协议或交互语义大改。

## 与 12 项清单的对应关系

| 项 | qoder-v1 状态 | 说明 |
| --- | --- | --- |
| I2C 频率 400k -> 100k | 已做 | `cores3_i2c_bus_init(..., 100000)` |
| PY32 初始化后延时 200ms | 已做 | `py32_init` 后 `vTaskDelay(200ms)` |
| 初始化顺序 PY32 -> 延时 -> LCD/LED/Servo | 已做 | 顺序符合清单 |
| PY32 I2C 超时 100ms -> 300ms | 已做 | read/write helper 内超时改为 300ms |
| LED 刷新率 25ms -> 50ms | 已做 | 减少 I2C/PY32 写入频率 |
| UART RX buffer 128 -> 1024 | 已做 | `SCSERVO_RX_BUF_SIZE` |
| UART TX buffer 128/64 -> 512 | 已做 | `SCSERVO_TX_BUF_SIZE` |
| UART queue 0 -> 10 | 已做 | `uart_driver_install` queue size |
| Servo ping timeout 30ms -> 200ms | 已做 | `read_status(id, 200)` |
| Servo power delay 80ms -> 200ms | 已做 | `py32_set_servo_power` 后延时 |
| Servo retry 1 次 -> 3 次 | 已做 | yaw/pitch 分别最多 3 次 |
| 禁用自动居中 | 已做 | 注释 `servo_center()` |

## 吸收风险分级

可直接吸收或轻量改写：

- Servo UART RX/TX 缓冲增大。
- Servo 上电后 200ms 延时。
- Servo 检测 3 次重试，但要有总等待预算和清楚日志。
- PY32 初始化后 200ms 延时，前提是只发生在一次性启动阶段。

需要重写后再吸收：

- I2C 频率降到 100kHz：这是共享总线参数，会影响 FT6336 触摸、PY32 LED、AXP2101/AW9523 操作，不能只按舵机需求判断。
- PY32 read/write 全部改成 300ms：初始化可以更宽松，运行期 LED 写入不宜长期占用 I2C 锁。
- LED 刷新率从 25ms 降到 50ms：PY32 ring 可节流，外接 SK6812/RMT 不应被一刀切降频。
- Servo ping timeout 30ms -> 200ms：和 3 次重试叠加后会拉长启动，建议只给检测路径使用，并保持降级非阻塞。
- UART queue 0 -> 10：如果没有消费 queue handle，收益不明确；只增大 RX/TX 缓冲即可。
- 禁用自动居中：应改成配置开关，默认不上电乱动，同时保留显式 `center` 命令。

不建议直接整分支合并：

- qoder-v1 基于当时主线，不包含后续 `e4abc20` 的 ESP-IDF v5.5 新 I2C master API 迁移。
- 直接合并可能把新 I2C API 迁移回滚到旧 API 或造成冲突。

## 对“舵机好了但等待/触摸/灯带坏了”的初步判断

qoder-v1 没改触摸事件语义，也没改 waiting/presence 逻辑。因此回退更可能来自：

- I2C/PY32 总线被舵机/LED 写入占用后，触摸读取更容易超时。
- LED/PY32 写入节流不足或通道映射未验证，导致灯带看起来不亮。
- Servo 检测变慢后，初始化阶段阻塞更久，其他任务启动时序改变。
- 禁用自动居中后，用户肉眼可能认为“等待动作没了”，但实际是 motion available 后不再主动动。

## 吸收策略

不要 cherry-pick 整个 qoder-v1。

建议分三批改：

1. **板级抽取批**：把 HtSz 的 CoreS3 I2C、AXP2101、AW9523、PY32、FT6336、SCServo 参数落成文档，作为后续验收基线。
2. **舵机稳定批**：吸收 RX/TX 缓冲、VM_EN 200ms、电机检测重试；长 timeout 只用于检测路径；启动自动居中改成配置。
3. **PY32/LED 批**：把 PY32 初始化 timeout 和 LED 运行期 timeout 分开，避免 LED 呼吸灯抢占触摸 I2C。
4. **触摸回归批**：参考 HtSz 把触摸轮询从 50ms 调整为更灵敏的周期，并验证 press/hold/release/tap/double_tap/swipe 都能发事件。

## 待 HtSz 抽取确认的问题

- HtSz 是否全局使用 100kHz I2C，还是只在 PY32/servo 阶段降低有效压力。
- HtSz 的 PY32 LED RAM 写入格式是否与当前 `rgb565` 小端写法一致。
- HtSz 的触摸读取周期、超时、任务优先级。
- HtSz 的 SCServo UART 引脚、缓冲、超时和检测重试。
- HtSz 是否上电自动居中，还是只在明确命令下居中。
