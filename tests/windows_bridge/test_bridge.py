from __future__ import annotations

import json
import socket
import sys
import unittest
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "windows_bridge"))

from openclaw_stackchan_bridge import StackChanBridge  # noqa: E402
from openclaw_stackchan_bridge import MAX_QUEUED_COMMANDS  # noqa: E402


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


if __name__ == "__main__":
    unittest.main()
