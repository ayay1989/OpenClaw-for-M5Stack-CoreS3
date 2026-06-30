# Stackchan-HtSz 功能抽取与失败原因对照

更新时间：2026-06-30

本文件用于保存 `mo-hantang/Stackchan-HtSz` 中对 CoreS3 / StackChan 有复用价值的代码位置、启动逻辑和硬件事实，并对照当前纯 C 固件分析“网络正常但灯、舵机、触摸、声音仍失败”的可能原因。

注意：本文件是硬件移植证据，不是项目进度总表。项目进度仍以 `docs/openclaw-stackchan-roadmap.md` 为唯一权威视图。

## 参考源

- 仓库：`https://github.com/mo-hantang/Stackchan-HtSz`
- 本次只读克隆位置：`/tmp/Stackchan-HtSz`
- 本次参考 commit：`0aa7d9c`
- 主要文件：
  - `main/boards/m5stack-core-s3/m5stack_core_s3.cc`
  - `main/boards/m5stack-core-s3/config.h`
  - `main/boards/m5stack-core-s3/cores3_audio_codec.cc`
  - `main/boards/m5stack-core-s3/SCSCL.*`
  - `main/boards/m5stack-core-s3/SCSerial.*`

HtSz 是 C++/LVGL/板级大类写法；本项目目标是 ESP-IDF v5.x 纯 C。因此适合抽取硬件事实、初始化顺序、寄存器和参数，不适合整段搬入。

## HtSz 模块启动逻辑

HtSz 的 `M5StackCoreS3Board()` 启动链可以简化为：

```text
InitializePowerSaveTimer()
InitializeI2c()
InitializeAxp2101()
InitializeAw9523()
I2cDetect()
EnableServoPowerViaPy32()
if PY32 found:
    wait 200ms
    servo_.Begin()
    InitializePy32LedDevice()
    RegisterLedMcpTools()
InitializeSpi()
InitializeIli9342Display()
InitializeCamera()
bind avatar -> servo / camera / led updater
InitializeFt6336TouchPad()
InitializeBmi270()
InitializeSi12T()
InitializeMorningGreeting()
delayed init-status log
```

关键点：

1. 板级电源链优先：I2C、AXP2101、AW9523、PY32 在 LCD/触摸/舵机/灯环之前完成。
2. PY32 成功后才打开舵机 VM_EN、启动舵机、启动 PY32 LED 环。
3. FT6336 是屏幕触摸；SI12T 才是头部/外壳触摸。
4. HtSz 会打印 PY32、Servo、Camera 的启动状态，便于现场判断身体硬件是否真的可用。

## 逐功能可用代码抽取

| 功能 | HtSz 可用代码位置 | 可抽取逻辑 | 当前项目对应 | 失败风险判断 |
|---|---|---|---|---|
| 内部 I2C | `m5stack_core_s3.cc::InitializeI2c`、`config.h` | `I2C_NUM_1`，SDA `GPIO12`，SCL `GPIO11`，internal pullup，glitch ignore 7；默认 400kHz，PY32/SI12T/camera 用 100kHz | `main/main.c::i2c_init_internal`、`main/i2c_bus.c` | 当前已对齐 HtSz。若现场 I2C scan 看不到 `0x34/0x58/0x6F/0x38`，优先怀疑硬件版本、接线或 I2C 总线被占用。 |
| PMIC 电源 | `Pmic`、`InitializeAxp2101` | AXP2101 `0x34`，写 `0x90/0x99/0x97/0x69/0x30/0x94/0x95` | `main/main.c::i2c_init_internal` | 当前已引入。早期 IP5306 判断不适合作为 CoreS3 主路径。 |
| AW9523 reset | `Aw9523`、`ResetAw88298`、`ResetIli9342` | AW9523 `0x58`；负责 AW88298 功放 reset 和 LCD reset | `main/main.c::i2c_init_internal` | 当前已引入。若音频无声，不能只查 I2S，还要确认 AW9523 reset 和 AW88298 codec 初始化。 |
| PY32 舵机电源 | `EnableServoPowerViaPy32` | PY32 `0x6F`；每次先等 200ms，最多 10 次；读版本寄存器 `0x02`；成功后 `0x03/0x09/0x05` bit0 置 1 | `main/py32driver.c::py32_init`、`py32_set_servo_power` | 当前已补齐每次 probe 前 200ms 等待，并保留 10 次 retry；真机仍需确认 `py32_available` 是否稳定。 |
| PY32 LED 环 | `InitializePy32LedDevice`、`Py32SetLedFrame`、`UpdateLedsFromEmotion` | 12 颗 LED；`REG_LED_CFG=0x24`；`REG_LED_RAM=0x30`；RGB565 little-endian；刷新位 `0x40` | `main/py32driver.c`、`main/leddriver.c` | 寄存器和格式已对齐。风险是当前仍保留 GPIO/RMT LED 路径，GPIO 写成功不代表真实灯环亮；验收应看 `py32_led_write_ok`。 |
| 舵机 | `StackChanServo::Begin/MoveTo/Nod/Shake/Tilt`、`SCSCL.*` | UART1；TX/RX `GPIO6/GPIO7`；1Mbps；yaw ID 1；pitch ID 2；yaw `460 + deg*16/5`；pitch `620 + deg*16/5`；HtSz 不 ping，直接 move | `main/scservo_bus.*`、`main/servodriver.c`、`main/body_service.c` | 引脚、速率、位置映射已对齐。当前增加 ping 能真实上报可用性，但如果 ping 时序不适配，可能出现“VM_EN 已开但 motion=false”。需要记录 UART 写是否实际成功。 |
| 空闲动作 | `IdleScanCb` | 每 4 秒扫视，yaw `-25..25`，pitch `25..35`，移动 `1500ms` | 尚无等价 idle scan | 这解释“不会转头/没有活着感”的一部分。即使舵机可用，当前也不会自动像 HtSz 那样等待扫视。 |
| FT6336 屏幕触摸 | `Ft6336`、`InitializeFt6336TouchPad`、`PollTouchpad` | 地址 `0x38`；chip id `0xA3`；从 `0x02` 读 6 bytes；20ms 轮询；tap/double/swipe/long 参数可复用 | `main/main.c::touch_task`、`main/body_events.c` | 当前 20ms 轮询和事件形态已对齐。若用户摸的是机器人外壳/头顶，不会触发 FT6336，这是预期。 |
| SI12T 头部/外壳触摸 | `InitializeSi12T`、`Si12tLoop` | 地址 `0x68`；100kHz；初始化 `0x0A..0x0F/0x09/0x08/0x02..0x07`；等待 12s；100ms 轮询 `0x10`；5s 冷却 | `main/si12tdriver.c`、`main/body_events.c` | 已新增可选 SI12T 驱动，检测不到 `0x68` 时降级；真机还需验证是否能触发 `source=head_si12t` 的 `body_input/pressure`。 |
| BMI270 摇晃/拿起 | `InitializeBmi270`、`MotionLoop` | BMI270 `0x69`；100ms 读取；检测 shake/lift/still；有 5 分钟冷却 | 当前未实现 | 用户期望“摇晃、触摸、轻按、长按”时，摇晃/拿起还缺真实 IMU 输入。 |
| 表情/头像 | `LvglAvatar`、`M5StackAvatarDisplay`、`SetEmotion`、`SetChatMessage` | LVGL 动态头像，眨眼、呼吸、overlay、说话嘴型；情绪多于 8 个 | `main/visuals.c`、`main/emotions_data.c`、`main/lcddriver.c` | 当前是纯 C 静态/生成式表情，不是 HtSz 原生 LVGL avatar，所以“像苹果 emoji / 只有静态感”的反馈合理。 |
| 音频输出 | `cores3_audio_codec.cc`、`Aw9523::ResetAw88298` | I2S0；24kHz；MCLK `GPIO0`；WS `GPIO33`；BCLK `GPIO34`；DOUT `GPIO13`；AW88298 codec；输出需要 codec 控制层 | `main/audiodriver.c` | 当前只是实验 I2S TX/beep/PCM，默认关闭；没有完整 AW88298 codec dev 链。不能期待烧录后自然有声音。 |
| 摄像头/人脸跟踪 | `InitializeCamera`、`FaceTracker` | DVP camera；GPIO39/40/41/42/15/16/48/47/46/38/45；face tracker 驱动舵机 | Windows bridge face tracking scaffold；固件只支持 `look` | 当前 CoreS3 上板摄像头未接入；而且 HtSz camera D2/D3 使用 GPIO41/42，和早期 A/B 按键设定冲突。 |

## 与当前固件的关键差异

### 1. “身体硬件可用”与“网络会话可用”是两条链

当前固件会在 PY32、舵机、音频不可用时继续启动 LCD、WiFi、TCP。这是对 OpenClaw 会话友好的降级策略，但也会造成现场观感：

- TCP 已连接，屏幕正常；
- 但 `servo=false`、`motion=false`、`audio=false`；
- 灯环如果只走 PY32，GPIO LED 路径成功也不等于物理灯环亮；
- 机器人看起来“没住进去”。

因此日志必须同时看：

- `PY32 initialized` / `PY32 not available`
- `servo VM_EN power enabled`
- `servos detected` 或 `servo ping failed`
- `py32_led_write_ok`
- `FT6336 touch initialized`
- `audio disabled` 或 `I2S speaker TX initialized`

### 2. 触摸屏 pressure 不等于摸头/外壳触摸

当前 `pressure` 可以来自 FT6336 屏幕触摸，也可以来自可选 SI12T 头部/外壳触摸。FT6336 可以表达屏幕按下、长按、松开；SI12T 用 `source=head_si12t` 表达摸头/外壳触摸。

HtSz 的身体触摸来自 SI12T `0x68`。当前已新增可选 SI12T 驱动，并把事件归一到 `body_input`，例如：

```json
{"event":"body_input","input":"touch","source":"head_si12t","action":"pet","intent":"comfort"}
```

### 3. 舵机不动可能不是代码命令缺失，而是可用性判定卡住

HtSz 的舵机初始化不做 ping，直接写位置；当前项目为了真实上报 `motion_available` 加了 ping 和降级。这个方向是对的，但如果 SCServo 对 ping 响应不稳定，就可能出现：

- VM_EN 已经打开；
- UART 引脚和速率正确；
- 但 ping 失败导致 `motion=false`；
- 后续动作命令被拒绝或被标记不可用。

后续排查应区分“ping 不通”和“write position 不动”。这两者不能混在一个 `servo=false` 里。

### 4. HtSz 原生表情不是 8 张静态图

HtSz 的表情系统是 LVGL avatar：眼睛、嘴、overlay、眨眼、呼吸、说话嘴型都会动。当前项目为了纯 C 和低依赖，做的是静态/生成式表情集合。它能满足协议，但不等价于 HtSz 的“原生 StackChan 表情”。

如果目标是更像 HtSz，下一步不是继续加静态 emoji，而是做一个纯 C avatar renderer，至少补齐：

- blink 眨眼；
- speaking 嘴型动画；
- loving/embarrassed/sleepy overlay；
- idle 呼吸或微动。

### 5. 音频仍是实验路径

HtSz 音频是 codec 管线，不只是 I2S 写 PCM。当前项目默认关闭 `CONFIG_OPENCLAW_AUDIO_ENABLE`，并且没有完整 ES7210/AW88298 codec 控制层。

因此“没有声音”目前不能当作异常，只能当作未完整实现/未验证。后续要先确认：

- AW9523 对 AW88298 reset 成功；
- 是否需要 GPIO0 MCLK；
- I2S 时钟和 slot 与 AW88298 配套；
- Windows bridge 推到设备的 PCM 是否是 24kHz、s16le、mono/stereo 可接受格式。

## 最可能导致本轮真机失败的原因

按优先级排序：

1. PY32 启动窗口已补齐到每次 probe 前等待 200ms；仍需真机确认 `py32_available` 是否稳定为 true。
2. LED 物理主路径是 PY32，不是 GPIO RMT。只看 `led_gpio_write_ok` 会误判。真机应以 `py32_led_available` 和 `py32_led_write_ok` 为准。
3. 舵机 ping 判定可能比 HtSz 更严格。需要增加“VM_EN opened / UART write attempted / ping result / position write result”分层日志，而不是只看 `servo=false`。
4. 头部/外壳触摸已有 SI12T 可选驱动，但仍需真机验证 `0x68` 是否存在、12 秒校准后是否能触发 `source=head_si12t`。
5. 音频没有完整 codec 链。当前默认不开音频，开了也只是实验 I2S 输出。
6. 当前表情不是 HtSz LVGL avatar，所以表情正常显示也不会有 HtSz 的动态原生感。
7. CoreS3 camera 与早期按钮定义有 GPIO 冲突。若未来启用板载 camera，需要重新治理按钮和 camera 的硬件边界。

## 建议下一步

在继续扩展功能前，先做一轮硬件探针式修复，范围要小：

1. 真机确认 PY32 初始化前 200ms 稳定等待后，`py32_available` 是否稳定为 true。
2. 查看 self-test 和 hello 中的 `py32_available`、`py32_led_available`、`py32_led_write_ok`、`servo_vm_en_ok`、`servo_ping_ok`、`servo_write_ok`。
3. 验证 SI12T 可选驱动：检测不到 `0x68` 时应静默降级；检测到时等待 12 秒校准后摸头应发 `source=head_si12t`。
4. 把舵机真机验收拆成 ping 和 write-position 两步。
5. 音频保持默认关闭，先不要把 TTS 失败归到 bridge，等 AW88298 codec 链补齐后再做端到端。
6. 若要 HtSz 原生观感，单独开 avatar renderer 工作，不和硬件修复混在一起。
