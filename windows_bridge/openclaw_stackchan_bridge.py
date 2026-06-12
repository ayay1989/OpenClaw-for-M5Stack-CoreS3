#!/usr/bin/env python3
"""Minimal Windows-side bridge for OpenClaw StackChan CoreS3 firmware.

The bridge is intentionally dependency-free. It accepts the CoreS3 TCP client,
logs newline-delimited JSON events, and lets a human or future OpenClaw adapter
send commands from stdin.
"""

from __future__ import annotations

import argparse
import json
import queue
import socket
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Any


DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8765


@dataclass
class DeviceState:
    connected: bool = False
    address: str = ""
    device_id: str = ""
    firmware: str = ""
    features: dict[str, Any] = field(default_factory=dict)
    presence_state: str = ""
    connection_state: str = ""
    emotion: str = ""
    last_heartbeat_at: float = 0.0


class StackChanBridge:
    def __init__(self, host: str, port: int) -> None:
        self.host = host
        self.port = port
        self.state = DeviceState()
        self._send_queue: queue.Queue[dict[str, Any]] = queue.Queue()
        self._stop = threading.Event()
        self._client_lock = threading.Lock()
        self._client: socket.socket | None = None

    def start(self) -> bool:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind((self.host, self.port))
            server.listen(1)
            server.settimeout(1.0)
        except OSError as exc:
            server.close()
            print(f"[bridge] cannot listen on {self.host}:{self.port}: {exc}")
            return False

        threading.Thread(target=self._server_loop, args=(server,), name="tcp-server", daemon=True).start()
        threading.Thread(target=self._stdin_loop, name="stdin", daemon=True).start()
        print(f"[bridge] listening on {self.host}:{self.port}")
        print("[bridge] commands: /help, /status, /emotion happy, /presence listening, /look 0 30, /motion nod, /led 0 100 255, /quit")
        try:
            while not self._stop.is_set():
                time.sleep(0.2)
        except KeyboardInterrupt:
            print("\n[bridge] stopping")
            self._stop.set()
        self._close_client()
        return True

    def send_command(self, payload: dict[str, Any]) -> bool:
        with self._client_lock:
            client = self._client
        if client is None:
            print("[bridge] no CoreS3 connected; command queued")
            self._send_queue.put(payload)
            return False
        return self._send_now(client, payload)

    def _server_loop(self, server: socket.socket) -> None:
        with server:
            while not self._stop.is_set():
                try:
                    client, address = server.accept()
                except socket.timeout:
                    continue
                except OSError as exc:
                    if not self._stop.is_set():
                        print(f"[bridge] accept failed: {exc}")
                    continue
                self._handle_client(client, address)

    def _handle_client(self, client: socket.socket, address: tuple[str, int]) -> None:
        self._close_client()
        client.settimeout(0.5)
        with self._client_lock:
            self._client = client
            self.state.connected = True
            self.state.address = f"{address[0]}:{address[1]}"
        print(f"[bridge] CoreS3 connected from {self.state.address}")
        self._flush_queued_commands(client)

        buffer = ""
        try:
            with client:
                while not self._stop.is_set():
                    try:
                        chunk = client.recv(4096)
                    except socket.timeout:
                        continue
                    if not chunk:
                        break
                    buffer += chunk.decode("utf-8", errors="replace")
                    while "\n" in buffer:
                        line, buffer = buffer.split("\n", 1)
                        line = line.strip()
                        if line:
                            self._handle_line(line)
        except OSError as exc:
            print(f"[bridge] client error: {exc}")
        finally:
            with self._client_lock:
                if self._client is client:
                    self._client = None
                    self.state.connected = False
            print("[bridge] CoreS3 disconnected")

    def _flush_queued_commands(self, client: socket.socket) -> None:
        while not self._send_queue.empty():
            try:
                payload = self._send_queue.get_nowait()
            except queue.Empty:
                return
            if not self._send_now(client, payload):
                self._send_queue.put(payload)
                return

    def _send_now(self, client: socket.socket, payload: dict[str, Any]) -> bool:
        try:
            line = json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n"
            client.sendall(line.encode("utf-8"))
            print(f"[tx] {line.strip()}")
            return True
        except OSError as exc:
            print(f"[bridge] send failed: {exc}")
            self._close_client()
            self._send_queue.put(payload)
            return False

    def _close_client(self) -> None:
        with self._client_lock:
            client = self._client
            self._client = None
            self.state.connected = False
        if client is not None:
            try:
                client.close()
            except OSError:
                pass

    def _handle_line(self, line: str) -> None:
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            print(f"[rx:bad-json] {line[:160]}")
            return
        if not isinstance(message, dict):
            print(f"[rx:ignored] {message!r}")
            return
        self._update_state(message)
        self._print_event(message)

    def _update_state(self, message: dict[str, Any]) -> None:
        if message.get("type") == "hello":
            device = message.get("device")
            if isinstance(device, dict):
                self.state.device_id = str(device.get("device_id", ""))
                self.state.firmware = str(device.get("firmware", ""))
            features = message.get("features")
            if isinstance(features, dict):
                self.state.features = features
            self._send_now_or_queue({"type": "hello_ack", "session_id": "windows-bridge", "resident_id": "openclaw"})
        if message.get("event") == "heartbeat":
            self.state.last_heartbeat_at = time.time()
        for key in ("presence_state", "connection_state", "emotion"):
            value = message.get(key)
            if isinstance(value, str):
                setattr(self.state, key, value)

    def _send_now_or_queue(self, payload: dict[str, Any]) -> None:
        with self._client_lock:
            client = self._client
        if client is None:
            self._send_queue.put(payload)
        else:
            self._send_now(client, payload)

    def _print_event(self, message: dict[str, Any]) -> None:
        event = message.get("event")
        if event == "pressure":
            print(
                "[pressure] "
                f"source={message.get('source')} action={message.get('action')} "
                f"intensity={message.get('intensity')} x={message.get('x')} y={message.get('y')}"
            )
            return
        if event in {"touch", "gesture", "button", "heartbeat"}:
            print(f"[{event}] {json.dumps(message, ensure_ascii=False, separators=(',', ':'))}")
            return
        if message.get("type") == "hello":
            print(
                "[hello] "
                f"device={self.state.device_id or 'unknown'} firmware={self.state.firmware or 'unknown'} "
                f"features={json.dumps(self.state.features, ensure_ascii=False, separators=(',', ':'))}"
            )
            return
        if "status" in message:
            print(f"[status] {json.dumps(message, ensure_ascii=False, separators=(',', ':'))}")
            return
        print(f"[rx] {json.dumps(message, ensure_ascii=False, separators=(',', ':'))}")

    def _stdin_loop(self) -> None:
        while not self._stop.is_set():
            line = sys.stdin.readline()
            if line == "":
                time.sleep(0.1)
                continue
            self._handle_console_command(line.strip())

    def _handle_console_command(self, line: str) -> None:
        if not line:
            return
        if line == "/quit":
            self._stop.set()
            return
        if line == "/help":
            self._print_help()
            return
        if line == "/status":
            self._print_status()
            return
        if line.startswith("{"):
            try:
                payload = json.loads(line)
            except json.JSONDecodeError as exc:
                print(f"[bridge] invalid json: {exc}")
                return
            if isinstance(payload, dict):
                self.send_command(payload)
            else:
                print("[bridge] command JSON must be an object")
            return

        parts = line.split()
        command = parts[0].lower()
        try:
            payload = self._payload_from_short_command(command, parts[1:])
        except ValueError as exc:
            print(f"[bridge] {exc}")
            return
        self.send_command(payload)

    def _payload_from_short_command(self, command: str, args: list[str]) -> dict[str, Any]:
        if command == "/emotion" and len(args) == 1:
            return {"action": "emotion", "value": args[0]}
        if command == "/presence" and len(args) in {1, 2}:
            payload = {"action": "presence", "state": args[0]}
            if len(args) == 2:
                payload["emotion"] = args[1]
            return payload
        if command == "/look" and len(args) in {2, 3}:
            payload = {"action": "look", "yaw": int(args[0]), "pitch": int(args[1])}
            if len(args) == 3:
                payload["duration_ms"] = int(args[2])
            return payload
        if command == "/motion" and len(args) == 1:
            return {"action": "motion", "gesture": args[0]}
        if command == "/led" and len(args) == 3:
            return {"action": "led", "r": int(args[0]), "g": int(args[1]), "b": int(args[2])}
        if command == "/breath" and len(args) in {3, 4}:
            payload = {"action": "led_effect", "effect": "breath", "r": int(args[0]), "g": int(args[1]), "b": int(args[2])}
            if len(args) == 4:
                payload["speed"] = int(args[3])
            return payload
        if command == "/beep" and len(args) in {0, 1, 2, 3}:
            payload = {"action": "beep"}
            if len(args) >= 1:
                payload["freq"] = int(args[0])
            if len(args) >= 2:
                payload["duration_ms"] = int(args[1])
            if len(args) == 3:
                payload["volume"] = int(args[2])
            return payload
        if command == "/ping" and not args:
            return {"action": "ping"}
        raise ValueError("unknown command or wrong arguments; type /help")

    def _print_help(self) -> None:
        print("Commands:")
        print("  /status")
        print("  /ping")
        print("  /emotion happy|normal|sad|angry|surprised|sleepy|shy|love")
        print("  /presence online_idle|listening|thinking|speaking|sleeping [emotion]")
        print("  /look <yaw -45..45> <pitch 5..60> [duration_ms]")
        print("  /motion center|nod|shake|tilt")
        print("  /led <r> <g> <b>")
        print("  /breath <r> <g> <b> [speed]")
        print("  /beep [freq] [duration_ms] [volume]")
        print("  {\"action\":\"ping\"}")
        print("  /quit")

    def _print_status(self) -> None:
        age = time.time() - self.state.last_heartbeat_at if self.state.last_heartbeat_at else None
        heartbeat = "never" if age is None else f"{age:.1f}s ago"
        print(
            "[bridge-status] "
            f"connected={self.state.connected} address={self.state.address or '-'} "
            f"device={self.state.device_id or '-'} firmware={self.state.firmware or '-'} "
            f"presence={self.state.presence_state or '-'} emotion={self.state.emotion or '-'} "
            f"heartbeat={heartbeat}"
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="OpenClaw StackChan Windows bridge")
    parser.add_argument("--host", default=DEFAULT_HOST, help="listen host, default 0.0.0.0")
    parser.add_argument("--port", default=DEFAULT_PORT, type=int, help="listen port, default 8765")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    bridge = StackChanBridge(args.host, args.port)
    return 0 if bridge.start() else 1


if __name__ == "__main__":
    raise SystemExit(main())
