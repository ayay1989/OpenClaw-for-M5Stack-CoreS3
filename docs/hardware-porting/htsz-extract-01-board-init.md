# Stackchan-HtSz CoreS3 板级初始化抽取 01

更新时间：2026-06-17

## 参考文件

只读参考源：`/private/tmp/Stackchan-HtSz-fresh`

- `main/boards/m5stack-core-s3/config.h`
- `main/boards/m5stack-core-s3/m5stack_core_s3.cc`
- `main/boards/m5stack-core-s3/cores3_audio_codec.cc`
- `main/boards/common/i2c_device.cc`
- `main/boards/common/axp2101.cc`

## 板级顺序

HtSz 的 CoreS3 路径不是通用 ESP32 ILI9341 示例。它的顺序是：

1. 初始化 CoreS3 内部 I2C。
2. 初始化 AXP2101 PMIC。
3. 初始化 AW9523 IO 扩展器。
4. 扫描 I2C。
5. 通过 PY32 打开舵机 VM_EN。
6. PY32 稳定后初始化 UART 舵机。
7. 初始化 PY32 LED 环。
8. 初始化 SPI LCD，LCD reset 走 AW9523。
9. 初始化 camera、FT6336 触摸、BMI270、SI12T 等外设。

当前仓库顺序已经接近，但需要保留关键约束：PY32 只影响舵机供电和 LED 环，PY32 不可用不能阻塞 LCD、触摸、网络启动。

## 关键硬件事实

### 内部 I2C

- I2C port：`I2C_NUM_1`
- SDA：`GPIO12`
- SCL：`GPIO11`
- glitch ignore：`7`
- internal pullup：enabled
- AXP2101、AW9523、FT6336 默认思路：`400kHz`
- PY32、SI12T、camera SCCB：`100kHz`

注意：HtSz 不是把整条内部 I2C 总线全局降到 100kHz。qoder-v1 的全局降频只能作为实验项，不能当作 HtSz 事实。

### AXP2101 PMIC

- 地址：`0x34`
- 主要用途：电源域、背光亮度。
- 当前仓库已经包含参考初始化序列，应保持它作为 CoreS3 PMIC 主路径。
- `IP5306 0x75` 只适合历史 fallback probe，不应作为 CoreS3 初始化依赖。

### AW9523 IO 扩展器

- 地址：`0x58`
- 主要用途：LCD reset、AW88298 reset、IO 扩展。
- 初始化后需要短延时。
- LCD reset 不应回退到普通 GPIO reset。

### LCD / SPI

- SPI host：`SPI3_HOST`
- MOSI：`GPIO37`
- SCLK：`GPIO36`
- CS：`GPIO3`
- DC：`GPIO35`
- reset：NC，实际由 AW9523 控制
- backlight：NC，背光由 AXP2101 管理
- 分辨率：`320 x 240`
- pixel clock：`40MHz`
- color order：BGR
- invert：enabled

这与最初常见资料里的 `MOSI=23/SCLK=18/CS=5/DC=15/RST=12/BL=32` 不一致。当前项目应以 HtSz/CoreS3 板级实现为准。

### PY32

- 地址：`0x6F`
- 版本寄存器：`0x02`
- 合法版本：非 `0x00` 且非 `0xFF`
- VM_EN 置位：
  - `0x03 |= 0x01`
  - `0x09 |= 0x01`
  - `0x05 |= 0x01`
- HtSz probe：最多 10 次，每次等待 200ms，I2C 操作超时约 200ms。
- 舵机初始化前：PY32 VM_EN 成功后再等待 200ms。

### FT6336 触摸屏

- 地址：`0x38`
- chip ID：`0xA3`
- 数据起始寄存器：`0x02`
- 每次读取：6 字节
- 点数：`data[0] & 0x0F`
- X：`((data[1] & 0x0F) << 8) | data[2]`
- Y：`((data[3] & 0x0F) << 8) | data[4]`
- HtSz 轮询周期：20ms

### SI12T 头部/外壳触摸

- 地址：`0x68`
- I2C 速度：`100kHz`
- 启动后等待：12s
- 轮询周期：100ms
- 状态寄存器：`0x10`
- 这不是 CoreS3 屏幕触摸，而是 StackChan 外壳/头顶触摸输入。是否启用要看实际硬件。

### BMI270 IMU

- HtSz 实际地址：`0x69`
- 需要避免与 SI12T `0x68` 混淆。
- 当前项目如要支持摇晃/拿起事件，应单独做可选 IMU 驱动。

## 当前项目采用结论

1. 保持 CoreS3 正确 I2C 引脚和 PMIC/IO 扩展器初始化。
2. 不整体套用通用 M5Stack CoreS3 示例引脚。
3. 不把全局 I2C 降到 100kHz 作为默认修复。
4. PY32 probe 和舵机 VM_EN 可向 HtSz 的保守时序靠近。
5. FT6336 触摸轮询应从当前 50ms 向 HtSz 20ms 靠近，但要避免 LED/PY32 写入抢占 I2C。
