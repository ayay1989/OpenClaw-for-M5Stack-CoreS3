from __future__ import annotations

import json
import socket
import sys
import threading
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "windows_bridge"))

from openclaw_bridge import MqttBusClient, MqttConfig, mqtt_parse_publish, mqtt_publish_packet  # noqa: E402


class MqttBusTest(unittest.TestCase):
    def test_mqtt_client_connects_publishes_and_receives_command(self) -> None:
        received_publish: list[tuple[str, dict[str, object]]] = []
        handler_messages: list[tuple[str, dict[str, object]]] = []
        broker_ready = threading.Event()
        done = threading.Event()

        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        host, port = server.getsockname()

        def broker() -> None:
            broker_ready.set()
            client, _address = server.accept()
            with client:
                packet_type, _payload = self._read_mqtt_packet(client)
                self.assertEqual(packet_type, 0x10)
                client.sendall(b"\x20\x02\x00\x00")
                packet_type, _payload = self._read_mqtt_packet(client)
                self.assertEqual(packet_type, 0x82)
                client.sendall(b"\x90\x03\x00\x01\x00")
                packet_type, payload = self._read_mqtt_packet(client)
                self.assertEqual(packet_type, 0x30)
                topic, raw = mqtt_parse_publish(payload)
                received_publish.append((topic, json.loads(raw.decode("utf-8"))))
                client.sendall(mqtt_publish_packet("openclaw/home/devices/desk/command", b'{"action":"ping"}'))
                done.wait(2)
            server.close()

        thread = threading.Thread(target=broker, daemon=True)
        thread.start()
        self.assertTrue(broker_ready.wait(2))

        client = MqttBusClient(MqttConfig(host=host, port=port), lambda topic, payload: handler_messages.append((topic, payload)))
        client.start()
        try:
            deadline = time.time() + 3
            while time.time() < deadline:
                if client.publish_json("openclaw/home/events", {"type": "event"}):
                    break
                time.sleep(0.05)
            deadline = time.time() + 3
            while time.time() < deadline and not handler_messages:
                time.sleep(0.05)
            self.assertTrue(received_publish)
            self.assertTrue(handler_messages)
            self.assertEqual(received_publish[0], ("openclaw/home/events", {"type": "event"}))
            self.assertEqual(handler_messages[0], ("openclaw/home/devices/desk/command", {"action": "ping"}))
        finally:
            done.set()
            client.stop()
            thread.join(timeout=2)

    def _read_mqtt_packet(self, sock: socket.socket) -> tuple[int, bytes]:
        first = sock.recv(1)
        self.assertTrue(first)
        remaining = self._decode_remaining(sock)
        payload = b""
        while len(payload) < remaining:
            payload += sock.recv(remaining - len(payload))
        return first[0], payload

    def _decode_remaining(self, sock: socket.socket) -> int:
        multiplier = 1
        value = 0
        while True:
            encoded = sock.recv(1)[0]
            value += (encoded & 127) * multiplier
            if encoded & 128 == 0:
                return value
            multiplier *= 128


if __name__ == "__main__":
    unittest.main()
