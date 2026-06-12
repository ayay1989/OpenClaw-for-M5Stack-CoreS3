# Windows Bridge

这是 OpenClaw StackChan 的 Windows 侧最小桥接程序。

它的职责是先把 CoreS3 身体接到 Windows：

- 作为 TCP Server 监听 `8765` 端口。
- 接收 CoreS3 主动连接。
- 打印 `hello`、`heartbeat`、`button`、`touch`、`pressure`、`gesture` 事件。
- 自动回复 `hello_ack`，让固件进入 OpenClaw ready 状态。
- 从命令行向 StackChan 发送表情、灯光、presence 和动作命令。

它暂时还不是完整 OpenClaw 集成层。后续 ASR、TTS、人脸追踪、长期记忆访问都应接在这个 Bridge 上。

## 运行方式

在 Windows 上安装 Python 3.11 或更新版本，然后运行：

```powershell
python windows_bridge\openclaw_stackchan_bridge.py --host 0.0.0.0 --port 8765
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

未来如果增加 FSR 压力片或外壳触摸传感器，仍然复用 `pressure` 事件，只需要把 `source` 改成更具体的位置，例如 `head_fsr`、`body_fsr`。

## 后续接 OpenClaw

建议下一步在这个 Bridge 中增加：

- OpenClaw 会话适配器：把 OpenClaw 的输出转成 `presence`、`emotion`、`look`、`motion`。
- ASR 输入：Windows 麦克风转文字。
- TTS 输出：第一版先用 Windows 播放，同时让 StackChan 做 speaking 表情和动作。
- 摄像头人脸追踪：Windows 摄像头识别人脸位置，再发 `look` 给 StackChan。
- 长期记忆：由 OpenClaw/Windows 访问，CoreS3 不保存记忆。
