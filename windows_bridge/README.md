# Windows Bridge

这是 OpenClaw StackChan 的 Windows 侧最小桥接程序。

它的职责是先把 CoreS3 身体接到 Windows，并给 OpenClaw/脚本提供一个稳定的本机控制入口：

- 作为 TCP Server 监听 `8765` 端口。
- 作为本机 HTTP API 监听 `127.0.0.1:8766`。
- 接收 CoreS3 主动连接。
- 打印 `hello`、`heartbeat`、`button`、`touch`、`pressure`、`gesture` 事件。
- 自动回复 `hello_ack`，让固件进入 OpenClaw ready 状态。
- 从命令行向 StackChan 发送表情、灯光、presence 和动作命令。
- 缓存最近事件，方便 OpenClaw 轮询。
- 对触摸压力做简单自动身体反应，可用 `--no-auto-react` 关闭。

它暂时还不是完整 OpenClaw 集成层。后续 ASR、TTS、人脸追踪、长期记忆访问都应接在这个 Bridge 上。

## 运行方式

在 Windows 上安装 Python 3.11 或更新版本，然后运行：

```powershell
python windows_bridge\openclaw_stackchan_bridge.py --host 0.0.0.0 --port 8765
```

常用参数：

```powershell
python windows_bridge\openclaw_stackchan_bridge.py --control-port 8766 --event-log bridge-events.jsonl
python windows_bridge\openclaw_stackchan_bridge.py --no-auto-react
python windows_bridge\openclaw_stackchan_bridge.py --control-host 0.0.0.0 --control-token <本机随机token>
```

控制 API 默认只绑定 `127.0.0.1`。如果改成局域网可访问地址，必须设置 `--control-token` 或环境变量 `OPENCLAW_BRIDGE_TOKEN`，否则 Bridge 会拒绝启动控制面，避免同网段设备直接控制 StackChan。

统一 runtime 入口：

```powershell
python windows_bridge\examples\stackchan_runtime.py --config windows_bridge\config.example.json --status
python windows_bridge\examples\stackchan_runtime.py --config windows_bridge\config.example.json --once "你好"
python windows_bridge\examples\stackchan_runtime.py --config windows_bridge\config.example.json --face-sim
python windows_bridge\examples\stackchan_runtime.py --config windows_bridge\config.example.json --life-demo
python windows_bridge\examples\stackchan_runtime.py --config windows_bridge\config.example.json --tools-list
```

本地真实接入建议复制本地模板：

```powershell
copy windows_bridge\config.local.example.json windows_bridge\config.local.json
copy windows_bridge\local_memory_context.example.json windows_bridge\local_memory_context.json
```

只修改 `config.local.json` 和 `local_memory_context.json`。这些本地文件已被 `.gitignore` 忽略；不要把真实 OpenClaw 地址、token、家庭 IP 或私人记忆提交到仓库。

无硬件一键检查：

```powershell
python windows_bridge\tools\run_no_hardware_checks.py
```

CoreS3 的 `CONFIG_OPENCLAW_TCP_HOST` 需要填写 Windows 电脑在局域网里的 IPv4 地址，例如：

```text
192.168.1.100
```

不要填写 `127.0.0.1`，因为对 CoreS3 来说那是设备自己。

如果 CoreS3 日志显示 `EHOSTUNREACH`，优先检查：

- Bridge 是否用 `--host 0.0.0.0 --port 8765` 启动。
- `CONFIG_OPENCLAW_TCP_HOST` 是否是 Windows 当前局域网 IPv4。
- Windows 防火墙是否允许 Python 监听 8765。
- Windows 和 CoreS3 是否在同一个 WiFi/网段。
- Bridge 启动后看起来“卡住”通常是正常等待连接；可用另一个终端访问 `http://127.0.0.1:8766/status` 验证。

## 手动测试命令

Bridge 启动后可以直接输入：

```text
/status
/ping
/emotion love
/presence listening normal
/presence speaking happy
/look 0 30 400
/motion nod
/motion shake
/led 255 100 50
/breath 0 100 255 3
/beep 880 120 30
```

也可以直接输入原始 JSON：

```json
{"action":"emotion","value":"happy"}
{"action":"look","yaw":10,"pitch":30,"duration_ms":400}
{"action":"motion","gesture":"nod"}
```

## 本机控制 API

Bridge 会默认开启本机 HTTP API：

```text
http://127.0.0.1:8766
```

查询设备状态：

```powershell
curl http://127.0.0.1:8766/status
```

查询最近事件：

```powershell
curl http://127.0.0.1:8766/events?limit=20
```

发送命令：

```powershell
curl -X POST http://127.0.0.1:8766/command -H "Content-Type: application/json" -d "{\"command\":\"/emotion love\"}"
curl -X POST http://127.0.0.1:8766/command -H "Content-Type: application/json" -d "{\"device_command\":{\"action\":\"presence\",\"state\":\"speaking\",\"emotion\":\"happy\"}}"
```

这就是后续 OpenClaw、ASR/TTS、人脸追踪模块优先调用的入口。

## OpenClaw 身体工具

`stackchan_runtime.py --tools-list` 会列出 OpenClaw 可调用的身体工具。低层工具用于精细控制，高层 `self.experience.*` 工具用于常见体验：

- `self.emotion.set`：切换表情。
- `self.presence.set`：切换 listening/thinking/speaking/sleeping 等状态。
- `self.motion.look_at`、`self.motion.nod`、`self.motion.shake`：头部动作。
- `self.led.set_color`、`self.led.breath`：灯光。
- `self.memory.cue`：提示“OpenClaw 记忆上下文已加载”。
- `self.experience.start_speaking`：开始说话状态，自动打开嘴型。
- `self.experience.react_to_touch`：被摸/被安抚时的组合反应。
- `self.experience.sleep_mode`：安静睡眠组合反应。

示例：

```powershell
python windows_bridge\examples\stackchan_runtime.py --config windows_bridge\config.example.json --tool-call self.experience.react_to_touch
python windows_bridge\examples\stackchan_runtime.py --config windows_bridge\config.example.json --tool-call self.motion.look_at --tool-args "{\"yaw\":10,\"pitch\":30}"
```

这些工具仍然通过本机 Bridge 发命令；如果 CoreS3 没有连接，命令会排队或按现有降级逻辑处理。

## OpenClaw 调用示例

仓库里提供了一个轻量 Python client：

```powershell
python windows_bridge\examples\openclaw_body_client.py
python windows_bridge\examples\openclaw_body_client.py --demo
```

其它 OpenClaw/ASR/TTS/视觉模块也可以直接 import：

```python
from windows_bridge.examples.openclaw_body_client import StackChanBodyClient

body = StackChanBodyClient()
body.start_listening()
body.start_speaking("happy")
body.look_at(10, 30)
body.stop_speaking()
```

更完整的事件语义和职责边界见 [`docs/windows-bridge-openclaw-integration.md`](../docs/windows-bridge-openclaw-integration.md)。

代码边界与扩展规则见 [`docs/windows-bridge-code-governance.md`](../docs/windows-bridge-code-governance.md)。

## Resident 对话闭环骨架

`resident_conversation_loop.py` 用键盘输入先模拟 ASR，用可选 HTTP URL 模拟真实 OpenClaw brain，用系统 TTS 模拟播报，同时驱动 StackChan 的 listening/thinking/speaking 表情和动作。

只跑一轮：

```powershell
python windows_bridge\examples\resident_conversation_loop.py --once "你好"
```

进入持续对话：

```powershell
python windows_bridge\examples\resident_conversation_loop.py
```

接一个本地 OpenClaw HTTP 端点：

```powershell
python windows_bridge\examples\resident_conversation_loop.py --openclaw-url http://127.0.0.1:8899/chat
```

没有真实 OpenClaw 时，可以先启动 fake brain：

```powershell
python windows_bridge\tools\fake_openclaw_brain.py --port 8899
```

开启系统 TTS：

```powershell
python windows_bridge\examples\resident_conversation_loop.py --tts
```

这个脚本是接线骨架，不替代真实 ASR、OpenClaw 记忆库或 TTS 服务。真实 ASR 可以接成外部命令：命令把一次识别结果打印到 stdout，Bridge 通过 `ExternalCommandAsr` 读取。真实模块接入后，应保留相同的身体调用顺序：`listening -> thinking -> speaking -> idle`。

如果 `config.example.json` 里设置了 `memory_context_path`，HTTP brain 请求会带上短期 `memory_context`。它只用于 Windows/OpenClaw 侧生成回复，CoreS3 不保存长期记忆。

## 人脸追踪骨架

`face_tracking_loop.py` 把人脸中心坐标转换成安全的 StackChan `look_at(yaw, pitch)` 命令。当前先支持模拟/手动输入，后续 Windows 摄像头或 OpenCV 检测器只需要把人脸位置喂给同一个 `FaceTracker.update()`。

模拟运行，不发送真实命令：

```powershell
python windows_bridge\examples\face_tracking_loop.py --simulate --dry-run
```

使用 Windows 摄像头和可选 OpenCV：

```powershell
pip install opencv-python
python windows_bridge\examples\face_tracking_loop.py --opencv-camera --dry-run
```

手动输入归一化坐标：

```powershell
python windows_bridge\examples\face_tracking_loop.py
```

输入示例：

```text
0.50 0.50 1.0
0.80 0.45 0.9
lost
```

设计边界：

- `x/y` 使用 `0..1`，画面中心是 `0.5/0.5`
- yaw 默认限制在 `-35..35`
- pitch 默认限制在 `12..55`
- 有平滑和限频，避免舵机抖动
- 人脸丢失超时后自动回正

## 身体交互反馈

当前固件会把 CoreS3 触摸屏事件拆成两类身体事件：

- `gesture`：轻按/点击、双击、长按、上下左右滑动。
- `pressure`：接触开始、持续接触、接触结束。

同时固件会额外上报统一的 `body_input`，这是 OpenClaw 优先消费的身体交互输入：

```json
{"event":"body_input","input":"touch","source":"touchscreen","action":"hold","x":120,"y":200,"intensity":80,"intent":"tactile_contact"}
{"event":"body_input","input":"gesture","source":"touchscreen","action":"double_tap","x":120,"y":200,"intensity":60,"intent":"summon"}
{"event":"body_input","input":"motion","source":"imu","action":"shake","intensity":75,"intent":"attention"}
```

触摸派生的 `pressure` 示例：

```json
{"event":"pressure","source":"touchscreen","action":"press","x":120,"y":200,"intensity":40}
{"event":"pressure","source":"touchscreen","action":"hold","x":120,"y":200,"intensity":80}
{"event":"pressure","source":"touchscreen","action":"release","x":120,"y":200,"intensity":0}
```

OpenClaw 可以先把它理解为“被摸到了”：

- `press`：开始接触
- `hold`：持续抚摸/长按
- `release`：接触结束

Bridge 默认会做轻量自动反应：

- `press`：让 StackChan 进入 listening/happy
- `hold`：切到 love/speaking，并尝试点头
- `release`：回到 online_idle/happy

这些只是身体层的临时反射，不替代 OpenClaw 的长期记忆和对话决策。接入真正 OpenClaw 后，可以用 `--no-auto-react` 关闭。

未来如果增加 FSR 压力片、外壳触摸传感器或 IMU 摇晃/拿起检测，仍然复用身体事件语义：接触类事件继续走 `pressure`，动作/姿态类事件可以走 `gesture` 或新增更明确的事件类型；同时把 `source` 改成更具体的位置，例如 `head_fsr`、`body_fsr`、`shell_touch`、`imu`。

## 无硬件模拟验收

先启动 Bridge：

```powershell
python windows_bridge\openclaw_stackchan_bridge.py
```

再开一个终端启动假设备：

```powershell
python windows_bridge\tools\fake_cores3_device.py --host 127.0.0.1 --port 8765
```

你应该能看到：

- Bridge 收到 fake `hello`
- Bridge 自动发送 `hello_ack`
- Bridge 收到 `heartbeat`
- Bridge 收到 `pressure press/hold/release`
- Bridge 因自动反应向假设备发送 presence/motion 命令

本地测试：

```powershell
python -m unittest tests.windows_bridge.test_bridge
```

真机连接诊断：

```powershell
python windows_bridge\tools\bridge_diagnostics.py --url http://127.0.0.1:8766
```

如果 Bridge 是安装器注册的服务，并启用了 token：

```powershell
python windows_bridge\tools\bridge_diagnostics.py --url http://127.0.0.1:8766 --token %OPENCLAW_BRIDGE_TOKEN%
```

诊断会显示：

- Windows 当前可用的局域网 IPv4，固件里的 TCP host 应该填这里面的一个，不要填 `127.0.0.1`。
- 本机 `8765` 是否在监听。
- CoreS3 是否已经 connected。
- CoreS3 上报的 features，例如 `motion`、`touch`、`audio_stream_out`。
- 最近 heartbeat 距离现在多久。
- 最近几行 `windows_bridge\logs\events.ndjson` 事件日志，避免 PowerShell 窗口滚动后看不到 `hello` 和 `heartbeat`。

Bridge 默认会把事件写入：

```text
windows_bridge\logs\events.ndjson
```

如果你想改位置，启动 Bridge 时传入 `--event-log <路径>`，或者设置 `OPENCLAW_EVENT_LOG`。

## WebSocket 家庭 WiFi 通道

Bridge 可以额外启动 WebSocket API，让同一家庭 WiFi 内的 OpenClaw、控制端或调试工具连接到 Windows 电脑：

```powershell
set OPENCLAW_BRIDGE_TOKEN=change-this-token
python windows_bridge\openclaw_stackchan_bridge.py --host 0.0.0.0 --port 8765 --ws-host 0.0.0.0 --ws-port 8767 --control-token %OPENCLAW_BRIDGE_TOKEN%
```

连接地址：

```text
ws://<Windows电脑局域网IP>:8767/bridge?token=change-this-token
```

WebSocket 连接后会收到 `welcome`，其中包含当前设备状态和最近事件。客户端可以发送：

```json
{"type":"ping"}
{"command":"/emotion love"}
{"device_command":{"action":"presence","state":"listening","emotion":"normal"}}
{"action":"motion","gesture":"nod"}
```

Bridge 会把 CoreS3 上报的 `hello`、`heartbeat`、`body_input`、`pressure`、`gesture`、`button` 等事件广播给 WebSocket 客户端。

如果 `--ws-host` 使用 `0.0.0.0`，必须设置 `--control-token` 或 `OPENCLAW_BRIDGE_TOKEN`，避免家庭网络内裸露控制接口。

## MQTT 家庭事件总线

Bridge 也可以连接家庭 MQTT broker，把 CoreS3 身体事件发布到 broker，并从命令 topic 接收控制命令：

```powershell
set OPENCLAW_MQTT_HOST=192.168.1.20
set OPENCLAW_MQTT_TOPIC_PREFIX=openclaw/home
python windows_bridge\openclaw_stackchan_bridge.py --host 0.0.0.0 --port 8765 --mqtt-host %OPENCLAW_MQTT_HOST%
```

默认 topic：

```text
发布事件: openclaw/home/events
订阅命令: openclaw/home/devices/+/command
```

命令 payload 仍然使用同一套 JSON：

```json
{"action":"emotion","value":"love"}
{"action":"presence","state":"listening","emotion":"normal"}
{"device_command":{"action":"motion","gesture":"nod"}}
```

可选环境变量：

- `OPENCLAW_MQTT_PORT`
- `OPENCLAW_MQTT_CLIENT_ID`
- `OPENCLAW_MQTT_TOPIC_PREFIX`
- `OPENCLAW_MQTT_USERNAME`
- `OPENCLAW_MQTT_PASSWORD`

## Edge-TTS 安装测试

Bridge 支持控制 API 里的 TTS 动作：

```json
{"action":"tts","text":"你好","voice":"zh-CN-YunxiNeural"}
```

推荐男声：

- `zh-CN-YunxiNeural`
- `zh-CN-YunjianNeural`

执行逻辑：

- 如果 CoreS3 报告 `audio_stream_out=true`，并且 Windows 有 `ffmpeg`，Bridge 会用 Edge-TTS 生成语音、转成 24kHz `pcm_s16le`，再通过 `audio_stream` 推到 CoreS3 AW88298 I2S。
- 如果设备音频流不可用，Bridge 会退回 Windows 本机播放，同时让 StackChan 进入 speaking 表情。
- 如果 edge-tts 或播放工具不可用，API 会返回 `mode=unavailable`，不会误报成设备已发声。

## 当前接入 OpenClaw 的方式

- OpenClaw 文本输出：调用 Bridge `/command`，发送 `presence`、`emotion`、`motion`、`tts` 等动作。
- Windows 麦克风输入：Bridge 侧 ASR source 负责把语音转 transcript，再交给 OpenClaw brain adapter。
- TTS：OpenClaw 或本地会话循环发送 `{"action":"tts","text":"..."}`，Bridge 使用 Edge-TTS 男声处理。
- 人脸追踪：Windows 摄像头/OpenCV 适配器识别人脸位置，再通过 Bridge 发送 `look`。
- 长期记忆：由 OpenClaw/Windows 读取和注入短期 `memory_context`，CoreS3 不保存长期记忆。

需要真机验收的硬件点：

- CoreS3 speaker 是否能成功初始化 AW88298/I2S，并上报 `audio_stream_out=true`。
- StackChan 舵机供电链路是否可用，并上报 `motion=true`。
- 触摸事件是否从 FT6336 正常上报为 `pressure` 和 `gesture`。
