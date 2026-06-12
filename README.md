# OpenClaw Stackchan CoreS3 固件

这是一个面向 **M5Stack CoreS3 / ESP32-S3** 的 ESP-IDF v5.x 固件，用来把 OpenClaw 接入 Stackchan 风格的桌面设备。

它通过 WiFi 作为 TCP Client 连接 OpenClaw，同时保留 UART0 CDC 串口调试；串口和 TCP 都使用“一行一个 JSON”的协议。设备可以显示表情、控制灯光、上报触摸/按键/心跳，并在硬件存在时控制 Stackchan 的头部舵机。

稳定协议文档：[`docs/openclaw-stackchan-protocol.md`](docs/openclaw-stackchan-protocol.md)

## 项目目标

本项目的目标是让 **OpenClaw 成为大脑，StackChan 成为身体**。

- OpenClaw / Windows 侧负责长期记忆、会话、人格、ASR、TTS、人脸识别和决策。
- CoreS3 / StackChan 侧负责表情、灯光、触摸/压力事件、按键、心跳和舵机动作。
- 设备端只保存短期会话状态，不保存长期记忆、事实、总结或 embedding。
- 触摸压力反馈第一版来自 CoreS3 触摸屏：按下、长按、松开都会上报 `pressure` 事件；后续可替换或叠加外壳压力传感器。

## 当前能力

- WiFi 自动连接与断线重连
- TCP Client 自动连接 OpenClaw，断线后每 5 秒重试
- UART0 CDC 串口调试与命令输入
- ILI9341 LCD 全屏表情显示
- Stackchan 风格表情：`happy`、`normal`、`sad`、`angry`、`surprised`、`sleepy`、`shy`、`love`
- SK6812 / NeoPixel 灯光控制与呼吸灯
- 触摸事件与手势：点击、双击、长按、上下左右滑动
- 触摸派生压力反馈：`press`、`hold`、`release`
- A/B/C 按键事件上报
- OpenClaw resident presence 状态层
- 短期 `memory_context` 接入，不在设备端保存长期记忆
- 可选 PY32 LED 环与舵机供电
- 可选 Stackchan yaw/pitch 舵机动作
- 可选实验 speaker beep
- MCP 风格 JSON-RPC 工具发现与调用

## 硬件目标

目标硬件：M5Stack CoreS3 / ESP32-S3

本项目参考并校准了 `mo-hantang/Stackchan-HtSz` 中的 CoreS3 板级配置。当前默认硬件参数如下：

| 模块 | 配置 |
| --- | --- |
| 内部 I2C | SDA GPIO12, SCL GPIO11 |
| LCD SPI | MOSI GPIO37, SCLK GPIO36, CS GPIO3, DC GPIO35 |
| PMIC / 背光 | AXP2101, I2C 地址 `0x34` |
| IO 扩展 / LCD reset / AW88298 reset | AW9523, I2C 地址 `0x58` |
| 触摸 | FT6336, I2C 地址 `0x38` |
| 外接 NeoPixel / SK6812 | GPIO45 |
| PY32 helper MCU | I2C 地址 `0x6F` |
| 舵机总线 | UART1, TX GPIO6, RX GPIO7, 1 Mbps |
| yaw 舵机 | ID `1`, 安全范围 `-45..45` |
| pitch 舵机 | ID `2`, 安全范围 `5..60` |
| 音频输出 | 实验功能，I2S0: WS GPIO33, BCLK GPIO34, DOUT GPIO13 |
| 音频 MCLK | 可选 GPIO0，会与 Button C / BOOT 引脚冲突 |

Button C 使用 GPIO0。默认情况下固件把它当作防抖后的普通按键处理；如果开启 `CONFIG_OPENCLAW_AUDIO_USE_GPIO0_MCLK`，Button C 会被禁用，避免与音频 MCLK 冲突。

## 构建方式

需要 ESP-IDF v5.x。

```powershell
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py flash monitor
```

配置入口在 `menuconfig` 的 `OpenClaw Stackchan` 菜单下：

- `CONFIG_OPENCLAW_WIFI_SSID`
- `CONFIG_OPENCLAW_WIFI_PASSWORD`
- `CONFIG_OPENCLAW_TCP_HOST`
- `CONFIG_OPENCLAW_TCP_PORT`
- `CONFIG_OPENCLAW_AUDIO_ENABLE`
- `CONFIG_OPENCLAW_AUDIO_USE_GPIO0_MCLK`

请不要把真实 WiFi 密码、家庭 IP、token 或其它敏感信息提交到仓库。默认配置使用占位值。

`CONFIG_OPENCLAW_TCP_HOST` 必须是 CoreS3 在同一网络中能访问到的 IPv4 地址。不要填 `127.0.0.1`，因为在设备上它指向 CoreS3 自己。

## Windows Bridge

CoreS3 是 TCP Client，需要 Windows 侧先启动一个 Bridge 监听端口。仓库内提供了一个最小版本：

```powershell
python windows_bridge\openclaw_stackchan_bridge.py --host 0.0.0.0 --port 8765
```

然后在固件配置中把 `CONFIG_OPENCLAW_TCP_HOST` 填成 Windows 电脑的局域网 IPv4 地址。

Bridge 当前用于 M1 连通验收：接收 `hello`、`heartbeat`、`button`、`touch`、`pressure`、`gesture`，并可以手动发送表情、灯光、presence 和动作命令。

详细说明见 [`windows_bridge/README.md`](windows_bridge/README.md)。

## 默认运行行为

- 上电默认显示 `happy` 表情
- 上电默认蓝色呼吸灯
- 每 10 秒上报一次 heartbeat
- WiFi 断开后自动重连
- TCP 断开后每 5 秒重连
- TCP 不在线时，本地表情、灯光、触摸、按键、motion 继续工作
- 未检测到 PY32 / 舵机时，motion 自动降级并返回 `motion unavailable`
- 未启用或未初始化音频时，`beep` 返回 `audio unavailable`

## 快速串口测试

串口和 TCP 使用同一套 JSON 协议，每行一个 JSON，以 `\n` 结尾。

```json
{"action":"ping"}
{"action":"emotion","value":"love"}
{"action":"led","r":255,"g":100,"b":50}
{"action":"led_effect","effect":"breath","r":0,"g":200,"b":255,"speed":3}
{"action":"presence","state":"listening","emotion":"normal"}
{"action":"presence","state":"thinking","emotion":"sleepy"}
{"action":"memory_cue","emotion":"love","ttl_ms":3000}
{"action":"presence","state":"speaking","emotion":"happy","mouth":true}
{"action":"sleep","enabled":true}
{"action":"look","yaw":10,"pitch":30,"duration_ms":400}
{"action":"motion","gesture":"center"}
{"action":"motion","gesture":"nod"}
{"action":"motion","gesture":"shake"}
{"action":"motion","gesture":"tilt"}
{"action":"beep","freq":880,"duration_ms":120,"volume":30}
```

## MCP 风格调用

固件仍然以 newline-delimited JSON 为主协议，同时支持轻量 MCP 风格 JSON-RPC 包装，方便 OpenClaw 发现设备能力。

```json
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"initialize","id":1}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/list","id":2}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.emotion.set","arguments":{"value":"love"}},"id":3}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.led.breath","arguments":{"r":0,"g":200,"b":255,"speed":3}},"id":4}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.presence.set","arguments":{"state":"speaking","emotion":"happy"}},"id":5}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.motion.look_at","arguments":{"yaw":-15,"pitch":32,"duration_ms":350}},"id":6}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.motion.nod","arguments":{}},"id":7}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.audio.beep","arguments":{"frequency_hz":880,"duration_ms":120,"volume":30}},"id":8}}
```

`tools/list` 会根据运行时硬件状态返回工具列表：

- 基础工具总是可见
- motion 工具只在舵机初始化成功后可见
- audio 工具只在实验音频输出初始化成功后可见

## OpenClaw Resident 与记忆边界

设备支持短期 resident context：

```json
{"type":"memory_context","session_id":"demo-session","resident_id":"openclaw","ttl_ms":3600000}
```

设备只保存短期 `session_id`、`resident_id` 和 memory context 状态标志。长期记忆、事实、总结、embedding 或用户隐私数据仍由 OpenClaw 管理，固件不会写入 Flash 保存。

如果上层发送 `summary`、`facts` 等字段，当前固件会忽略其内容。

## 验收建议

基础验收：

1. 开机后显示 `happy` 表情和蓝色呼吸灯。
2. 串口可见 WiFi / TCP / hello / heartbeat 日志。
3. `{"action":"ping"}` 返回 `pong`。
4. 表情、灯光、presence 命令均可执行。
5. 断开 TCP 后设备继续本地运行，并每 5 秒重试连接。
6. 触摸屏幕时能看到 `touch` 和 `pressure` 事件，长按时能看到 `pressure.action=hold`。

motion 验收：

1. 未接 PY32 / 舵机时，设备不重启，motion 返回 `motion unavailable`。
2. 接入 PY32 / 舵机后，`hello.features.motion=true`。
3. `center`、`nod`、`shake`、`tilt`、`look` 动作安全可见。
4. 双击触摸进入 listening，并在 motion 可用时轻微点头。
5. 长按触摸进入 sleep / wake，并在 motion 可用时低头。

audio 验收：

1. 默认 `CONFIG_OPENCLAW_AUDIO_ENABLE=n`，`hello.features.audio_out=false`。
2. 开启 `CONFIG_OPENCLAW_AUDIO_ENABLE=y` 后，确认 I2S 输出初始化日志。
3. 先保持 `CONFIG_OPENCLAW_AUDIO_USE_GPIO0_MCLK=n`。
4. 如硬件需要 MCLK，再开启 GPIO0 MCLK，同时确认 Button C 被禁用。
5. `{"action":"beep","freq":880,"duration_ms":120,"volume":30}` 发出短音或返回明确的 `audio unavailable`。

v1.0 完整验收清单见协议文档末尾。

## 当前不包含的功能

- WebSocket
- MQTT
- TTS 流式播放
- 麦克风输入
- 唤醒词
- 回声消除
- 摄像头
- OTA
- 设备端长期记忆存储

## 许可证

本项目使用 GPL-3.0 许可证，详见 [`LICENSE`](LICENSE)。

这意味着别人可以基于你的改造继续开发和分发；如果他们公开分发修改后的版本，也需要按 GPL-3.0 的要求开放对应源码并保留许可证说明。
