#!/usr/bin/env python3
"""Fake CoreS3 device for testing the Windows bridge without hardware."""

from __future__ import annotations

import argparse
import json
import socket
import threading
import time
from typing import Any


def send_json(sock: socket.socket, payload: dict[str, Any]) -> None:
    line = json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n"
    sock.sendall(line.encode("utf-8"))
    print(f"[fake-tx] {line.strip()}")


def reader(sock: socket.socket) -> None:
    buffer = ""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            return
        buffer += chunk.decode("utf-8", errors="replace")
        while "\n" in buffer:
            line, buffer = buffer.split("\n", 1)
            line = line.strip()
            if line:
                print(f"[fake-rx] {line}")


def run(host: str, port: int) -> None:
    with socket.create_connection((host, port), timeout=10) as sock:
        threading.Thread(target=reader, args=(sock,), daemon=True).start()
        send_json(
            sock,
            {
                "type": "hello",
                "name": "openclaw-stackchan-cores3",
                "version": "fake",
                "protocol": "openclaw-stackchan",
                "protocol_version": 1,
                "features": {
                    "emotion": True,
                    "led": True,
                    "touch": True,
                    "gesture": True,
                    "pressure": True,
                    "tactile": True,
                    "presence": True,
                    "motion": True,
                    "audio_out": False,
                },
                "device": {
                    "device_id": "FAKECORES3",
                    "name": "fake-cores3",
                    "model": "m5stack-cores3",
                    "firmware": "fake",
                },
            },
        )
        time.sleep(0.5)
        send_json(sock, {"event": "heartbeat", "uptime": 1, "wifi_rssi": -35, "motion_available": True})
        time.sleep(0.5)
        send_json(sock, {"event": "pressure", "source": "touchscreen", "action": "press", "x": 120, "y": 200, "intensity": 40})
        time.sleep(0.5)
        send_json(sock, {"event": "pressure", "source": "touchscreen", "action": "hold", "x": 120, "y": 200, "intensity": 80})
        time.sleep(0.5)
        send_json(sock, {"event": "pressure", "source": "touchscreen", "action": "release", "x": 120, "y": 200, "intensity": 0})
        time.sleep(3.0)


def main() -> int:
    parser = argparse.ArgumentParser(description="Fake CoreS3 device for bridge testing")
    parser.add_argument("--host", default="127.0.0.1", help="bridge host")
    parser.add_argument("--port", default=8765, type=int, help="bridge CoreS3 TCP port")
    args = parser.parse_args()
    run(args.host, args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
