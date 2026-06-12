from __future__ import annotations

import base64
import os
import json
import socket
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "windows_bridge"))

from openclaw_stackchan_bridge import StackChanBridge  # noqa: E402
from openclaw_stackchan_bridge import MAX_QUEUED_COMMANDS  # noqa: E402
from openclaw_stackchan_bridge import MAX_LINE_BUFFER  # noqa: E402
from openclaw_bridge import StackChanBodyClient  # noqa: E402


class BridgeTest(unittest.TestCase):
    def make_bridge(self, auto_react: bool = True) -> StackChanBridge:
        return StackChanBridge(
            host="127.0.0.1",
            port=0,
            control_host="127.0.0.1",
            control_port=0,
            auto_react=auto_react,
            event_log=None,
        )

    def test_short_commands_build_device_payloads(self) -> None:
        bridge = self.make_bridge()
        self.assertEqual(bridge.payload_from_console_line("/emotion happy"), {"action": "emotion", "value": "happy"})
        self.assertEqual(
            bridge.payload_from_console_line("/look 0 30 400"),
            {"action": "look", "yaw": 0, "pitch": 30, "duration_ms": 400},
        )
        self.assertEqual(
            bridge.payload_from_console_line("/beep 880 120 30"),
            {"action": "beep", "freq": 880, "duration_ms": 120, "volume": 30},
        )

    def test_hello_updates_state_and_queues_ack(self) -> None:
        bridge = self.make_bridge(auto_react=False)
        bridge._handle_line(
            json.dumps(
                {
                    "type": "hello",
                    "features": {"pressure": True, "motion": True},
                    "device": {"device_id": "FAKE", "firmware": "1.0.0"},
                }
            )
        )
        self.assertEqual(bridge.state.device_id, "FAKE")
        self.assertEqual(bridge.state.firmware, "1.0.0")
        self.assertTrue(bridge.state.features["pressure"])
        queued = bridge._send_queue.popleft()
        self.assertEqual(queued["type"], "hello_ack")
        self.assertEqual(queued["resident_id"], "openclaw")

    def test_pressure_press_maps_to_presence_reaction(self) -> None:
        bridge = self.make_bridge(auto_react=True)
        bridge._handle_line(
            json.dumps(
                {
                    "event": "pressure",
                    "source": "touchscreen",
                    "action": "press",
                    "x": 10,
                    "y": 20,
                    "intensity": 40,
                }
            )
        )
        queued = bridge._send_queue.popleft()
        self.assertEqual(queued, {"action": "presence", "state": "listening", "emotion": "happy"})
        events = bridge.recent_events()
        self.assertEqual(events[-1]["kind"], "pressure")

    def test_commands_wait_for_hello_before_socket_send(self) -> None:
        bridge = self.make_bridge(auto_react=False)
        left, right = socket.socketpair()
        try:
            with bridge._client_lock:
                bridge._client = left
                bridge.state.connected = True
                bridge._connection_ready = False
            self.assertFalse(bridge.send_command({"action": "emotion", "value": "love"}))
            right.settimeout(0.1)
            with self.assertRaises(TimeoutError):
                right.recv(1024)
            bridge._handle_line(json.dumps({"type": "hello", "device": {"device_id": "FAKE"}}))
            received = right.recv(4096).decode("utf-8")
            self.assertIn('"type":"hello_ack"', received)
            self.assertIn('"action":"emotion"', received)
        finally:
            left.close()
            right.close()
            bridge.stop()

    def test_queue_drops_oldest_when_full(self) -> None:
        bridge = self.make_bridge(auto_react=False)
        for index in range(MAX_QUEUED_COMMANDS + 5):
            bridge.send_command({"action": "ping", "seq": index})
        self.assertEqual(len(bridge._send_queue), MAX_QUEUED_COMMANDS)
        self.assertEqual(bridge._send_queue[0]["seq"], 5)
        self.assertEqual(bridge._send_queue[-1]["seq"], MAX_QUEUED_COMMANDS + 4)

    def test_oversized_line_buffer_disconnects_client(self) -> None:
        bridge = self.make_bridge(auto_react=False)
        left, right = socket.socketpair()
        try:
            thread = threading.Thread(target=bridge._handle_client, args=(left, ("127.0.0.1", 12345)), daemon=True)
            thread.start()
            right.sendall(b"{" + (b"x" * (MAX_LINE_BUFFER + 1)))
            thread.join(timeout=2.0)
            self.assertFalse(thread.is_alive())
            self.assertFalse(bridge.state.connected)
        finally:
            bridge.stop()
            right.close()

    def test_control_api_status_and_command(self) -> None:
        bridge = self.make_bridge(auto_react=False)
        self.assertTrue(bridge._start_control_server())
        try:
            assert bridge._http_server is not None
            host, port = bridge._http_server.server_address
            with urllib.request.urlopen(f"http://{host}:{port}/status", timeout=3) as response:
                data = json.loads(response.read().decode("utf-8"))
            self.assertTrue(data["ok"])
            payload = json.dumps({"command": "/emotion love"}).encode("utf-8")
            request = urllib.request.Request(
                f"http://{host}:{port}/command",
                data=payload,
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            with urllib.request.urlopen(request, timeout=3) as response:
                result = json.loads(response.read().decode("utf-8"))
            self.assertTrue(result["ok"])
            self.assertTrue(result["queued"])
            self.assertEqual(result["device_command"], {"action": "emotion", "value": "love"})
        finally:
            bridge.stop()

    def test_remote_control_host_requires_token(self) -> None:
        bridge = StackChanBridge(
            host="127.0.0.1",
            port=0,
            control_host="0.0.0.0",
            control_port=0,
            auto_react=False,
            event_log=None,
        )
        self.assertFalse(bridge._start_control_server())

    def test_control_api_token_auth(self) -> None:
        bridge = StackChanBridge(
            host="127.0.0.1",
            port=0,
            control_host="127.0.0.1",
            control_port=0,
            auto_react=False,
            event_log=None,
            control_token="test-value",
        )
        self.assertTrue(bridge._start_control_server())
        try:
            assert bridge._http_server is not None
            host, port = bridge._http_server.server_address
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                urllib.request.urlopen(f"http://{host}:{port}/status", timeout=3)
            self.assertEqual(ctx.exception.code, 401)
            request = urllib.request.Request(f"http://{host}:{port}/status", headers={"X-OpenClaw-Token": "test-value"})
            with urllib.request.urlopen(request, timeout=3) as response:
                data = json.loads(response.read().decode("utf-8"))
            self.assertTrue(data["ok"])
        finally:
            bridge.stop()

    def test_websocket_api_accepts_ping_and_command(self) -> None:
        bridge = StackChanBridge(
            host="127.0.0.1",
            port=0,
            control_host="127.0.0.1",
            control_port=0,
            auto_react=False,
            event_log=None,
            control_token="test-value",
            ws_host="127.0.0.1",
            ws_port=0,
        )
        self.assertTrue(bridge._start_ws_server())
        try:
            assert bridge._ws_server is not None
            host, port = bridge._ws_server.getsockname()
            client = socket.create_connection((host, port), timeout=3)
            try:
                key = base64.b64encode(os.urandom(16)).decode("ascii")
                request = (
                    "GET /bridge?token=test-value HTTP/1.1\r\n"
                    f"Host: {host}:{port}\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    f"Sec-WebSocket-Key: {key}\r\n"
                    "Sec-WebSocket-Version: 13\r\n"
                    "\r\n"
                )
                client.sendall(request.encode("ascii"))
                response = client.recv(4096).decode("ascii", errors="replace")
                self.assertIn("101 Switching Protocols", response)
                welcome = self._ws_read_json(client)
                self.assertEqual(welcome["type"], "welcome")
                client.sendall(self._masked_ws_frame(json.dumps({"type": "ping"}).encode("utf-8")))
                pong = self._ws_read_json(client)
                self.assertEqual(pong["type"], "pong")
                client.sendall(self._masked_ws_frame(json.dumps({"command": "/emotion love"}).encode("utf-8")))
                result = self._ws_read_json(client)
                self.assertEqual(result["type"], "command_result")
                self.assertEqual(result["device_command"], {"action": "emotion", "value": "love"})
                bridge._record_event({"event": "body_input", "input": "touch", "action": "hold"})
                event = self._ws_read_json(client)
                self.assertEqual(event["type"], "event")
                self.assertEqual(event["event"]["kind"], "body_input")
            finally:
                client.close()
        finally:
            bridge.stop()

    def test_remote_websocket_requires_token(self) -> None:
        bridge = StackChanBridge(
            host="127.0.0.1",
            port=0,
            control_host="127.0.0.1",
            control_port=0,
            auto_react=False,
            event_log=None,
            ws_host="0.0.0.0",
            ws_port=0,
        )
        self.assertFalse(bridge._start_ws_server())

    def _masked_ws_frame(self, payload: bytes) -> bytes:
        mask = os.urandom(4)
        length = len(payload)
        if length < 126:
            header = bytes([0x81, 0x80 | length])
        elif length <= 65535:
            header = bytes([0x81, 0x80 | 126]) + length.to_bytes(2, "big")
        else:
            header = bytes([0x81, 0x80 | 127]) + length.to_bytes(8, "big")
        masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        return header + mask + masked

    def _ws_read_json(self, client: socket.socket) -> dict[str, object]:
        header = client.recv(2)
        self.assertEqual(header[0] & 0x0F, 0x1)
        length = header[1] & 0x7F
        if length == 126:
            length = int.from_bytes(client.recv(2), "big")
        elif length == 127:
            length = int.from_bytes(client.recv(8), "big")
        payload = b""
        while len(payload) < length:
            payload += client.recv(length - len(payload))
        data = json.loads(payload.decode("utf-8"))
        self.assertIsInstance(data, dict)
        return data

    def test_event_log_redacts_resident_context(self) -> None:
        with tempfile.NamedTemporaryFile("r", encoding="utf-8", suffix=".jsonl", delete=False) as fh:
            path = fh.name
        bridge = StackChanBridge(
            host="127.0.0.1",
            port=0,
            control_host="127.0.0.1",
            control_port=0,
            auto_react=False,
            event_log=path,
        )
        bridge._record_event({"event": "heartbeat", "session_id": "s1", "resident_id": "openclaw", "memory_context": {"summary": "private"}})
        line = Path(path).read_text(encoding="utf-8").strip()
        event = json.loads(line)
        self.assertEqual(event["message"]["session_id"], "[redacted]")
        self.assertEqual(event["message"]["resident_id"], "[redacted]")
        self.assertEqual(event["message"]["memory_context"], "[redacted]")

    def test_body_client_uses_control_api(self) -> None:
        bridge = self.make_bridge(auto_react=False)
        self.assertTrue(bridge._start_control_server())
        try:
            assert bridge._http_server is not None
            host, port = bridge._http_server.server_address
            client = StackChanBodyClient(f"http://{host}:{port}")
            self.assertTrue(client.status()["ok"])
            result = client.start_speaking("happy")
            self.assertTrue(result["ok"])
            self.assertTrue(result["queued"])
            self.assertEqual(
                result["device_command"],
                {"action": "presence", "state": "speaking", "emotion": "happy", "mouth": True},
            )
            stream = client.audio_stream_start("tts-1")
            self.assertEqual(stream["device_command"]["action"], "audio_stream")
            chunk = client.audio_stream_chunk("tts-1", 1, b"\x00\x00")
            self.assertEqual(chunk["device_command"]["data_b64"], "AAA=")
            self.assertEqual(client.interrupt()["device_command"], {"action": "interrupt", "source": "openclaw"})
        finally:
            bridge.stop()


if __name__ == "__main__":
    unittest.main()
