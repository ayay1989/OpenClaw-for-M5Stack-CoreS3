# OpenClaw Stackchan CoreS3 Protocol

This document is the stable integration contract for the OpenClaw CoreS3 firmware.

Transport:
- TCP client to the configured OpenClaw host and port.
- UART0 CDC serial.
- Both transports use one JSON object per line with `\n` separators.

Compatibility:
- Legacy `action` JSON remains the primary debug and fallback protocol.
- MCP-style JSON-RPC is an additive discovery/call layer.
- The device must keep running when optional hardware is absent.

## Compatibility Matrix

| Stage | Legacy action surface | Hello features | MCP tools/list | Missing hardware behavior | Default enabled |
| --- | --- | --- | --- | --- | --- |
| v0.4 resident | `presence`, `sleep`, `memory_cue` added to the original face/LED/ping commands | `presence=true`, `memory_context=true`, `motion=false`, `audio_out=false` | device, emotion, presence, LED tools | No memory storage required on device | Yes |
| v0.5 motion | `look`, `motion` | `motion=true/false`, `servo=true/false` | motion tools only when servos initialized | `motion unavailable`; other systems continue | Optional runtime hardware |
| v0.6 beep | `beep` | `audio_out=true/false`, `audio_in=false` | `self.audio.beep` only when audio initialized | `audio unavailable`; other systems continue | Disabled by default |
| v1.0 stable | All listed legacy actions stay compatible | Field names are stable for protocol version `1` | Runtime capability discovery is required after reconnect | All optional failures are recoverable | Base firmware yes; optional hardware no |

## Hello

The device emits `type=hello` on boot and after TCP connect.

Important fields:

```json
{
  "type": "hello",
  "name": "openclaw-stackchan-cores3",
  "version": "1.0.0",
  "protocol": "openclaw-stackchan",
  "protocol_version": 1,
  "transport": "tcp-json",
  "features": {
    "mcp": true,
    "emotion": true,
    "led": true,
    "touch": true,
    "gesture": true,
    "pressure": true,
    "body_input": true,
    "tactile": true,
    "presence": true,
    "memory_context": true,
    "motion": false,
    "servo": false,
    "audio_in": false,
    "audio_out": false,
    "audio_stream_out": false,
    "wake_word": false,
    "echo_cancellation": false,
    "websocket": false,
    "mqtt": false,
    "ota": false,
    "multi_device": false
  },
  "audio_params": null
}
```

`touch`, `gesture`, `pressure`, `tactile`, `motion`, `servo`, and `audio_out` are runtime capability flags. They are true only when the related subsystem initialized successfully.

`pressure` is the stable tactile/contact event family, not a promise that every build has a physical pressure pad. In v1 it is derived from touch contact. Future shell touch, FSR pressure pads, IMU shake/pickup, or other body sensors should keep the same high-level body event semantics where possible.

If `audio_out` is true, `audio_params` is:

```json
{
  "sample_rate": 24000,
  "format": "pcm_s16le",
  "channels": 2
}
```

Audio is still local experimental beep by default. The `audio_stream` message shape is reserved for future PCM streaming and must return recoverable errors until streaming is explicitly implemented and enabled.

### Hello Field Stability

| Field | Type | Required | Meaning | v1 stable |
| --- | --- | --- | --- | --- |
| `type` | string | yes | Always `hello` | yes |
| `name` | string | yes | Firmware integration name | yes |
| `version` | string | yes | Firmware version | yes |
| `protocol` | string | yes | Always `openclaw-stackchan` | yes |
| `protocol_version` | number | yes | Compatibility version, currently `1` | yes |
| `transport` | string | yes | Transport label, currently `tcp-json` | yes |
| `features` | object | yes | Runtime capability flags | yes |
| `device` | object | yes | Device identity and model | yes |
| `resident` | object | yes | Resident support/context info | yes |
| `session_id` | string | no | Present after OpenClaw session is known | yes |
| presence fields | mixed | yes | Presence state, connection state, emotion, memory flags | yes |
| `audio_params` | object or null | yes | Audio format only when `audio_out=true` | yes |
| `emotions` | array | yes | Supported face names | yes |

## OpenClaw Context

OpenClaw may acknowledge or attach context:

```json
{"type":"hello_ack","session_id":"demo-session","resident_id":"openclaw"}
{"type":"memory_context","session_id":"demo-session","resident_id":"openclaw","ttl_ms":3600000}
```

The device stores only short-lived session/resident identifiers and the memory-context flag. It does not store long-term memory facts, summaries, embeddings, or user private records.

## Legacy Actions

Supported commands:

```json
{"action":"ping"}
{"action":"emotion","value":"happy"}
{"action":"led","r":255,"g":100,"b":50}
{"action":"led_effect","effect":"breath","r":0,"g":200,"b":255,"speed":3}
{"action":"presence","state":"listening","emotion":"normal"}
{"action":"presence","state":"speaking","emotion":"happy","mouth":true}
{"action":"sleep","enabled":true}
{"action":"memory_cue","emotion":"love","ttl_ms":3000}
{"action":"look","yaw":10,"pitch":30,"duration_ms":400}
{"action":"motion","gesture":"nod"}
{"action":"beep","freq":880,"duration_ms":120,"volume":30}
{"action":"audio_stream","op":"start","stream_id":"tts-1","direction":"tts_out","sample_rate":24000,"channels":1,"format":"pcm_s16le"}
{"action":"audio_stream","op":"chunk","stream_id":"tts-1","seq":1,"data_b64":"AAAA"}
{"action":"audio_stream","op":"stop","stream_id":"tts-1"}
{"action":"interrupt","source":"button"}
```

Supported emotions:
- `happy`
- `normal`
- `sad`
- `angry`
- `surprised`
- `sleepy`
- `shy`
- `love`

Supported motion gestures:
- `center`
- `nod`
- `shake`
- `tilt`

Motion ranges:
- `yaw`: `-45..45`
- `pitch`: `5..60`
- `duration_ms`: `50..3000`

Audio beep ranges:
- `freq` or `frequency_hz`: `80..4000`
- `duration_ms`: `20..2000`
- `volume`: `0..100`

Audio stream fields are reserved for future TTS PCM and microphone work:
- `op`: `start`, `chunk`, or `stop`
- `stream_id`: stable id for one stream
- `direction`: `tts_out` or `mic_in`
- `sample_rate`: `8000..48000`
- `channels`: `1..2`
- `format`: currently `pcm_s16le`
- `seq`: monotonically increasing chunk sequence
- `data_b64`: base64 PCM chunk for `chunk`

In the current firmware, `audio_stream` is a compatibility placeholder and returns a recoverable error unless a future audio streaming implementation is enabled.

Interrupt:
- `{"action":"interrupt","source":"button"}` asks the body to stop speaking/listening visuals and return to idle.

## Responses

Success:

```json
{"status":"ok","action":"motion","value":"nod"}
```

Error:

```json
{"status":"error","action":"motion","message":"motion unavailable"}
```

Expected optional-hardware errors:
- `motion unavailable`
- `motion busy`
- `audio unavailable`
- `audio busy`

These errors are recoverable. OpenClaw should keep the resident session alive and continue using available capabilities.

## Events

Body interaction input:

```json
{"event":"body_input","input":"touch","source":"touchscreen","action":"hold","x":120,"y":200,"intensity":80,"intent":"tactile_contact"}
{"event":"body_input","input":"gesture","source":"touchscreen","action":"double_tap","x":120,"y":200,"intensity":60,"intent":"summon"}
{"event":"body_input","input":"motion","source":"imu","action":"shake","intensity":75,"intent":"attention"}
```

`body_input` is the preferred OpenClaw-facing event for physical interaction. Legacy `touch`, `pressure`, `gesture`, and `button` events remain available for compatibility and low-level debugging.

Button:

```json
{"event":"button","pin":"A","action":"press","intent":"wake"}
```

Touch:

```json
{"event":"touch","x":120,"y":200,"intent":"attention"}
```

Pressure/tactile contact:

```json
{"event":"pressure","source":"touchscreen","action":"press","x":120,"y":200,"intensity":40,"intent":"tactile_contact"}
{"event":"pressure","source":"touchscreen","action":"hold","x":120,"y":200,"intensity":80,"intent":"tactile_contact"}
{"event":"pressure","source":"touchscreen","action":"release","x":120,"y":200,"intensity":0,"intent":"tactile_contact"}
```

In v1, pressure is derived from the CoreS3 FT6336 touch panel so OpenClaw can react when the robot is touched. Future shell touch sensors, head/body FSR pressure sensors, or IMU-based shake/pickup detection should reuse the same body-event vocabulary where possible and set a more specific `source`, for example `head_fsr`, `body_fsr`, `shell_touch`, or `imu`.

Gesture:

```json
{"event":"gesture","gesture":"double_tap","intent":"summon","x":120,"y":200}
```

Heartbeat:

```json
{
  "event": "heartbeat",
  "uptime": 12345,
  "wifi_rssi": -45,
  "motion_available": true,
  "audio_out_available": false
}
```

Presence fields are included in hello, heartbeat, and input events.

### Heartbeat Field Stability

| Field | Type | Required | Meaning | v1 stable |
| --- | --- | --- | --- | --- |
| `event` | string | yes | Always `heartbeat` | yes |
| `uptime` | number | yes | Device uptime in seconds | yes |
| `wifi_rssi` | number | yes | WiFi RSSI, `0` if unavailable | yes |
| `motion_available` | boolean | yes | Current motion runtime availability | yes |
| `audio_out_available` | boolean | yes | Current speaker runtime availability | yes |
| `presence_state` | string | yes | Resident presence state | yes |
| `connection_state` | string | yes | Offline/WiFi/TCP/OpenClaw readiness | yes |
| `emotion` | string | yes | Current/last face emotion | yes |
| `session_id` | string | no | Present when bound | yes |
| `resident_id` | string | no | Present when bound | yes |
| `memory_context_loaded` | boolean | yes | Short-lived context flag | yes |

## MCP Tools

Always available when MCP is enabled:
- `self.device.ping`
- `self.device.get_status`
- `self.emotion.set`
- `self.presence.set`
- `self.led.set_color`
- `self.led.breath`

Available only when motion initialized:
- `self.motion.look_at`
- `self.motion.center`
- `self.motion.nod`
- `self.motion.shake`
- `self.motion.tilt`

Available only when experimental audio output initialized:
- `self.audio.beep`

Future audio streaming and microphone tools must be added only when explicit runtime features are present, for example `audio_stream_out=true` or `audio_in=true`.

OpenClaw should call `tools/list` after each TCP reconnect because optional capabilities are runtime-dependent.

## Degradation Rules

WiFi lost:
- Continue local face, LED, touch, buttons, motion, and audio if initialized.
- Auto reconnect WiFi.
- Resume TCP after WiFi returns.

TCP lost:
- Continue local behavior.
- Retry TCP every 5 seconds.
- Clear session id until OpenClaw reconnects.

PY32 missing:
- External GPIO45 LED still works.
- PY32 LED ring disabled.
- Servo VM_EN unavailable, so motion stays unavailable.

Servos missing:
- Motion commands return `motion unavailable`.
- Face, LED, touch, WiFi, TCP, and heartbeat continue.

Audio disabled or missing:
- `features.audio_out=false`.
- `beep` returns `audio unavailable`.
- No microphone or streaming behavior is implied.

Bad JSON:
- Device logs only source and length.
- Device does not echo raw malformed payloads.

## Stage Boundaries

Included in the current firmware/bridge contract:
- WiFi/TCP/serial JSON.
- Windows Bridge WebSocket API for home WiFi clients.
- Fullscreen Stackchan-style emotions.
- LED color and breathing.
- Touch/gesture/button events.
- Touch-derived pressure/tactile contact events.
- Resident presence and short-lived memory context.
- Optional yaw/pitch motion.
- Optional local beep.

Planned future enhancements:
- MQTT event bus.
- TTS PCM streaming to the CoreS3 speaker.
- Optional device-side or external microphone input.
- Wake word.
- Echo cancellation.
- CoreS3 camera or alternate camera modules if hardware allows.
- OTA.

Architectural boundary:
- Long-term memory is required for the full OpenClaw resident experience, but it remains owned by OpenClaw/Windows. CoreS3 must not persist long-term memory facts, summaries, embeddings, or private records.

## v1.0 Acceptance Checklist

Build and boot:
- Firmware reports version `1.0.0`.
- README links to this protocol document.
- Default configuration keeps `CONFIG_OPENCLAW_AUDIO_ENABLE=n`.
- Default hello reports `features.audio_out=false`.
- `idf.py set-target esp32s3 && idf.py build` passes in an ESP-IDF v5 environment.

Legacy JSON smoke test:
- `ping` returns `pong`.
- Each emotion renders or returns a clear `unknown emotion`.
- `led` and `led_effect` still work.
- `presence`, `sleep`, and `memory_cue` still update face/LED mood.
- `look` and `motion` return success only when motion is available; otherwise `motion unavailable`.
- `beep` returns success only when audio output is available; otherwise `audio unavailable`.
- Touch press/hold/release emits `pressure` events with `source`, `action`, and `intensity`.

MCP smoke test:
- `initialize` returns server info.
- `tools/list` always includes base device/emotion/presence/LED tools.
- `tools/list` does not expose motion tools when `features.motion=false`.
- `tools/list` does not expose `self.audio.beep` when `features.audio_out=false`.
- `self.device.get_status` includes presence, motion availability, and audio availability.

Degradation:
- WiFi disconnect does not blank the LCD or stop local LED/face/touch behavior.
- TCP disconnect retries every 5 seconds and clears session until re-bound.
- Missing PY32/servos does not reboot the device.
- Missing/disabled audio does not reboot the device.
- Bad JSON does not echo raw payload content into logs.

Resident boundary:
- `memory_context` can set `session_id`, `resident_id`, and a TTL flag.
- The device does not persist long-term memory facts, summaries, or embeddings.
- OpenClaw remains the memory owner.

Deferred after v1:
- WebSocket/MQTT transport.
- Audio streaming to CoreS3 speaker.
- Device-side microphone input and echo cancellation.
- CoreS3 camera experiments.
- OTA.

Never store on CoreS3:
- Device-side long-term memory facts, summaries, embeddings, or private records.
