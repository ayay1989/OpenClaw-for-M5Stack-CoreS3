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

## Current Project Status

Status: repository-ready, hardware-diagnostics repair pass implemented, pending ESP-IDF build and CoreS3 hardware validation.

Authoritative progress view:
- This roadmap is the single project-level progress view.
- `docs/hardware-validation-log.md` records hardware evidence only.
- `docs/release-readiness-checklist.md` records release gates only.
- `docs/pre-residency-engineering-plan.md` records engineering split references only.

Current governance baseline:
- `AGENTS.md` defines fail-fast, sub-agent, reviewer, drift-audit, and QA rules.
- Windows Bridge auto-reactions are centralized in `windows_bridge/openclaw_bridge/auto_reactions.py`.
- Firmware local visual feedback has been separated from the protocol parser through `main/visuals.c`.
- Firmware self-test has been separated from the protocol parser through `main/selftest.c`.
- Firmware body input events have been separated from the protocol parser through `main/body_events.c`.
- Shared LED/motion/audio diagnostics live in `main/diagnostics.c`.
- `main/protocol.c` remains the main command parser and transport-facing protocol module.

Current hardware diagnosis:
- HtSz functional extraction and failure analysis lives in `docs/hardware-porting/htsz-functional-extract-and-failure-analysis.md`.
- User-side feedback indicates network/session can come up, but LED ring, servo motion, body-touch input, and speaker output are not yet reliable on real hardware.
- Firmware now separates PY32, PY32 LED, servo VM_EN, servo ping, servo write, SI12T body touch, motion, and audio diagnostics in hello/heartbeat/self-test/status outputs.
- PY32 LED ring configuration can retry after startup, so early power timing failures do not permanently disable the ring.
- Motion/servo capability is reported true only after verified servo detection; VM_EN-only state remains diagnostic.
- SI12T head/body touch has a pure-C optional driver; it reports available only after the 12-second calibration and first state read succeed, and still requires true CoreS3 validation.

## Milestone 1: Body Contract

Status: repository-ready, second governance hardening pass complete, pending hardware validation.

Firmware side:
- Stable TCP/serial JSON protocol.
- Hello, heartbeat, and runtime feature discovery.
- Emotions, LED, buttons, touch, gestures.
- Touch-derived pressure events: `press`, `hold`, `release`.
- Optional SI12T head/body touch events through `source=head_si12t`.
- Optional motion with graceful fallback.
- GPL-3.0 licensing and Chinese README.

Windows Bridge side:
- TCP server on the configured host/port.
- Device registry from `hello`.
- Event router for button, touch, pressure, gesture, and heartbeat.
- Command helpers for emotion, presence, LED, look, motion, and beep.
- Local HTTP control API for OpenClaw and future ASR/TTS/vision adapters.
- Fake CoreS3 device and unit tests for no-hardware validation.
- Fake OpenClaw brain and no-hardware check script.

Acceptance:
- Touching the robot produces both `touch` and `pressure` events in Windows logs.
- If SI12T is present, touching the head/body produces `body_input` and `pressure` with `source=head_si12t`.
- Missing motion/audio hardware does not break the session.
- OpenClaw can set face, light, presence, and head motion through one bridge API.
- Without hardware, the fake CoreS3 device can exercise hello, heartbeat, pressure, and command delivery.
- Without real OpenClaw, the fake brain can exercise the resident conversation loop.

## Milestone 2: Conversation Loop

Firmware side:
- Keep current command/event protocol stable.
- Add real PCM speaker playback only after AW88298/I2S validation.
- Keep microphone input out of firmware until a Windows-first loop works.

Windows Bridge side:
- Windows microphone to ASR.
- ASR text to OpenClaw.
- OpenClaw response to TTS.
- Short-lived OpenClaw memory context is forwarded to the brain adapter.
- Experience-level body tools wrap common actions like speaking, touch reaction, and sleep mode.
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
- Repository scaffold is ready; real ASR/OpenClaw/TTS endpoints are still local integration work.

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
- Repository scaffold is ready; real camera detector is still local integration work.

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

## Change Log

- 2026-07-31: Added PY32 LED late-retry behavior after hardware feedback showed the LED ring can stay dark despite the rest of the session working.
- 2026-06-30: Implemented hardware-diagnostics repair pass: PY32 pre-wait, layered PY32/LED/servo diagnostics, optional SI12T head/body touch driver, and updated protocol docs.
- 2026-06-30: Added HtSz functional extraction and current failure analysis after hardware feedback showed body-output issues still remain.
- 2026-06-26: Set this roadmap as the single project-level progress view; recorded completed first governance pass for Agent rules, Bridge auto-reaction centralization, and firmware visual-service extraction.
- 2026-06-26: Completed second governance pass by extracting firmware self-test, body-event handling, and shared diagnostics from `main/protocol.c`.
