# OpenClaw Stackchan CoreS3 Firmware

ESP-IDF v5.x firmware for M5Stack CoreS3 / ESP32-S3. It connects to WiFi, keeps a TCP client connection to OpenClaw, accepts newline-delimited JSON from both serial and TCP, drives the ILI9341 LCD, controls LED outputs, moves the Stackchan head when servos are present, and reports button/touch/heartbeat events.

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
- Presence states: `booting`, `connecting`, `online_idle`, `listening`, `thinking`, `speaking`, `sleeping`, `offline_local`, `error`
- Motion: optional Stackchan yaw/pitch servos on UART1 GPIO6/GPIO7, auto-disabled when PY32 or servos are not present

## Quick Serial Tests

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
```

`presence` is the lightweight OpenClaw resident state layer. It changes the face and LED mood while keeping the older `emotion`, `led`, and `led_effect` commands compatible.
`look` and `motion` are v0.5 body commands. If the servo hardware is not detected, the firmware keeps running and replies with `motion unavailable`.

The firmware also accepts a short-lived context envelope:

```json
{"type":"memory_context","session_id":"demo-session","resident_id":"openclaw","ttl_ms":3600000}
```

Do not send full long-term memory to the device. CoreS3 only keeps a short-lived state flag; OpenClaw remains the memory owner.
If `summary`, `facts`, or similar fields are present, this firmware intentionally ignores their content.

## MCP-Compatible Calls

The main protocol is still newline-delimited JSON. The firmware also accepts a lightweight JSON-RPC payload wrapped as `type=mcp` so OpenClaw can discover and call basic device tools.

```json
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"initialize","id":1}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/list","id":2}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.emotion.set","arguments":{"value":"love"}},"id":3}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.led.breath","arguments":{"r":0,"g":200,"b":255,"speed":3}},"id":4}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.presence.set","arguments":{"state":"speaking","emotion":"happy"}},"id":5}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.motion.look_at","arguments":{"yaw":-15,"pitch":32,"duration_ms":350}},"id":6}}
{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.motion.nod","arguments":{}},"id":7}}
```

Supported face names: `happy`, `normal`, `sad`, `angry`, `surprised`, `sleepy`, `shy`, `love`.
The aliases `neutral`, `loving`, `kissy`, `embarrassed`, and `shocked` are also accepted.

## Hardware Notes

- AXP2101 PMIC is initialized early for CoreS3 power/backlight rails.
- AW9523 is initialized early and used to reset the LCD panel and AW88298 amp.
- IP5306 `0x75` is only probed as a fallback note; normal CoreS3 hardware uses AXP2101 instead.
- GPIO45 is the known-good external NeoPixel/SK6812 output inherited from the flashed baseline.
- A Stackchan-style PY32 helper MCU is optionally probed on internal I2C address `0x6F`; it drives the RGB ring and enables servo VM_EN power. If it is missing, GPIO45 LED output still works and motion is disabled.
- Stackchan yaw/pitch servos use UART1 at 1 Mbps: TX GPIO6, RX GPIO7, yaw servo ID `1`, pitch servo ID `2`. Safe ranges are yaw `-45..45` and pitch `5..60`, with center at `0,30`.
- Button C uses GPIO0, the ESP32-S3 boot pin, so firmware handles it as a debounced active-low input only after boot.
- AW88298 speaker audio is not enabled yet, but the shared internal I2C bus and AW9523 reset path are initialized.

## v0.5 Motion Validation

1. Boot and confirm the device still shows the `happy` face and blue breathing LED.
2. Confirm serial logs show PY32 and servo availability. Missing servos should be a warning, not a reboot.
3. Send `{"action":"ping"}` and the earlier emotion/LED/presence commands to confirm old JSON still works.
4. With PY32 and servos connected, send `{"type":"mcp","payload":{"jsonrpc":"2.0","method":"tools/list","id":2}}` and confirm `self.motion.*` tools are listed.
5. With servos connected, test `center`, `nod`, `shake`, `tilt`, and one `look` command.
6. Without PY32 or servos connected, `hello.features.motion` should be `false`; legacy motion commands should return an error such as `motion unavailable` while screen, LED, touch, WiFi, TCP, and heartbeat continue.
7. Touch validation: double tap should summon/listen and nod when motion is available; long press should sleep/wake and lower the head; left/right swipes should lightly look left/right.

v0.5 intentionally does not add audio, microphone, WebSocket, camera, IMU, OTA, or device-side long-term memory storage.
