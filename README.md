# OpenClaw Stackchan CoreS3 Firmware

ESP-IDF v5.x firmware for M5Stack CoreS3 / ESP32-S3. It connects to WiFi, keeps a TCP client connection to OpenClaw, accepts newline-delimited JSON from both serial and TCP, drives the ILI9341 LCD, controls LED outputs, and reports button/touch/heartbeat events.

This project was checked against the CoreS3 board support in `mo-hantang/Stackchan-HtSz`. The current defaults therefore use the Stackchan-HtSz CoreS3 board pins:

- Internal I2C: SDA GPIO12, SCL GPIO11
- LCD SPI: MOSI GPIO37, SCLK GPIO36, CS GPIO3, DC GPIO35, SPI mode 2
- LCD reset: AW9523 IO expander at `0x58`
- PMIC/backlight: AXP2101 at `0x34`
- Touch: FT6336 at `0x38`

## Build

```powershell
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py flash monitor
```

Set WiFi and OpenClaw TCP settings under `OpenClaw Stackchan` in menuconfig. Do not commit real WiFi credentials in `sdkconfig`.

`CONFIG_OPENCLAW_TCP_HOST` must be an IPv4 address that the CoreS3 can reach on the same network. Do not use `127.0.0.1` for a PC-side OpenClaw server; on the device that address points back to the CoreS3 itself.

## Runtime Defaults

- WiFi: configured by `CONFIG_OPENCLAW_WIFI_SSID` and `CONFIG_OPENCLAW_WIFI_PASSWORD`
- TCP server: configured by `CONFIG_OPENCLAW_TCP_HOST` and `CONFIG_OPENCLAW_TCP_PORT`
- Serial/TCP protocol: one JSON object per line, separated by `\n`
- Boot state: fullscreen Stackchan-style `happy` face + blue breathing LED
- Heartbeat: every 10 seconds
- Touch gestures: `tap`, `double_tap`, `long_press`, `swipe_left`, `swipe_right`, `swipe_up`, `swipe_down`

## Quick Serial Tests

```json
{"action":"ping"}
{"action":"emotion","value":"love"}
{"action":"led","r":255,"g":100,"b":50}
{"action":"led_effect","effect":"breath","r":0,"g":200,"b":255,"speed":3}
```

## MCP-Compatible Calls

The main protocol is still newline-delimited JSON. The firmware also accepts a lightweight JSON-RPC payload wrapped as `type=mcp` so OpenClaw can discover and call basic device tools.

```json
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"initialize","id":1}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/list","id":2}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.emotion.set","arguments":{"value":"love"}},"id":3}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.led.breath","arguments":{"r":0,"g":200,"b":255,"speed":3}},"id":4}}
```

Supported face names: `happy`, `normal`, `sad`, `angry`, `surprised`, `sleepy`, `shy`, `love`.
The aliases `neutral`, `loving`, `kissy`, `embarrassed`, and `shocked` are also accepted.

## Hardware Notes

- AXP2101 PMIC is initialized early for CoreS3 power/backlight rails.
- AW9523 is initialized early and used to reset the LCD panel and AW88298 amp.
- IP5306 `0x75` is only probed as a fallback note; normal CoreS3 hardware uses AXP2101 instead.
- GPIO45 is the known-good external NeoPixel/SK6812 output inherited from the flashed baseline.
- A Stackchan-style PY32 LED ring is optionally probed on internal I2C address `0x6F`; if it is missing, GPIO45 LED output still works.
- Button C uses GPIO0, the ESP32-S3 boot pin, so firmware handles it as a debounced active-low input only after boot.
- AW88298 speaker audio is not enabled yet, but the shared internal I2C bus and AW9523 reset path are initialized.
