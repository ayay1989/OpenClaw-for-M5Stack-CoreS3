#!/usr/bin/env python3
"""Smoke-test a running Windows Bridge after installer setup."""

from __future__ import annotations

import argparse
import json
import time
import urllib.error
import urllib.request
from typing import Any


def get_json(url: str, token: str | None = None, timeout: float = 2.0) -> dict[str, Any]:
    request = urllib.request.Request(url, headers=auth_headers(token))
    with urllib.request.urlopen(request, timeout=timeout) as response:
        payload = json.loads(response.read().decode("utf-8"))
    if not isinstance(payload, dict):
        raise RuntimeError("response is not a JSON object")
    return payload


def post_json(url: str, payload: dict[str, Any], token: str | None = None, timeout: float = 5.0) -> dict[str, Any]:
    request = urllib.request.Request(
        url,
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={"Content-Type": "application/json", **auth_headers(token)},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        result = json.loads(response.read().decode("utf-8"))
    if not isinstance(result, dict):
        raise RuntimeError("response is not a JSON object")
    return result


def auth_headers(token: str | None) -> dict[str, str]:
    return {"X-OpenClaw-Token": token} if token else {}


def main() -> int:
    parser = argparse.ArgumentParser(description="Smoke-test OpenClaw StackChan Bridge")
    parser.add_argument("--url", default="http://127.0.0.1:8766")
    parser.add_argument("--tts-text", default="你好")
    parser.add_argument("--voice", default="zh-CN-YunxiNeural")
    parser.add_argument("--token", default=None)
    parser.add_argument("--wait-s", default=15, type=int)
    parser.add_argument("--require-device", action="store_true", help="fail unless a CoreS3 device is connected")
    args = parser.parse_args()

    deadline = time.time() + args.wait_s
    last_error: Exception | None = None
    while time.time() < deadline:
        try:
            status = get_json(args.url.rstrip("/") + "/status", token=args.token)
            print("[smoke] bridge status:", json.dumps(status, ensure_ascii=False, separators=(",", ":")))
            break
        except (OSError, urllib.error.URLError, RuntimeError) as exc:
            last_error = exc
            time.sleep(1)
    else:
        raise SystemExit(f"[smoke] bridge is not reachable: {last_error}")

    state = status.get("state") if isinstance(status.get("state"), dict) else {}
    if args.require_device and not state.get("connected"):
        raise SystemExit(
            "[smoke] bridge is running, but CoreS3 has not connected. "
            "Check firmware TCP host, Windows LAN IPv4, and Windows Firewall for TCP 8765."
        )

    result = post_json(
        args.url.rstrip("/") + "/command",
        {"action": "tts", "text": args.tts_text, "voice": args.voice, "prefer_device": True},
        token=args.token,
    )
    print("[smoke] tts result:", json.dumps(result, ensure_ascii=False, separators=(",", ":")))
    if not result.get("ok"):
        raise SystemExit("[smoke] tts command failed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
