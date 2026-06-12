# OpenClaw StackChan CoreS3

让 **OpenClaw 成为大脑，StackChan/CoreS3 成为身体**。

这个仓库包含两部分：

- **CoreS3 固件**：纯 C / ESP-IDF v5.x，运行在 M5Stack CoreS3 上，负责表情、灯光、触摸/手势/身体交互、按键、心跳、舵机动作和可选 beep。
- **Windows Bridge**：运行在 OpenClaw 所在的 Windows 电脑上，接收 CoreS3 TCP 连接，并给 OpenClaw、ASR、TTS、人脸追踪模块提供本机控制 API。

它的目标不是把长期记忆塞进设备，而是让 OpenClaw 带着 Windows 电脑里的记忆“住进”StackChan：OpenClaw 思考、记忆、说话；StackChan 显示表情、转头、发光、感知触摸，并把身体事件送回 OpenClaw。

## 亮点功能

- **OpenClaw 大脑 / StackChan 身体分层**：OpenClaw 负责记忆、人格、ASR/TTS、人脸识别和决策；CoreS3 只做身体表现和短期状态。
- **纯 ESP-IDF 固件**：不用 Arduino，不用 C++，适配 M5Stack CoreS3 / ESP32-S3。
- **稳定 JSON 协议**：TCP 和串口都使用“一行一个 JSON”，方便调试和扩展。
- **StackChan 表情与 presence 状态**：支持 8 个表情，以及 listening、thinking、speaking、sleeping、online idle 等身体状态。
- **身体交互反馈**：CoreS3 触摸屏会识别点击、双击、长按、滑动，并派生 `pressure press/hold/release`，OpenClaw 可以知道“被摸到了”。
- **动作与人脸追踪接口**：支持安全 yaw/pitch `look` 命令和 nod/shake/tilt/center 动作；Windows 侧提供可选 OpenCV 摄像头适配。
- **语音对话骨架**：Windows 侧提供可插拔 ASR 输入、HTTP OpenClaw brain adapter、系统 TTS wrapper 和说话状态同步。
- **OpenClaw 身体工具层**：提供 `self.emotion.*`、`self.motion.*`、`self.led.*`、`self.experience.*` 等可调用工具，避免上层拼低级命令。
- **记忆边界清晰**：Windows/OpenClaw 可传短期 `memory_context`，CoreS3 不保存长期记忆、事实、总结或 embedding。
- **无硬件验证**：提供 fake CoreS3、fake OpenClaw brain 和一键 no-hardware check，方便贡献者先跑通桥接层。

稳定协议文档：[`docs/openclaw-stackchan-protocol.md`](docs/openclaw-stackchan-protocol.md)

协议 JSON Schema：[`docs/schema/openclaw-stackchan-message.schema.json`](docs/schema/openclaw-stackchan-message.schema.json)

Windows Bridge 说明：[`windows_bridge/README.md`](windows_bridge/README.md)

发布检查清单：[`docs/release-readiness-checklist.md`](docs/release-readiness-checklist.md)

硬件验收日志：[`docs/hardware-validation-log.md`](docs/hardware-validation-log.md)

代码治理规则：[`docs/windows-bridge-code-governance.md`](docs/windows-bridge-code-governance.md)

## 项目目标

本项目的目标是让 **OpenClaw 成为大脑，StackChan 成为身体**。

- OpenClaw / Windows 侧负责长期记忆、会话、人格、ASR、TTS、人脸识别和决策。
- CoreS3 / StackChan 侧负责表情、灯光、触摸/手势/身体交互事件、按键、心跳和舵机动作。
- 设备端只保存短期会话状态，不保存长期记忆、事实、总结或 embedding。
- 身体交互反馈第一版来自 CoreS3 触摸屏：轻按/点击、双击、长按、滑动会作为 `gesture` 上报；按下、持续接触、松开会作为 `pressure` 上报。后续可叠加外壳触摸、FSR 压力片、IMU 摇晃/拿起等传感器，但对 OpenClaw 仍保持统一的身体事件语义。

## 当前能力

### CoreS3 固件

- WiFi 自动连接与断线重连
- TCP Client 自动连接 OpenClaw，断线后每 5 秒重试
- UART0 CDC 串口调试与命令输入
- ILI9341 LCD 全屏表情显示
- Stackchan 风格表情：`happy`、`normal`、`sad`、`angry`、`surprised`、`sleepy`、`shy`、`love`
- SK6812 / NeoPixel 灯光控制与呼吸灯
- 触摸与手势：轻按/点击、双击、长按、上下左右滑动
- 身体交互反馈：触摸派生 `press`、`hold`、`release`，后续可扩展外壳压力和摇晃/拿起事件
- A/B/C 按键事件上报
- OpenClaw resident presence 状态层
- 短期 `memory_context` 接入，不在设备端保存长期记忆
- 可选 PY32 LED 环与舵机供电
- 可选 Stackchan yaw/pitch 舵机动作
- 可选实验 speaker beep
- MCP 风格 JSON-RPC 工具发现与调用

### Windows Bridge / OpenClaw 接入层

- CoreS3 TCP server 和本机 HTTP 控制 API
- 家庭 WiFi WebSocket API：远程客户端可订阅身体事件并发送设备命令
- 最近身体事件缓存与轮询
- hello/heartbeat/pressure/gesture/button 事件处理
- OpenClaw 可调用身体工具：表情、presence、LED、动作、beep、sleep、memory cue
- 高层体验工具：开始说话、触摸回应、睡眠模式
- 生命周期管理：睡眠/唤醒、主动回应、状态摘要
- 对话循环骨架：ASR transcript source -> OpenClaw brain -> TTS -> StackChan presence
- 可插拔 ASR：键盘模拟和外部命令输入
- HTTP OpenClaw brain adapter 和 fake brain
- Windows camera / OpenCV 人脸检测适配器
- 语音时长估算和说话嘴型状态 cue
- 本机控制 API token 保护和事件日志脱敏

## 快速开始

如果你只是想先看 OpenClaw/StackChan 桥接层能不能跑，不需要 CoreS3：

```powershell
python windows_bridge\tools\run_no_hardware_checks.py
```

如果你要接自己的 OpenClaw：

```powershell
copy windows_bridge\config.local.example.json windows_bridge\config.local.json
copy windows_bridge\local_memory_context.example.json windows_bridge\local_memory_context.json
```

然后只修改 `config.local.json` 和 `local_memory_context.json`。这些本地文件已被 `.gitignore` 忽略，不要提交真实 OpenClaw 地址、token、家庭 IP 或私人记忆。

如果你要接真机：

1. 在 Windows 上启动 Bridge。
2. 在 ESP-IDF `menuconfig` 中配置 WiFi 和 Windows 电脑局域网 IP。
3. 编译并烧录 CoreS3 固件。
4. 用串口或 Windows Bridge 查看 `hello`、`heartbeat`、`pressure` 等事件。

详细步骤见下方“构建方式”和 [`windows_bridge/README.md`](windows_bridge/README.md)。

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

## 贡献与治理

欢迎基于 GPL-3.0 继续改造。为了保持项目可维护，请遵守这些边界：

- 不要把真实 WiFi、家庭 IP、token、私有记忆文件提交到仓库。
- 新增设备命令时，先更新 [`docs/openclaw-stackchan-protocol.md`](docs/openclaw-stackchan-protocol.md)，再改固件和 Windows Bridge。
- 涉及协议变化时，同时更新 [`docs/schema/openclaw-stackchan-message.schema.json`](docs/schema/openclaw-stackchan-message.schema.json) 和 [`docs/examples/protocol-valid-messages.jsonl`](docs/examples/protocol-valid-messages.jsonl)。
- Windows 侧可复用逻辑放在 `windows_bridge/openclaw_bridge/`，示例脚本只做薄入口。
- 长期记忆永远留在 OpenClaw/Windows 侧；CoreS3 只保存短期 resident/session 状态。
- 能无硬件测试的能力，请补到 `tests/windows_bridge/` 并让 `run_no_hardware_checks.py` 覆盖。
- 真机发布前，请填写 [`docs/hardware-validation-log.md`](docs/hardware-validation-log.md)。

更完整的治理规则见：

- [`docs/windows-bridge-code-governance.md`](docs/windows-bridge-code-governance.md)
- [`docs/protocol-governance.md`](docs/protocol-governance.md)

## 许可证

本项目使用 GPL-3.0 许可证，详见 [`LICENSE`](LICENSE)。

这意味着别人可以基于你的改造继续开发和分发；如果他们公开分发修改后的版本，也需要按 GPL-3.0 的要求开放对应源码并保留许可证说明。
