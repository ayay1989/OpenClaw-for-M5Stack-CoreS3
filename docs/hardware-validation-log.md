# 硬件验收日志

本文件用于记录真实 M5Stack CoreS3 / StackChan 硬件验证结果。仓库测试只能证明 Windows Bridge 和协议样本可跑；真正发布固件前，还需要在设备上完成本日志。

## 当前状态

- 验收状态：未完成
- 最近一次完整硬件验收：无
- 最近一次无硬件验证：见 GitHub commit 对应测试输出
- 目标硬件：M5Stack CoreS3 / ESP32-S3
- 固件框架：ESP-IDF v5.x

## 验收记录模板

复制下面模板追加新记录，不要覆盖旧记录。

```text
日期：
测试人：
Git commit：
ESP-IDF 版本：
CoreS3 硬件版本：
是否接 PY32：
是否接 yaw/pitch 舵机：
是否启用音频：
Windows Bridge 运行机器：
OpenClaw 端点：

结果摘要：
- [ ] idf.py set-target esp32s3 成功
- [ ] idf.py build 成功
- [ ] idf.py flash monitor 成功
- [ ] 开机显示 happy 表情
- [ ] 开机蓝色呼吸灯
- [ ] 串口输出 hello
- [ ] 串口输出 heartbeat
- [ ] WiFi 自动连接
- [ ] TCP 连接 Windows Bridge
- [ ] TCP 断开后 5 秒重试
- [ ] WiFi 断开后自动重连
- [ ] 串口 JSON 命令可用
- [ ] TCP JSON 命令可用
- [ ] 8 个表情显示正确
- [ ] LED 固定颜色正确
- [ ] LED 呼吸灯正确
- [ ] A/B/C 按键 press/release 正确
- [ ] 触摸 touch 事件正确
- [ ] pressure press/hold/release 正确
- [ ] tap/double_tap/long_press/swipe 手势正确
- [ ] presence listening/thinking/speaking/sleeping 正确
- [ ] memory_context 只改变短期状态，不落盘保存长期记忆
- [ ] 未接舵机时 motion 返回 motion unavailable 且不重启
- [ ] 接舵机时 center/nod/shake/tilt/look 动作安全
- [ ] 未启用音频时 beep 返回 audio unavailable
- [ ] 启用音频时 beep 可听或给出明确错误
- [ ] Windows Bridge no-hardware check 通过
- [ ] Windows Bridge 真机事件可见
- [ ] OpenClaw 可以发送表情/动作/说话状态
- [ ] OpenClaw 可以接收 pressure/button/heartbeat 事件

问题记录：
1.

结论：
- [ ] 可发布
- [ ] 暂不可发布，需修复上述问题
```

## 已知未完成项

- 尚未在本仓库中记录真实 CoreS3 build/flash 结果。
- 尚未记录真实触摸、舵机、音频和 OpenClaw 端到端验证。
- 真实 ASR、TTS、摄像头和 OpenClaw 记忆库接入依赖本地 Windows 环境。
