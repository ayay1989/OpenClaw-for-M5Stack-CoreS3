# Stackchan-HtSz 身体输入输出抽取 02

更新时间：2026-06-17

## 舵机

HtSz 舵机实现走 SCSCL 串行舵机总线：

- UART：`UART_NUM_1`
- 波特率：`1000000`
- TX/RX：`GPIO6 / GPIO7`
- UART buffer：默认 `1024 / 1024`
- UART queue：`0`
- 串口读超时：`100ms`
- 写完成等待：`100ms`
- yaw ID：`1`
- pitch ID：`2`
- yaw 范围：`-45..45`
- pitch 范围：`5..60`
- yaw position：`460 + yaw_deg * 16 / 5`
- pitch position：`620 + pitch_deg * 16 / 5`

HtSz `Begin()` 中不显式 ping 两个舵机；它初始化 UART 后直接 `MoveTo(0, 30, 1500)`，然后启动每 4 秒一次的空闲扫视。

对当前项目的结论：

- 可以吸收 UART buffer 放大。
- queue 不需要从 0 改成 10，除非实际消费 UART event queue。
- 当前项目要把 `motion=true/false` 上报给 OpenClaw，所以仍应做实体 ping 或读反馈，而不能只看 UART init 成功。
- 启动自动居中不要硬删，应做配置开关；默认可关闭上电乱动，但保留显式 `center` 命令。

## 等待/空闲动作

HtSz 的等待动作不是协议层 waiting 文本，而是舵机定时扫视：

- 周期：4s
- yaw：`-25..25`
- pitch：`25..35`
- move time：`1500ms`

当前项目如果要“看起来活着”，应在 body service 中增加可选 idle scan，而不是依赖启动居中。

## PY32 LED 环

HtSz CoreS3 的 StackChan 灯环走 PY32，不是 ESP32-S3 直接 RMT：

- PY32 地址：`0x6F`
- LED 数量：`12`
- LED config：`0x24`
- LED RAM：`0x30`
- refresh bit：`0x40`
- 颜色格式：RGB565 little-endian，每颗 2 字节

对当前项目的结论：

- 当前 `py32_write_led_rgb()` 的 RGB565 little-endian 方向正确。
- 运行期 LED 写入不能长时间持有 I2C 锁，否则 FT6336 触摸会被饿住。
- LED 呼吸灯应节流 PY32 ring 写入，而不是全局降低所有 LED 通道刷新率。

## FT6336 屏幕触摸

HtSz 参数：

- 地址：`0x38`
- chip ID：`0xA3`
- 数据寄存器：`0x02`
- 读取长度：6 bytes
- 轮询周期：20ms
- 短触摸：500ms
- 双击窗口：500ms
- 滑动阈值：20px
- 单击最大移动：5px
- 长按/摸头：1500ms
- 摸头移动阈值：3px

当前项目协议需要保留：

- `touch`
- `pressure press`
- `pressure hold`
- `pressure release`
- `gesture tap/double_tap/long_press/swipe_*`

因此不能照搬 HtSz 的 `SendUserText()`，只抽取判定参数和轮询节奏。

## SI12T 头部/外壳触摸

HtSz 还有 SI12T 3 区触摸：

- 地址：`0x68`
- I2C 速度：`100kHz`
- 启动校准等待：12s
- 轮询周期：100ms
- 状态寄存器：`0x10`
- 触发冷却：5s

这对应用户说的“身体交互输入”。当前项目可以先把 FT6336 压力事件修好，再把 SI12T 作为可选驱动新增；没有检测到 `0x68` 时必须静默降级。

## qoder 清单校正

- HtSz 符合：PY32 后 200ms、UART buffer 1024、servo power delay 200ms。
- HtSz 不符合：全局 I2C 100k、PY32 300ms、UART queue 10、servo ping 200ms、servo retry 3 次、禁用自动居中。
- 因此 qoder 清单只能作为实验经验，不能作为 HtSz 事实直接合入。
