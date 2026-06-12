#!/usr/bin/env python3
"""Run no-hardware validation for the Windows Bridge stack."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def run_step(args: list[str]) -> None:
    print("[check]", " ".join(args))
    subprocess.run(args, cwd=ROOT, check=True)


def main() -> int:
    py = sys.executable
    run_step([py, "-m", "unittest", "discover", "-s", "tests/windows_bridge"])
    run_step(
        [
            py,
            "-m",
            "py_compile",
            "windows_bridge/openclaw_stackchan_bridge.py",
            "windows_bridge/openclaw_bridge/__init__.py",
            "windows_bridge/openclaw_bridge/body_client.py",
            "windows_bridge/openclaw_bridge/events.py",
            "windows_bridge/openclaw_bridge/face_tracking.py",
            "windows_bridge/openclaw_bridge/resident_loop.py",
            "windows_bridge/openclaw_bridge/runtime.py",
            "windows_bridge/examples/openclaw_body_client.py",
            "windows_bridge/examples/resident_conversation_loop.py",
            "windows_bridge/examples/face_tracking_loop.py",
            "windows_bridge/examples/stackchan_runtime.py",
            "windows_bridge/tools/fake_cores3_device.py",
            "windows_bridge/tools/fake_openclaw_brain.py",
        ]
    )
    run_step([py, "windows_bridge/examples/stackchan_runtime.py", "--config", "windows_bridge/config.example.json", "--face-sim"])
    run_step([py, "windows_bridge/examples/stackchan_runtime.py", "--config", "windows_bridge/config.example.json", "--life-demo"])
    print("[check] no-hardware validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
