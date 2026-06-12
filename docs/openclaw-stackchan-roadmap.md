# OpenClaw StackChan Roadmap

This roadmap keeps the project aligned with the goal:

OpenClaw is the brain. StackChan/CoreS3 is the body.

## Responsibility Split

Windows/OpenClaw owns:
- Long-term memory, user facts, summaries, embeddings, and recall policy.
- ASR from the Windows microphone.
- TTS generation and voice selection.
- Face tracking from the Windows camera.
- Conversation state, intention, and safety decisions.
- TCP server / bridge that speaks newline-delimited JSON to the CoreS3 device.

CoreS3/StackChan owns:
- LCD emotions and speaking/listening visuals.
- LED color and breathing.
- Button, touch, pressure/tactile, gesture, and heartbeat events.
- Optional yaw/pitch head motion through Stackchan servos.
- Optional speaker output when the audio path is validated.
- Short-lived session and resident state only.

The device must not persist long-term OpenClaw memory.

## Milestone 1: Body Contract

Status: in progress.

Firmware side:
- Stable TCP/serial JSON protocol.
- Hello, heartbeat, and runtime feature discovery.
- Emotions, LED, buttons, touch, gestures.
- Touch-derived pressure events: `press`, `hold`, `release`.
- Optional motion with graceful fallback.
- GPL-3.0 licensing and Chinese README.

Windows Bridge side:
- TCP server on the configured host/port.
- Device registry from `hello`.
- Event router for button, touch, pressure, gesture, and heartbeat.
- Command helpers for emotion, presence, LED, look, motion, and beep.

Acceptance:
- Touching the robot produces both `touch` and `pressure` events in Windows logs.
- Missing motion/audio hardware does not break the session.
- OpenClaw can set face, light, presence, and head motion through one bridge API.

## Milestone 2: Conversation Loop

Firmware side:
- Keep current command/event protocol stable.
- Add real PCM speaker playback only after AW88298/I2S validation.
- Keep microphone input out of firmware until a Windows-first loop works.

Windows Bridge side:
- Windows microphone to ASR.
- ASR text to OpenClaw.
- OpenClaw response to TTS.
- TTS playback route:
  - first: Windows speaker for fast validation;
  - later: stream PCM to StackChan when firmware audio is ready.
- Presence mapping:
  - listening -> listening face and blue breath;
  - thinking -> sleepy/normal face;
  - speaking -> mouth-open face and warm LED;
  - interrupted -> normal face and stop speech.

Acceptance:
- User can speak to Windows.
- OpenClaw answers with remembered context.
- StackChan face and motion follow listening/thinking/speaking states.

## Milestone 3: Face Tracking

Firmware side:
- Use existing `look` command for yaw/pitch.
- Keep camera out of CoreS3 initially because CoreS3 camera pins conflict with buttons and other board functions.

Windows Bridge side:
- Use Windows camera for face detection.
- Convert face position to safe yaw/pitch targets.
- Rate limit servo commands.
- Return to center when face is lost.

Acceptance:
- StackChan looks toward the user without jitter.
- Face loss does not leave servos at extreme angles.

## Milestone 4: Rich Tactile Feedback

Firmware side:
- Keep `pressure` event shape stable.
- Add new `source` values when hardware sensors are added, for example `head_fsr`, `body_fsr`, or `base_fsr`.
- Include real sensor `intensity` when available.

Windows Bridge side:
- Map tactile events to OpenClaw intents:
  - `press` -> attention/contact;
  - `hold` -> comfort/petting;
  - `release` -> contact ended.
- Add cooldowns so repeated touches do not flood the conversation.

Acceptance:
- OpenClaw can respond differently to tap, hold, and release.
- Future pressure hardware does not require a new protocol.

## Milestone 5: Public Release

Repository side:
- Keep secrets out of defaults and history.
- Keep build instructions in Chinese.
- Add release notes and known hardware limits.
- Tag stable firmware versions.

Acceptance:
- A new user can configure WiFi/TCP from `menuconfig`.
- `idf.py set-target esp32s3 && idf.py build` passes in ESP-IDF v5.x.
- README states GPL-3.0 clearly.
