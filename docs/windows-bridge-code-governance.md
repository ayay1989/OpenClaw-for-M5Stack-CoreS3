# Windows Bridge Code Governance

The Windows Bridge is split into reusable core modules and thin CLI examples.

## Module Boundaries

Reusable logic belongs in:

- `windows_bridge/openclaw_bridge/body_client.py`
  - Local Bridge HTTP client.
  - StackChan body commands such as presence, emotion, look, motion, LED, and beep.
- `windows_bridge/openclaw_bridge/resident_loop.py`
  - Conversation loop state transitions.
  - Brain adapter interface.
  - Demo and generic HTTP brain adapters.
  - System TTS wrapper.
- `windows_bridge/openclaw_bridge/body_tools.py`
  - OpenClaw-callable low-level and experience-level body tools.
  - Argument clamping and tool-to-device-command mapping.
- `windows_bridge/openclaw_bridge/face_tracking.py`
  - Face observation model.
  - Safe yaw/pitch conversion.
  - Smoothing, rate limiting, and lost-face recentering.
- `windows_bridge/openclaw_bridge/events.py`
  - Device event to OpenClaw intent mapping.
  - Shared semantics for tactile, button, gesture, and heartbeat events.
- `windows_bridge/openclaw_bridge/lifecycle.py`
  - Sleep/wake state.
  - Event-triggered proactive responses.
  - Local status overlay summaries.
- `windows_bridge/openclaw_bridge/memory_context.py`
  - Short-lived OpenClaw memory context loading and sanitization.
- `windows_bridge/openclaw_bridge/speech.py`
  - Speech duration cues used by speaking-state synchronization.
- `windows_bridge/openclaw_bridge/runtime.py`
  - Runtime config loading.
  - Shared builders for body, resident conversation loop, and face tracking.

CLI-only behavior belongs in:

- `windows_bridge/examples/openclaw_body_client.py`
- `windows_bridge/examples/resident_conversation_loop.py`
- `windows_bridge/examples/face_tracking_loop.py`
- `windows_bridge/examples/stackchan_runtime.py`
- `windows_bridge/tools/fake_cores3_device.py`

Do not duplicate business logic in examples. Examples should import from `openclaw_bridge`.

## Extension Rules

When adding real OpenClaw, ASR, TTS, or camera integrations:

- Add adapter classes or new modules under `openclaw_bridge` when the code is reusable.
- Keep hardware/model/vendor-specific startup flags in `examples` or `tools`.
- Keep CoreS3 protocol payload construction inside `StackChanBodyClient`.
- Keep high-level OpenClaw tools and presets in `body_tools.py`.
- Keep body event semantics inside `events.py`.
- Keep sleep/wake and proactive life behavior inside `lifecycle.py`.
- Keep face-to-servo safety limits inside `FaceTracker`.
- Keep long-term memory outside the bridge and outside CoreS3 firmware; only pass sanitized short-lived summaries through `memory_context.py`.

## Testing Rules

Every reusable module should have tests under `tests/windows_bridge`.

Current test responsibilities:

- `test_bridge.py`: TCP/HTTP bridge behavior, queueing, hello gating, body client.
- `test_resident_loop.py`: conversation loop body-state order and tactile context.
- `test_face_tracking.py`: face coordinate mapping, rate limit, and lost-face recenter.
- `test_events_runtime.py`: event intent mapping and runtime config behavior.
- `test_lifecycle.py`: sleep/wake behavior, proactive actions, and status overlay summaries.
- `test_body_tools_memory.py`: OpenClaw body tools, memory context, and speech cues.

Before pushing:

```powershell
python -m unittest discover -s tests/windows_bridge
python -m py_compile windows_bridge\openclaw_stackchan_bridge.py windows_bridge\openclaw_bridge\*.py windows_bridge\examples\*.py
```

Firmware builds still require an ESP-IDF v5.x environment:

```powershell
idf.py set-target esp32s3
idf.py build
```
