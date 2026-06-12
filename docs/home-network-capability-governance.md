# Home Network Capability Governance

This document governs larger home-network features such as WebSocket, MQTT, audio streaming, microphone input, wake word, echo cancellation, OTA, and multi-device routing.

## Architecture Rules

- OpenClaw / Windows remains the brain, long-term memory owner, and external service integration layer.
- CoreS3 remains the body, short-lived state holder, and local hardware expression layer.
- CoreS3 must not store long-term memory, facts, summaries, embeddings, tokens, or private context.
- New transports must reuse the same message contract; WebSocket and MQTT are transport adapters, not separate business protocols.

## Default Safety

High-risk capabilities must default to disabled:

- WebSocket remote control.
- MQTT broker integration.
- Audio streaming.
- Microphone input.
- Wake word.
- Echo cancellation.
- OTA.
- Remote multi-device control.

Enabling them requires local config, menuconfig, or an environment variable. Real tokens, broker credentials, OTA URLs, and home IP addresses must never be committed.

## Capability Discovery

Optional capabilities must be discoverable through `hello.features`, `tools/list`, or local runtime config.

Use separate feature flags instead of one broad `audio=true` flag:

- `audio_out`
- `audio_stream_out`
- `audio_in`
- `wake_word`
- `echo_cancellation`
- `ota`
- `websocket`
- `mqtt`
- `multi_device`

Missing optional hardware or services must return recoverable errors and must not crash the resident session.

## Transport Rules

- Default transport remains TCP newline JSON.
- WebSocket/MQTT must preserve existing `action`, event, hello, heartbeat, and MCP semantics.
- Remote listeners must require a token or stronger authentication.
- Reconnect must trigger hello/capability rediscovery.
- Message queues must have explicit limits and drop policies.

## Audio Rules

- Raw audio must not be logged by default.
- Microphone input must be explicitly enabled.
- Audio chunks must declare sample rate, channels, format, stream id, sequence, and direction.
- Audio streaming must enforce max chunk size, max duration, busy behavior, and interruption behavior.
- Speaker or microphone failure must not break LED, face, touch, heartbeat, or buttons.

## OTA Rules

OTA is a high-risk capability. Before implementation, define firmware version, target hardware model, artifact hash, signature policy, rollback conditions, power/network preflight checks, update permissions, status events, and failure errors.

OTA tokens, signed URLs, and private manifests must not appear in logs.

## Multi-Device Rules

- Each StackChan needs a stable `device_id`.
- Bridge routing must not depend on socket order.
- Commands must target a device, room, or explicit broadcast policy.
- Dangerous broadcasts such as OTA, motion, and audio playback are disabled by default.
- Queues, events, and capabilities are per-device.
