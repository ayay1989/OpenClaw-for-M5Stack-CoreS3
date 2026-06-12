#!/usr/bin/env python3
"""Windows-side bridge for OpenClaw StackChan CoreS3 firmware.

The bridge has two jobs:
- accept the CoreS3 TCP client that speaks newline-delimited JSON;
- expose a small localhost HTTP API that OpenClaw, ASR/TTS, or face tracking
  adapters can call without touching the device socket directly.

It intentionally uses only the Python standard library so it can run on a fresh
Windows machine with Python installed.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import socket
import sys
import threading
import time
from collections import deque
from dataclasses import asdict, dataclass, field
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

from openclaw_bridge.edge_tts_bridge import EdgeTtsBridge
from openclaw_bridge.mqtt_bus import MqttBusClient, MqttConfig


DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8765
DEFAULT_CONTROL_HOST = "127.0.0.1"
DEFAULT_CONTROL_PORT = 8766
DEFAULT_WS_PORT = 8767
MAX_QUEUED_COMMANDS = 100
MAX_EVENTS = 300
MAX_LINE_BUFFER = 65536
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


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

    def to_public_dict(self) -> dict[str, Any]:
        data = asdict(self)
        if self.last_heartbeat_at:
            data["last_heartbeat_age_s"] = round(time.time() - self.last_heartbeat_at, 3)
        else:
            data["last_heartbeat_age_s"] = None
        return data


@dataclass
class BridgeEvent:
    timestamp: float
    kind: str
    message: dict[str, Any]

    def to_public_dict(self) -> dict[str, Any]:
        return {
            "timestamp": self.timestamp,
            "kind": self.kind,
            "message": self.message,
        }


class StackChanBridge:
    def __init__(
        self,
        host: str,
        port: int,
        control_host: str,
        control_port: int,
        auto_react: bool,
        event_log: str | None,
        control_token: str | None = None,
        ws_host: str | None = None,
        ws_port: int = DEFAULT_WS_PORT,
        mqtt_config: MqttConfig | None = None,
        tts_voice: str = "zh-CN-YunxiNeural",
    ) -> None:
        self.host = host
        self.port = port
        self.control_host = control_host
        self.control_port = control_port
        self.auto_react = auto_react
        self.event_log = event_log
        self.control_token = control_token
        self.ws_host = ws_host
        self.ws_port = ws_port
        self.mqtt_config = mqtt_config
        self.tts_voice = tts_voice

        self.state = DeviceState()
        self._events: deque[BridgeEvent] = deque(maxlen=MAX_EVENTS)
        self._send_queue: deque[dict[str, Any]] = deque(maxlen=MAX_QUEUED_COMMANDS)
        self._stop = threading.Event()
        self._client_lock = threading.Lock()
        self._events_lock = threading.Lock()
        self._queue_lock = threading.Lock()
        self._send_lock = threading.Lock()
        self._client: socket.socket | None = None
        self._connection_ready = False
        self._http_server: ThreadingHTTPServer | None = None
        self._ws_server: socket.socket | None = None
        self._ws_clients: set[socket.socket] = set()
        self._ws_lock = threading.Lock()
        self._mqtt_client: MqttBusClient | None = None
        self._tts = EdgeTtsBridge(tts_voice)
        self._last_auto_reaction: dict[str, float] = {}

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

        if not self._start_control_server():
            server.close()
            return False
        if not self._start_ws_server():
            server.close()
            self._stop_control_server()
            return False
        self._start_mqtt_client()

        threading.Thread(target=self._server_loop, args=(server,), name="tcp-server", daemon=True).start()
        threading.Thread(target=self._stdin_loop, name="stdin", daemon=True).start()
        print(f"[bridge] CoreS3 TCP listening on {self.host}:{self.port}")
        print(f"[bridge] local control API on http://{self.control_host}:{self.control_port}")
        if self.ws_host:
            print(f"[bridge] WebSocket API on ws://{self.ws_host}:{self.ws_port}/bridge")
        if self.mqtt_config:
            print(f"[bridge] MQTT bus {self.mqtt_config.host}:{self.mqtt_config.port} topic_prefix={self.mqtt_config.topic_prefix}")
        print("[bridge] commands: /help, /status, /emotion happy, /presence listening, /look 0 30, /motion nod, /quit")
        try:
            while not self._stop.is_set():
                time.sleep(0.2)
        except KeyboardInterrupt:
            print("\n[bridge] stopping")
            self._stop.set()
        self._close_client()
        self._stop_control_server()
        self._stop_ws_server()
        self._stop_mqtt_client()
        return True

    def stop(self) -> None:
        self._stop.set()
        self._close_client()
        self._stop_control_server()
        self._stop_ws_server()
        self._stop_mqtt_client()

    def send_command(self, payload: dict[str, Any]) -> bool:
        with self._client_lock:
            client = self._client
            ready = self._connection_ready
        if client is None or not ready:
            return self._queue_command(payload)
        return self._send_now(client, payload)

    def handle_control_command(self, payload: dict[str, Any]) -> dict[str, Any]:
        if payload.get("action") == "tts":
            return self._handle_tts_command(payload)
        if "command" in payload:
            command = payload.get("command")
            if not isinstance(command, str):
                return {"ok": False, "error": "command must be a string"}
            try:
                device_payload = self.payload_from_console_line(command)
            except ValueError as exc:
                return {"ok": False, "error": str(exc)}
        elif "device_command" in payload:
            device_payload = payload.get("device_command")
            if not isinstance(device_payload, dict):
                return {"ok": False, "error": "device_command must be an object"}
        elif "action" in payload or "type" in payload:
            device_payload = payload
        else:
            return {"ok": False, "error": "expected command, device_command, action, or type"}

        sent = self.send_command(device_payload)
        return {
            "ok": True,
            "sent_now": sent,
            "queued": not sent,
            "device_command": device_payload,
        }

    def _handle_tts_command(self, payload: dict[str, Any]) -> dict[str, Any]:
        text = payload.get("text")
        if not isinstance(text, str) or not text.strip():
            return {"ok": False, "error": "tts text is required"}
        voice = payload.get("voice")
        if isinstance(voice, str) and voice.strip() and voice != self._tts.voice:
            self._tts = EdgeTtsBridge(voice.strip())
        prefer_device = bool(payload.get("prefer_device", True))
        self.send_command({"action": "presence", "state": "speaking", "emotion": "happy", "mouth": True})
        result = self._tts.speak(
            text,
            self.send_command,
            device_audio_stream_available=bool(self.state.features.get("audio_stream_out")),
            prefer_device=prefer_device,
        )
        self.send_command({"action": "presence", "state": "online_idle", "emotion": "happy", "mouth": False})
        return {
            "ok": result.ok,
            "action": "tts",
            "mode": result.mode,
            "voice": result.voice,
            "message": result.message,
        }

    def recent_events(self, limit: int = 50, after: float | None = None) -> list[dict[str, Any]]:
        if limit < 1:
            limit = 1
        if limit > MAX_EVENTS:
            limit = MAX_EVENTS
        with self._events_lock:
            events = list(self._events)
        if after is not None:
            events = [event for event in events if event.timestamp > after]
        return [event.to_public_dict() for event in events[-limit:]]

    def payload_from_console_line(self, line: str) -> dict[str, Any]:
        line = line.strip()
        if not line:
            raise ValueError("empty command")
        if line.startswith("{"):
            payload = json.loads(line)
            if not isinstance(payload, dict):
                raise ValueError("command JSON must be an object")
            return payload
        parts = line.split()
        return self._payload_from_short_command(parts[0].lower(), parts[1:])

    def _start_control_server(self) -> bool:
        bridge = self

        class ControlHandler(BaseHTTPRequestHandler):
            def do_GET(self) -> None:  # noqa: N802
                if not self._authorized():
                    self._send_json(HTTPStatus.UNAUTHORIZED, {"ok": False, "error": "unauthorized"})
                    return
                if self.path.startswith("/status"):
                    self._send_json(HTTPStatus.OK, {"ok": True, "state": bridge.state.to_public_dict()})
                    return
                if self.path.startswith("/events"):
                    limit = self._query_int("limit", 50)
                    after = self._query_float("after")
                    self._send_json(HTTPStatus.OK, {"ok": True, "events": bridge.recent_events(limit=limit, after=after)})
                    return
                if self.path in {"/health", "/healthz"}:
                    self._send_json(HTTPStatus.OK, {"ok": True, "connected": bridge.state.connected})
                    return
                self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not found"})

            def do_POST(self) -> None:  # noqa: N802
                if not self._authorized():
                    self._send_json(HTTPStatus.UNAUTHORIZED, {"ok": False, "error": "unauthorized"})
                    return
                if self.path != "/command":
                    self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not found"})
                    return
                try:
                    payload = self._read_json_body()
                    result = bridge.handle_control_command(payload)
                except ValueError as exc:
                    self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": str(exc)})
                    return
                status = HTTPStatus.OK if result.get("ok") else HTTPStatus.BAD_REQUEST
                self._send_json(status, result)

            def log_message(self, fmt: str, *args: Any) -> None:
                print(f"[control] {self.address_string()} {fmt % args}")

            def _authorized(self) -> bool:
                if not bridge.control_token:
                    return True
                return self.headers.get("X-OpenClaw-Token") == bridge.control_token

            def _read_json_body(self) -> dict[str, Any]:
                length = int(self.headers.get("Content-Length", "0"))
                if length <= 0 or length > 65536:
                    raise ValueError("invalid content length")
                raw = self.rfile.read(length).decode("utf-8")
                payload = json.loads(raw)
                if not isinstance(payload, dict):
                    raise ValueError("request body must be a JSON object")
                return payload

            def _query_int(self, name: str, fallback: int) -> int:
                value = self._query_value(name)
                if value is None:
                    return fallback
                try:
                    return int(value)
                except ValueError:
                    return fallback

            def _query_float(self, name: str) -> float | None:
                value = self._query_value(name)
                if value is None:
                    return None
                try:
                    return float(value)
                except ValueError:
                    return None

            def _query_value(self, name: str) -> str | None:
                if "?" not in self.path:
                    return None
                query = self.path.split("?", 1)[1]
                for pair in query.split("&"):
                    key, _, value = pair.partition("=")
                    if key == name:
                        return value
                return None

            def _send_json(self, status: HTTPStatus, payload: dict[str, Any]) -> None:
                body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        try:
            if not self._control_host_is_local() and not self.control_token:
                print("[bridge] refusing remote control API without --control-token or OPENCLAW_BRIDGE_TOKEN")
                return False
            self._http_server = ThreadingHTTPServer((self.control_host, self.control_port), ControlHandler)
        except OSError as exc:
            print(f"[bridge] cannot start control API on {self.control_host}:{self.control_port}: {exc}")
            return False
        threading.Thread(target=self._http_server.serve_forever, name="control-api", daemon=True).start()
        return True

    def _stop_control_server(self) -> None:
        if self._http_server is not None:
            self._http_server.shutdown()
            self._http_server.server_close()
            self._http_server = None

    def _start_ws_server(self) -> bool:
        if not self.ws_host:
            return True
        if not self._host_is_local(self.ws_host) and not self.control_token:
            print("[bridge] refusing remote WebSocket API without --control-token or OPENCLAW_BRIDGE_TOKEN")
            return False
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind((self.ws_host, self.ws_port))
            server.listen(8)
            server.settimeout(1.0)
        except OSError as exc:
            server.close()
            print(f"[bridge] cannot start WebSocket API on {self.ws_host}:{self.ws_port}: {exc}")
            return False
        self._ws_server = server
        threading.Thread(target=self._ws_server_loop, args=(server,), name="websocket-api", daemon=True).start()
        return True

    def _stop_ws_server(self) -> None:
        if self._ws_server is not None:
            try:
                self._ws_server.close()
            except OSError:
                pass
            self._ws_server = None
        with self._ws_lock:
            clients = list(self._ws_clients)
            self._ws_clients.clear()
        for client in clients:
            try:
                client.close()
            except OSError:
                pass

    def _start_mqtt_client(self) -> None:
        if self.mqtt_config is None:
            return
        self._mqtt_client = MqttBusClient(self.mqtt_config, self._handle_mqtt_message)
        self._mqtt_client.start()

    def _stop_mqtt_client(self) -> None:
        if self._mqtt_client is not None:
            self._mqtt_client.stop()
            self._mqtt_client = None

    def _handle_mqtt_message(self, topic: str, message: dict[str, Any]) -> None:
        result = self.handle_control_command(message)
        if not result.get("ok"):
            print(f"[mqtt] rejected command on {topic}: {result.get('error')}")

    def _publish_mqtt_event(self, event: BridgeEvent) -> None:
        if self._mqtt_client is None or self.mqtt_config is None:
            return
        payload = {"type": "event", "event": self._redact_event(event).to_public_dict()}
        if not self._mqtt_client.publish_json(self.mqtt_config.event_topic, payload):
            print("[mqtt] event publish skipped; broker not connected")

    def _ws_server_loop(self, server: socket.socket) -> None:
        while not self._stop.is_set():
            try:
                client, address = server.accept()
            except socket.timeout:
                continue
            except OSError:
                if not self._stop.is_set():
                    print("[ws] accept failed")
                return
            threading.Thread(target=self._handle_ws_client, args=(client, address), name="websocket-client", daemon=True).start()

    def _handle_ws_client(self, client: socket.socket, address: tuple[str, int]) -> None:
        client.settimeout(1.0)
        try:
            request = self._recv_http_headers(client)
            if not self._complete_ws_handshake(client, request):
                client.close()
                return
            with self._ws_lock:
                self._ws_clients.add(client)
            self._ws_send_json(client, {"type": "welcome", "state": self.state.to_public_dict(), "events": self.recent_events(limit=20)})
            print(f"[ws] client connected from {address[0]}:{address[1]}")
            while not self._stop.is_set():
                payload = self._ws_recv_text(client)
                if payload is None:
                    break
                self._handle_ws_message(client, payload)
        except OSError as exc:
            print(f"[ws] client error: {exc}")
        finally:
            with self._ws_lock:
                self._ws_clients.discard(client)
            try:
                client.close()
            except OSError:
                pass
            print("[ws] client disconnected")

    def _recv_http_headers(self, client: socket.socket) -> str:
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = client.recv(4096)
            if not chunk:
                break
            data += chunk
            if len(data) > MAX_LINE_BUFFER:
                break
        return data.decode("utf-8", errors="replace")

    def _complete_ws_handshake(self, client: socket.socket, request: str) -> bool:
        lines = request.split("\r\n")
        request_line = lines[0] if lines else ""
        path = request_line.split(" ")[1] if len(request_line.split(" ")) >= 2 else "/"
        headers: dict[str, str] = {}
        for line in lines[1:]:
            key, sep, value = line.partition(":")
            if sep:
                headers[key.strip().lower()] = value.strip()
        key = headers.get("sec-websocket-key")
        token = headers.get("x-openclaw-token") or self._query_value_from_path(path, "token")
        if self.control_token and token != self.control_token:
            client.sendall(b"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n")
            return False
        if not key:
            client.sendall(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n")
            return False
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode("ascii")).digest()).decode("ascii")
        response = (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Accept: {accept}\r\n"
            "\r\n"
        )
        client.sendall(response.encode("ascii"))
        return True

    def _query_value_from_path(self, path: str, name: str) -> str | None:
        if "?" not in path:
            return None
        query = path.split("?", 1)[1]
        for pair in query.split("&"):
            key, _, value = pair.partition("=")
            if key == name:
                return value
        return None

    def _handle_ws_message(self, client: socket.socket, payload: str) -> None:
        try:
            message = json.loads(payload)
        except json.JSONDecodeError:
            self._ws_send_json(client, {"ok": False, "error": "invalid JSON"})
            return
        if not isinstance(message, dict):
            self._ws_send_json(client, {"ok": False, "error": "message must be an object"})
            return
        if message.get("type") == "ping":
            self._ws_send_json(client, {"type": "pong", "time": time.time()})
            return
        result = self.handle_control_command(message)
        self._ws_send_json(client, {"type": "command_result", **result})

    def _broadcast_ws_event(self, event: BridgeEvent) -> None:
        with self._ws_lock:
            clients = list(self._ws_clients)
        if not clients:
            return
        payload = {"type": "event", "event": self._redact_event(event).to_public_dict()}
        for client in clients:
            if not self._ws_send_json(client, payload):
                with self._ws_lock:
                    self._ws_clients.discard(client)
                try:
                    client.close()
                except OSError:
                    pass

    def _ws_send_json(self, client: socket.socket, payload: dict[str, Any]) -> bool:
        try:
            body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            client.sendall(self._ws_frame(body))
            return True
        except OSError:
            return False

    def _ws_frame(self, payload: bytes) -> bytes:
        length = len(payload)
        if length < 126:
            header = bytes([0x81, length])
        elif length <= 65535:
            header = bytes([0x81, 126]) + length.to_bytes(2, "big")
        else:
            header = bytes([0x81, 127]) + length.to_bytes(8, "big")
        return header + payload

    def _ws_recv_text(self, client: socket.socket) -> str | None:
        header = self._recv_exact(client, 2)
        if not header:
            return None
        first, second = header
        opcode = first & 0x0F
        masked = (second & 0x80) != 0
        length = second & 0x7F
        if length == 126:
            ext = self._recv_exact(client, 2)
            if not ext:
                return None
            length = int.from_bytes(ext, "big")
        elif length == 127:
            ext = self._recv_exact(client, 8)
            if not ext:
                return None
            length = int.from_bytes(ext, "big")
        if length > MAX_LINE_BUFFER:
            return None
        mask = self._recv_exact(client, 4) if masked else b""
        payload = self._recv_exact(client, length)
        if payload is None:
            return None
        if opcode == 0x8:
            return None
        if opcode == 0x9:
            client.sendall(bytes([0x8A, 0]))
            return ""
        if opcode != 0x1:
            return ""
        if masked and mask:
            payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        return payload.decode("utf-8", errors="replace")

    def _recv_exact(self, client: socket.socket, length: int) -> bytes | None:
        data = b""
        while len(data) < length:
            chunk = client.recv(length - len(data))
            if not chunk:
                return None
            data += chunk
        return data

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
            self._connection_ready = False
            self.state.connected = True
            self.state.address = f"{address[0]}:{address[1]}"
        print(f"[bridge] CoreS3 connected from {self.state.address}")

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
                    if len(buffer) > MAX_LINE_BUFFER:
                        print(f"[bridge] closing client: line buffer exceeded {MAX_LINE_BUFFER} bytes")
                        break
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
                    self._connection_ready = False
                    self.state.connected = False
            print("[bridge] CoreS3 disconnected")

    def _flush_queued_commands(self, client: socket.socket) -> None:
        while True:
            with self._queue_lock:
                if not self._send_queue:
                    return
                payload = self._send_queue.popleft()
            if not self._send_now(client, payload):
                self._queue_command(payload)
                return

    def _queue_command(self, payload: dict[str, Any]) -> bool:
        with self._queue_lock:
            full = len(self._send_queue) >= MAX_QUEUED_COMMANDS
            if full:
                dropped = self._send_queue[0]
                print(f"[bridge] command queue full; dropping oldest command {dropped}")
            self._send_queue.append(payload)
        return False

    def _send_now(self, client: socket.socket, payload: dict[str, Any]) -> bool:
        with self._send_lock:
            try:
                line = json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n"
                client.sendall(line.encode("utf-8"))
                print(f"[tx] {line.strip()}")
                return True
            except OSError as exc:
                print(f"[bridge] send failed: {exc}")
                self._close_client()
                self._queue_command(payload)
                return False

    def _close_client(self) -> None:
        with self._client_lock:
            client = self._client
            self._client = None
            self._connection_ready = False
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
        self._record_event(message)
        self._print_event(message)
        if self.auto_react:
            self._apply_auto_reaction(message)

    def _update_state(self, message: dict[str, Any]) -> None:
        if message.get("type") == "hello":
            device = message.get("device")
            if isinstance(device, dict):
                self.state.device_id = str(device.get("device_id", ""))
                self.state.firmware = str(device.get("firmware", ""))
            features = message.get("features")
            if isinstance(features, dict):
                self.state.features = features
            with self._client_lock:
                client = self._client
                self._connection_ready = client is not None
            self._send_now_or_queue({"type": "hello_ack", "session_id": "windows-bridge", "resident_id": "openclaw"})
            if client is not None:
                self._flush_queued_commands(client)
        if message.get("event") == "heartbeat":
            self.state.last_heartbeat_at = time.time()
        for key in ("presence_state", "connection_state", "emotion"):
            value = message.get(key)
            if isinstance(value, str):
                setattr(self.state, key, value)

    def _send_now_or_queue(self, payload: dict[str, Any]) -> None:
        with self._client_lock:
            client = self._client
            ready = self._connection_ready
        if client is None or not ready:
            self._queue_command(payload)
        else:
            self._send_now(client, payload)

    def _record_event(self, message: dict[str, Any]) -> None:
        kind = str(message.get("event") or message.get("type") or message.get("status") or "message")
        event = BridgeEvent(timestamp=time.time(), kind=kind, message=message)
        with self._events_lock:
            self._events.append(event)
        self._broadcast_ws_event(event)
        self._publish_mqtt_event(event)
        if self.event_log:
            try:
                with open(self.event_log, "a", encoding="utf-8") as fh:
                    fh.write(json.dumps(self._redact_event(event).to_public_dict(), ensure_ascii=False, separators=(",", ":")) + "\n")
            except OSError as exc:
                print(f"[bridge] event log write failed: {exc}")

    def _redact_event(self, event: BridgeEvent) -> BridgeEvent:
        sensitive = {"session_id", "resident_id", "memory_context", "summary", "facts", "preferences", "token"}
        message = {key: ("[redacted]" if key in sensitive else value) for key, value in event.message.items()}
        return BridgeEvent(timestamp=event.timestamp, kind=event.kind, message=message)

    def _control_host_is_local(self) -> bool:
        return self._host_is_local(self.control_host)

    def _host_is_local(self, host: str) -> bool:
        return host in {"127.0.0.1", "localhost", "::1"}

    def _print_event(self, message: dict[str, Any]) -> None:
        event = message.get("event")
        if event == "body_input":
            print(
                "[body_input] "
                f"input={message.get('input')} action={message.get('action')} "
                f"source={message.get('source')} intensity={message.get('intensity')} "
                f"x={message.get('x')} y={message.get('y')} intent={message.get('intent')}"
            )
            return
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

    def _apply_auto_reaction(self, message: dict[str, Any]) -> None:
        event = message.get("event")
        if event == "body_input":
            input_kind = message.get("input")
            action = message.get("action")
            source = message.get("source")
            if input_kind == "touch":
                if action in {"press", "contact"}:
                    self._auto_send("pressure_press", 1.5, {"action": "presence", "state": "listening", "emotion": "happy"})
                elif action == "hold":
                    self._auto_send("pressure_hold", 3.0, {"action": "presence", "state": "speaking", "emotion": "love", "mouth": True})
                    self._auto_send("pressure_hold_motion", 3.0, {"action": "motion", "gesture": "nod"})
                elif action == "release":
                    self._auto_send("pressure_release", 1.5, {"action": "presence", "state": "online_idle", "emotion": "happy"})
            elif input_kind == "gesture":
                if action == "double_tap":
                    self._auto_send("gesture_double_tap", 1.5, {"action": "presence", "state": "listening", "emotion": "surprised"})
                elif action == "long_press":
                    self._auto_send("gesture_long_press", 2.0, {"action": "sleep", "enabled": True})
            elif input_kind == "button" and action == "press":
                if source == "A":
                    self._auto_send("button_a", 1.0, {"action": "presence", "state": "listening", "emotion": "normal"})
                elif source == "B":
                    self._auto_send("button_b", 1.0, {"action": "presence", "state": "online_idle", "emotion": "normal"})
                elif source == "C":
                    self._auto_send("button_c", 1.0, {"action": "motion", "gesture": "center"})
            elif input_kind == "motion" and action == "shake":
                self._auto_send("body_shake", 2.0, {"action": "presence", "state": "listening", "emotion": "surprised"})
            return

        if event == "pressure":
            action = message.get("action")
            if action == "press":
                self._auto_send("pressure_press", 1.5, {"action": "presence", "state": "listening", "emotion": "happy"})
            elif action == "hold":
                self._auto_send("pressure_hold", 3.0, {"action": "presence", "state": "speaking", "emotion": "love", "mouth": True})
                self._auto_send("pressure_hold_motion", 3.0, {"action": "motion", "gesture": "nod"})
            elif action == "release":
                self._auto_send("pressure_release", 1.5, {"action": "presence", "state": "online_idle", "emotion": "happy"})
            return

        if event == "button" and message.get("action") == "press":
            pin = message.get("pin")
            if pin == "A":
                self._auto_send("button_a", 1.0, {"action": "presence", "state": "listening", "emotion": "normal"})
            elif pin == "B":
                self._auto_send("button_b", 1.0, {"action": "presence", "state": "online_idle", "emotion": "normal"})
            elif pin == "C":
                self._auto_send("button_c", 1.0, {"action": "motion", "gesture": "center"})
            return

        if event == "gesture":
            gesture = message.get("gesture")
            if gesture == "double_tap":
                self._auto_send("gesture_double_tap", 1.5, {"action": "presence", "state": "listening", "emotion": "surprised"})
            elif gesture == "long_press":
                self._auto_send("gesture_long_press", 2.0, {"action": "sleep", "enabled": True})

    def _auto_send(self, key: str, cooldown_s: float, payload: dict[str, Any]) -> None:
        now = time.time()
        last = self._last_auto_reaction.get(key, 0.0)
        if now - last < cooldown_s:
            return
        self._last_auto_reaction[key] = now
        self.send_command(payload)

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
        if line == "/events":
            print(json.dumps(self.recent_events(limit=20), ensure_ascii=False, indent=2))
            return
        try:
            payload = self.payload_from_console_line(line)
        except (ValueError, json.JSONDecodeError) as exc:
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
        print("  /events")
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
            f"heartbeat={heartbeat} queued={self._queued_count()} auto_react={self.auto_react}"
        )

    def _queued_count(self) -> int:
        with self._queue_lock:
            return len(self._send_queue)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="OpenClaw StackChan Windows bridge")
    parser.add_argument("--host", default=DEFAULT_HOST, help="CoreS3 listen host, default 0.0.0.0")
    parser.add_argument("--port", default=DEFAULT_PORT, type=int, help="CoreS3 listen port, default 8765")
    parser.add_argument("--control-host", default=DEFAULT_CONTROL_HOST, help="local API host, default 127.0.0.1")
    parser.add_argument("--control-port", default=DEFAULT_CONTROL_PORT, type=int, help="local API port, default 8766")
    parser.add_argument("--ws-host", default=None, help="optional WebSocket API host, for example 0.0.0.0")
    parser.add_argument("--ws-port", default=DEFAULT_WS_PORT, type=int, help="WebSocket API port, default 8767")
    parser.add_argument("--mqtt-host", default=os.environ.get("OPENCLAW_MQTT_HOST"), help="optional MQTT broker host")
    parser.add_argument("--mqtt-port", default=int(os.environ.get("OPENCLAW_MQTT_PORT", "1883")), type=int, help="MQTT broker port, default 1883")
    parser.add_argument("--mqtt-topic-prefix", default=os.environ.get("OPENCLAW_MQTT_TOPIC_PREFIX", "openclaw/home"), help="MQTT topic prefix")
    parser.add_argument("--mqtt-client-id", default=os.environ.get("OPENCLAW_MQTT_CLIENT_ID", "openclaw-stackchan-bridge"), help="MQTT client id")
    parser.add_argument("--mqtt-username", default=os.environ.get("OPENCLAW_MQTT_USERNAME"), help="optional MQTT username")
    parser.add_argument("--mqtt-password", default=os.environ.get("OPENCLAW_MQTT_PASSWORD"), help="optional MQTT password")
    parser.add_argument("--tts-voice", default=os.environ.get("OPENCLAW_TTS_VOICE", "zh-CN-YunxiNeural"), help="Edge-TTS voice, for example zh-CN-YunxiNeural")
    parser.add_argument("--event-log", default=None, help="optional JSONL event log path")
    parser.add_argument("--control-token", default=os.environ.get("OPENCLAW_BRIDGE_TOKEN"), help="required token when control API is not localhost")
    parser.add_argument("--no-auto-react", action="store_true", help="disable simple built-in body reactions")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    mqtt_config = None
    if args.mqtt_host:
        mqtt_config = MqttConfig(
            host=args.mqtt_host,
            port=args.mqtt_port,
            client_id=args.mqtt_client_id,
            topic_prefix=args.mqtt_topic_prefix,
            username=args.mqtt_username,
            password=args.mqtt_password,
        )
    bridge = StackChanBridge(
        host=args.host,
        port=args.port,
        control_host=args.control_host,
        control_port=args.control_port,
        auto_react=not args.no_auto_react,
        event_log=args.event_log,
        control_token=args.control_token,
        ws_host=args.ws_host,
        ws_port=args.ws_port,
        mqtt_config=mqtt_config,
        tts_voice=args.tts_voice,
    )
    return 0 if bridge.start() else 1


if __name__ == "__main__":
    raise SystemExit(main())
