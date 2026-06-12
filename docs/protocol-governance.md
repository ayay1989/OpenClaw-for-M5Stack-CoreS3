# Protocol Governance

This project uses a small stable JSON contract between OpenClaw, the Windows Bridge, and CoreS3.

The goal is simple: contributors should be able to add body capabilities without breaking existing OpenClaw integrations.

## Source of Truth

The protocol source of truth is:

1. `docs/openclaw-stackchan-protocol.md`
2. `docs/schema/openclaw-stackchan-message.schema.json`
3. `docs/examples/protocol-valid-messages.jsonl`
4. Firmware parser and responder code under `main/`
5. Windows Bridge client/tool code under `windows_bridge/openclaw_bridge/`
6. Tests under `tests/windows_bridge/`

When these disagree, update the document first, then code and tests.

## Compatibility Rules

- Keep newline-delimited JSON as the base transport.
- Keep existing `action` payloads backward compatible.
- Add new fields instead of renaming existing fields.
- Optional hardware must degrade with explicit recoverable errors, for example `motion unavailable` or `audio unavailable`.
- `tools/list` must reflect runtime capability flags after reconnect.
- Do not store long-term OpenClaw memory on CoreS3.

## Adding a New Device Command

Before merging a new command:

1. Add the command shape to `docs/openclaw-stackchan-protocol.md`.
2. Update `docs/schema/openclaw-stackchan-message.schema.json`.
3. Add at least one sample to `docs/examples/protocol-valid-messages.jsonl` when useful.
4. Define ranges, defaults, and error behavior.
5. Add firmware parsing and response behavior.
6. Add or update `StackChanBodyClient` helpers when the command should be used from Windows.
7. Add `BodyToolRouter` entries only if OpenClaw should call it as a tool.
8. Add no-hardware tests when the behavior can be validated without CoreS3.

## Adding a New Body Event

Before merging a new event:

1. Add the event JSON example to the protocol document.
2. Update the JSON schema and sample JSONL when the shape changes.
3. Add the OpenClaw intent mapping in `windows_bridge/openclaw_bridge/events.py`.
4. Add tests that map firmware-like event samples to intents.
5. Keep raw event fields stable so older OpenClaw adapters can ignore unknown additions.

## Validation

Run this before pushing protocol changes:

```powershell
python windows_bridge\tools\validate_protocol_schema.py
python windows_bridge\tools\run_no_hardware_checks.py
```

Home-network feature governance lives in `docs/home-network-capability-governance.md`.

## Secrets and Local Runtime Data

Do not commit:

- real WiFi SSID or password;
- home LAN IP addresses;
- OpenClaw tokens;
- local memory context files;
- bridge event logs;
- generated build artifacts.

Use `menuconfig`, environment variables, or local ignored config files for private runtime values.
