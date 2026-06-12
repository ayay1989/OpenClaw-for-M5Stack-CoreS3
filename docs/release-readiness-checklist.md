# Release Readiness Checklist

Use this checklist before tagging or announcing a public release.

## Repository

- `LICENSE` is present and states GPL-3.0.
- README is Chinese-first and explains the brain/body boundary.
- No real WiFi SSID, WiFi password, home IP, token, or private memory data is committed.
- `windows_bridge/config.example.json` uses only local/example values.

## Firmware

- ESP-IDF v5.x is installed on the build machine.
- `idf.py set-target esp32s3` succeeds.
- `idf.py build` succeeds.
- CoreS3 boots with `happy` face and blue breathing LED.
- Serial monitor shows `hello` and `heartbeat`.
- Touch emits `touch` and `pressure` events.
- Missing motion/audio hardware degrades without rebooting.

## Windows Bridge

- No-hardware checks pass:

```powershell
python windows_bridge\tools\run_no_hardware_checks.py
```

- Bridge starts:

```powershell
python windows_bridge\openclaw_stackchan_bridge.py
```

- Fake CoreS3 can connect:

```powershell
python windows_bridge\tools\fake_cores3_device.py
```

- Fake OpenClaw brain can answer:

```powershell
python windows_bridge\tools\fake_openclaw_brain.py
```

- Runtime entry works:

```powershell
python windows_bridge\examples\stackchan_runtime.py --config windows_bridge\config.example.json --face-sim
```

## Real Integration

These require local hardware or services and cannot be fully proven by repository tests alone:

- Real OpenClaw memory/chat endpoint.
- Real Windows microphone ASR.
- Real Windows TTS voice selection.
- Real Windows camera face detection.
- Real CoreS3 speaker PCM playback.
- Real external pressure sensors, if added later.
