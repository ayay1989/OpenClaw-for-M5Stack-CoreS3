#!/usr/bin/env python3
"""Example OpenClaw-side client for the local StackChan bridge API."""

from __future__ import annotations

import argparse
import json
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any


class StackChanBodyClient:
    def __init__(self, base_url: str = "http://127.0.0.1:8766") -> None:
        self.base_url = base_url.rstrip("/")

    def status(self) -> dict[str, Any]:
        return self._get("/status")

    def poll_events(self, limit: int = 20, after: float | None = None) -> list[dict[str, Any]]:
        query = {"limit": str(limit)}
        if after is not None:
            query["after"] = str(after)
        response = self._get("/events?" + urllib.parse.urlencode(query))
        events = response.get("events", [])
        return events if isinstance(events, list) else []

    def set_emotion(self, emotion: str) -> dict[str, Any]:
        return self.device_command({"action": "emotion", "value": emotion})

    def set_presence(self, state: str, emotion: str | None = None, mouth: bool | None = None) -> dict[str, Any]:
        payload: dict[str, Any] = {"action": "presence", "state": state}
        if emotion is not None:
            payload["emotion"] = emotion
        if mouth is not None:
            payload["mouth"] = mouth
        return self.device_command(payload)

    def start_listening(self) -> dict[str, Any]:
        return self.set_presence("listening", "normal")

    def start_thinking(self) -> dict[str, Any]:
        return self.set_presence("thinking", "sleepy")

    def start_speaking(self, emotion: str = "happy") -> dict[str, Any]:
        return self.set_presence("speaking", emotion, mouth=True)

    def stop_speaking(self) -> dict[str, Any]:
        return self.set_presence("online_idle", "happy", mouth=False)

    def look_at(self, yaw: int, pitch: int, duration_ms: int = 350) -> dict[str, Any]:
        return self.device_command({"action": "look", "yaw": yaw, "pitch": pitch, "duration_ms": duration_ms})

    def motion(self, gesture: str) -> dict[str, Any]:
        return self.device_command({"action": "motion", "gesture": gesture})

    def led(self, r: int, g: int, b: int) -> dict[str, Any]:
        return self.device_command({"action": "led", "r": r, "g": g, "b": b})

    def breath(self, r: int, g: int, b: int, speed: int = 3) -> dict[str, Any]:
        return self.device_command({"action": "led_effect", "effect": "breath", "r": r, "g": g, "b": b, "speed": speed})

    def beep(self, freq: int = 880, duration_ms: int = 120, volume: int = 30) -> dict[str, Any]:
        return self.device_command({"action": "beep", "freq": freq, "duration_ms": duration_ms, "volume": volume})

    def device_command(self, payload: dict[str, Any]) -> dict[str, Any]:
        return self._post("/command", {"device_command": payload})

    def _get(self, path: str) -> dict[str, Any]:
        with urllib.request.urlopen(self.base_url + path, timeout=5) as response:
            return json.loads(response.read().decode("utf-8"))

    def _post(self, path: str, payload: dict[str, Any]) -> dict[str, Any]:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        request = urllib.request.Request(
            self.base_url + path,
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            return json.loads(exc.read().decode("utf-8"))


def demo(base_url: str) -> None:
    body = StackChanBodyClient(base_url)
    print(json.dumps(body.status(), ensure_ascii=False, indent=2))
    body.start_listening()
    time.sleep(0.5)
    body.start_thinking()
    time.sleep(0.5)
    body.start_speaking("happy")
    body.motion("nod")
    time.sleep(1.0)
    body.stop_speaking()
    print(json.dumps(body.poll_events(limit=10), ensure_ascii=False, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser(description="Example client for the local StackChan bridge API")
    parser.add_argument("--base-url", default="http://127.0.0.1:8766")
    parser.add_argument("--demo", action="store_true", help="run a small body behavior demo")
    args = parser.parse_args()
    if args.demo:
        demo(args.base_url)
    else:
        print(json.dumps(StackChanBodyClient(args.base_url).status(), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
