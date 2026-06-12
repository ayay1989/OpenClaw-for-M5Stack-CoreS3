#!/usr/bin/env python3
"""Diagnose the local Windows Bridge and CoreS3 connection state."""

from __future__ import annotations

import argparse
import json
import socket
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


DEFAULT_EVENT_LOG = Path(__file__).resolve().parents[1] / "logs" / "events.ndjson"


def auth_headers(token: str | None) -> dict[str, str]:
    return {"X-OpenClaw-Token": token} if token else {}


def get_json(url: str, token: str | None = None, timeout: float = 3.0) -> dict[str, Any]:
    request = urllib.request.Request(url, headers=auth_headers(token))
    with urllib.request.urlopen(request, timeout=timeout) as response:
        payload = json.loads(response.read().decode("utf-8"))
    if not isinstance(payload, dict):
        raise RuntimeError("response is not a JSON object")
    return payload


def local_ipv4_addresses() -> list[str]:
    addresses: set[str] = set()
    hostname = socket.gethostname()
    try:
        for item in socket.getaddrinfo(hostname, None, socket.AF_INET):
            addresses.add(item[4][0])
    except OSError:
        pass
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("8.8.8.8", 80))
        addresses.add(probe.getsockname()[0])
        probe.close()
    except OSError:
        pass
    return sorted(ip for ip in addresses if not ip.startswith("127."))


def tcp_connect(host: str, port: int, timeout: float = 2.0) -> tuple[bool, str]:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True, "ok"
    except OSError as exc:
        return False, str(exc)


def print_status(status: dict[str, Any]) -> None:
    state = status.get("state") if isinstance(status.get("state"), dict) else {}
    print("[diagnostics] control API: ok")
    print(f"[diagnostics] device connected: {bool(state.get('connected'))}")
    print(f"[diagnostics] device address: {state.get('address') or 'none'}")
    print(f"[diagnostics] device id: {state.get('device_id') or 'unknown'}")
    print(f"[diagnostics] firmware: {state.get('firmware') or 'unknown'}")
    print(f"[diagnostics] last heartbeat age: {state.get('last_heartbeat_age_s')}")
    features = state.get("features") if isinstance(state.get("features"), dict) else {}
    print(f"[diagnostics] features: {json.dumps(features, ensure_ascii=False, separators=(',', ':'))}")


def print_recent_log(path: Path, limit: int) -> None:
    if limit <= 0:
        return
    if not path.exists():
        print(f"[diagnostics] event log: missing ({path})")
        return
    try:
        lines = path.read_text(encoding="utf-8").splitlines()[-limit:]
    except OSError as exc:
        print(f"[diagnostics] event log: unreadable ({exc})")
        return
    print(f"[diagnostics] event log: {path}")
    for line in lines:
        print(f"[diagnostics] recent: {line}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Diagnose OpenClaw StackChan Bridge")
    parser.add_argument("--url", default="http://127.0.0.1:8766", help="Bridge control API URL")
    parser.add_argument("--token", default=None)
    parser.add_argument("--device-port", default=8765, type=int, help="CoreS3 TCP listen port")
    parser.add_argument("--event-log", default=str(DEFAULT_EVENT_LOG), help="Bridge JSONL event log path")
    parser.add_argument("--tail", default=8, type=int, help="number of recent event log lines to print")
    args = parser.parse_args()

    print("[diagnostics] Windows LAN IPv4 candidates:", ", ".join(local_ipv4_addresses()) or "none")
    ok, message = tcp_connect("127.0.0.1", args.device_port)
    print(f"[diagnostics] local TCP {args.device_port}: {'listening' if ok else 'not reachable'} ({message})")

    try:
        status = get_json(args.url.rstrip("/") + "/status", token=args.token)
    except (OSError, urllib.error.URLError, RuntimeError) as exc:
        print(f"[diagnostics] control API: not reachable ({exc})")
        print("[diagnostics] next: start windows_bridge\\openclaw_stackchan_bridge.py or check the Windows service log.")
        print_recent_log(Path(args.event_log), args.tail)
        return 2

    print_status(status)
    print_recent_log(Path(args.event_log), args.tail)
    state = status.get("state") if isinstance(status.get("state"), dict) else {}
    if not state.get("connected"):
        print("[diagnostics] next: confirm the firmware TCP host is one of the LAN IPv4 candidates above.")
        print("[diagnostics] next: allow inbound TCP 8765 through Windows Firewall for private networks.")
        print("[diagnostics] next: make sure Windows and CoreS3 are on the same WiFi/VLAN.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
