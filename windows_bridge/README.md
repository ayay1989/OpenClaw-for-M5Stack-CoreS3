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
```

CoreS3 的 `CONFIG_OPENCLAW_TCP_HOST` 需要填写 Windows 电脑在局域网里的 IPv4 地址，例如：

```text
192.168.1.100
```

不要填写 `127.0.0.1`，因为对 CoreS3 来说那是设备自己。

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

## 压力反馈

当前固件会把 CoreS3 触摸屏事件派生成压力事件：

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

未来如果增加 FSR 压力片或外壳触摸传感器，仍然复用 `pressure` 事件，只需要把 `source` 改成更具体的位置，例如 `head_fsr`、`body_fsr`。

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

## 后续接 OpenClaw

建议下一步在这个 Bridge 中增加：

- OpenClaw 会话适配器：调用本机 `/command` API，把 OpenClaw 输出转成 `presence`、`emotion`、`look`、`motion`。
- ASR 输入：Windows 麦克风转文字。
- TTS 输出：第一版先用 Windows 播放，同时让 StackChan 做 speaking 表情和动作。
- 摄像头人脸追踪：Windows 摄像头识别人脸位置，再发 `look` 给 StackChan。
- 长期记忆：由 OpenClaw/Windows 访问，CoreS3 不保存记忆。
