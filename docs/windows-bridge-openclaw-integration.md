# Windows Bridge OpenClaw Integration

This document describes how OpenClaw should treat the Windows Bridge.

OpenClaw is the brain. The Bridge is the nervous system. CoreS3/StackChan is the body.

## Local API

Default base URL:

```text
http://127.0.0.1:8766
```

Endpoints:

- `GET /status`: bridge and body state.
- `GET /events?limit=20`: recent body events.
- `POST /command`: send a body command.

Command examples:

```json
{"command":"/emotion love"}
{"device_command":{"action":"presence","state":"speaking","emotion":"happy","mouth":true}}
{"device_command":{"action":"look","yaw":10,"pitch":30,"duration_ms":350}}
```

Python example:

```python
from windows_bridge.examples.openclaw_body_client import StackChanBodyClient

body = StackChanBodyClient()
body.start_listening()
body.start_speaking("happy")
body.motion("nod")
body.stop_speaking()
```

## Body Events

Pressure/tactile:

```json
{"kind":"pressure","message":{"event":"pressure","source":"touchscreen","action":"press","intensity":40}}
```

Recommended OpenClaw meaning:

- `press`: contact began; the user touched the robot.
- `hold`: sustained contact; can be interpreted as petting, comfort, or attention.
- `release`: contact ended.

Button:

- `pin=A`, `press`: wake/listen.
- `pin=B`, `press`: interrupt/stop speaking.
- `pin=C`, `press`: safe action or recenter.

Gesture:

- `tap`: attention.
- `double_tap`: summon/wake.
- `long_press`: sleep toggle or quiet mode.
- `swipe_*`: browse mood or explicit UI gesture.

Heartbeat:

- Use heartbeat age to detect stale body state.
- Do not kill the resident session on one missed heartbeat.
- If stale, keep OpenClaw memory/session alive and degrade only body control.

## Conversation Loop

Recommended first implementation:

1. Windows microphone captures user speech.
2. Windows ASR turns speech into text.
3. OpenClaw handles memory recall and response generation.
4. Bridge receives presence commands:
   - listening while ASR is active;
   - thinking while OpenClaw is generating;
   - speaking while TTS is playing.
5. Windows TTS plays audio through the PC speaker first.
6. StackChan shows face, LED, and motion synchronized with the conversation.

Do not put long-term memory on CoreS3. The device receives only short-lived state.

## Face Tracking Loop

Recommended first implementation:

1. Windows camera detects face position.
2. Vision adapter converts face x/y into safe yaw/pitch.
3. Adapter rate-limits calls to `body.look_at(yaw, pitch)`.
4. If the face is lost, return to center after a short timeout.

CoreS3 should only execute safe `look` commands.

## Boundaries

The Bridge may keep:

- Current device state.
- Recent events.
- Short-lived session identifiers.
- Optional diagnostic event logs.

The Bridge should not become:

- The long-term memory store.
- The ASR/TTS model runtime.
- The camera recognition model runtime.
- The OpenClaw reasoning engine.

Those belong to Windows/OpenClaw adapters that call the Bridge.
