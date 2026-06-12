from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "windows_bridge"))

from openclaw_bridge import (  # noqa: E402
    AudioFormat,
    DeviceRegistry,
    EnergyVad,
    KeywordWakeWord,
    MemoryTransport,
    mqtt_publish_packet,
    OutboundMessage,
    StackChanDevice,
    VoiceFrame,
    make_microphone_stream,
    mqtt_endpoint,
    transport_from_config,
    websocket_endpoint,
    websocket_text_frame,
)


class HomeNetworkSkeletonTest(unittest.TestCase):
    def test_websocket_and_mqtt_endpoints_describe_concepts(self) -> None:
        ws = websocket_endpoint("wss://bridge.local/stackchan", room="desk", device_id="desk-1")
        mqtt = mqtt_endpoint("mqtt://broker.local", room="living_room", device_id="living-1", topic_prefix="openclaw/home")
        self.assertTrue(ws.is_websocket())
        self.assertFalse(ws.is_mqtt())
        self.assertTrue(mqtt.is_mqtt())
        self.assertEqual(mqtt.command_topic(), "openclaw/home/devices/living-1/command")
        self.assertEqual(mqtt.command_topic("kitchen-1"), "openclaw/home/devices/kitchen-1/command")

    def test_transport_config_accepts_websocket_and_mqtt(self) -> None:
        endpoint = transport_from_config({"kind": "mqtt", "url": "mqtt://broker.local", "options": {"topic_prefix": "home/openclaw"}})
        self.assertTrue(endpoint.is_mqtt())
        self.assertEqual(endpoint.command_topic("desk"), "home/openclaw/devices/desk/command")

    def test_transport_wire_helpers(self) -> None:
        self.assertEqual(websocket_text_frame("hi"), b"\x81\x02hi")
        packet = mqtt_publish_packet("openclaw/home/devices/desk/command", b'{"action":"ping"}')
        self.assertEqual(packet[0], 0x30)
        self.assertIn(b"openclaw/home/devices/desk/command", packet)

    def test_memory_transport_collects_enveloped_messages(self) -> None:
        endpoint = mqtt_endpoint("mqtt://broker.local")
        transport = MemoryTransport(endpoint)
        message = OutboundMessage(endpoint.command_topic("desk"), {"action": "beep"}, qos=1)
        transport.publish(message)
        self.assertEqual(transport.messages[0].to_envelope(), {"topic": "openclaw/home/devices/desk/command", "payload": {"action": "beep"}, "qos": 1})

    def test_registry_routes_multiple_devices_by_room_and_feature(self) -> None:
        registry = DeviceRegistry()
        registry.upsert_from_hello({"device": {"device_id": "old", "room": "living"}, "features": {"body": True}}, now=1.0)
        registry.upsert_from_hello({"device": {"device_id": "new", "room": "living"}, "features": {"body": True, "audio_in": True}}, now=5.0)
        registry.upsert_from_hello({"device": {"device_id": "desk", "room": "desk"}, "features": {"body": True}}, now=5.0)
        targets = registry.route_targets(room="living", feature="audio_in", now=6.0)
        self.assertEqual([target.device_id for target in targets], ["new"])
        messages = registry.command_messages({"action": "look", "yaw": 0, "pitch": 30}, room="living", feature="body", now=6.0)
        self.assertEqual([message["device_id"] for message in messages], ["new", "old"])

    def test_registry_filters_stale_devices_by_default(self) -> None:
        registry = DeviceRegistry()
        registry.upsert_from_hello({"device": {"device_id": "stale", "room": "living"}, "features": {"body": True}}, now=1.0)
        self.assertEqual(registry.route_targets(room="living", feature="body", now=100.0), [])
        self.assertEqual(registry.route_targets(room="living", feature="body", now=100.0, include_stale=True)[0].device_id, "stale")

    def test_audio_stream_session_and_voice_abstractions(self) -> None:
        stream = make_microphone_stream("mic-1", sample_rate=16000, channels=1)
        self.assertEqual(stream.audio_format, AudioFormat(sample_rate=16000, channels=1))
        stream.add_chunk(b"\x00\x01" * 160)
        self.assertGreater(stream.estimated_duration_s(), 0)
        vad = EnergyVad(threshold=0.2, interrupt_threshold=0.8)
        wakeword = KeywordWakeWord(["openclaw"])
        frame = VoiceFrame(energy=0.9, text_hint="OpenClaw wake up")
        self.assertTrue(vad.decide(frame, speaking=True).interrupt)
        self.assertTrue(wakeword.detect(frame).wake)


if __name__ == "__main__":
    unittest.main()
