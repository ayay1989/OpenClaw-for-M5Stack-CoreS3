from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "windows_bridge"))

from openclaw_bridge import (  # noqa: E402
    DeviceRegistry,
    EchoControlPlan,
    EnergyVad,
    FaceObservation,
    KeywordWakeWord,
    MemoryTransport,
    OtaPackage,
    OtaPlan,
    OutboundMessage,
    TransportEndpoint,
    VoiceFrame,
    make_microphone_stream,
    make_tts_stream,
    transport_from_config,
)
from openclaw_bridge.runtime import RuntimeConfig, build_audio_in_format, build_audio_out_format  # noqa: E402


class HomeNetworkCapabilitiesTest(unittest.TestCase):
    def test_memory_transport_records_messages(self) -> None:
        transport = MemoryTransport(TransportEndpoint(kind="websocket", url="ws://127.0.0.1:8767", room="desk"))
        message = OutboundMessage("desk/stackchan/command", {"action": "emotion", "value": "love"})
        transport.publish(message)
        self.assertEqual(transport.messages[0].to_json_line(), '{"action":"emotion","value":"love"}\n')
        transport.close()
        with self.assertRaises(RuntimeError):
            transport.publish(message)

    def test_transport_config_parses_websocket_and_mqtt(self) -> None:
        ws = transport_from_config({"kind": "websocket", "url": "ws://127.0.0.1:8767", "room": "desk", "secure": False})
        mqtt = transport_from_config({"kind": "mqtt", "url": "mqtt://127.0.0.1:1883", "room": "living"})
        self.assertEqual(ws.kind, "websocket")
        self.assertEqual(mqtt.room, "living")

    def test_device_registry_routes_by_room_or_device(self) -> None:
        registry = DeviceRegistry()
        registry.upsert_from_hello({"name": "openclaw-stackchan", "room": "desk", "features": {"motion": True}}, now=1.0)
        registry.upsert_from_hello({"device": {"device_id": "living-1", "room": "living", "name": "Living StackChan"}}, endpoint="tcp://living", now=2.0)
        self.assertEqual(registry.route_target(room="living").device_id, "living-1")
        self.assertEqual(registry.route_target(device_id="openclaw-stackchan").room, "desk")
        self.assertEqual(len(registry.snapshot(now=40.0)), 2)

    def test_audio_stream_sessions_track_duration(self) -> None:
        tts = make_tts_stream("tts-1", sample_rate=24000, channels=1)
        tts.add_chunk(b"\x00" * 48000)
        self.assertEqual(tts.estimated_duration_s(), 1.0)
        tts.close()
        with self.assertRaises(RuntimeError):
            tts.add_chunk(b"\x00")
        mic = make_microphone_stream("mic-1")
        self.assertEqual(mic.audio_format.sample_rate, 16000)

    def test_voice_activity_and_wakeword(self) -> None:
        vad = EnergyVad(threshold=0.2, interrupt_threshold=0.8)
        self.assertTrue(vad.decide(VoiceFrame(0.3)).active)
        self.assertTrue(vad.decide(VoiceFrame(0.9), speaking=True).interrupt)
        wake = KeywordWakeWord(["槐序"])
        self.assertTrue(wake.detect(VoiceFrame(0.1, text_hint="槐序醒醒")).wake)

    def test_echo_control_plan_requires_reference(self) -> None:
        self.assertFalse(EchoControlPlan(enabled=True, playback_reference="speaker").requires_reference())
        self.assertTrue(EchoControlPlan(enabled=True, playback_reference="speaker", microphone_source="mic").requires_reference())

    def test_ota_plan_requires_existing_package_and_hash(self) -> None:
        with tempfile.NamedTemporaryFile("wb", delete=False) as fh:
            fh.write(b"firmware")
            path = fh.name
        package = OtaPackage(path=path, version="1.0.0", sha256="a" * 64)
        plan = OtaPlan(package=package, target_device_id="desk-1")
        self.assertTrue(plan.ready_for_dispatch())
        bad = OtaPlan(package=OtaPackage(path=path, version="1.0.0", sha256="bad"), target_device_id="desk-1")
        self.assertFalse(bad.ready_for_dispatch())

    def test_runtime_config_loads_home_network_fields(self) -> None:
        config = RuntimeConfig.from_dict(
            {
                "audio_out_sample_rate": 22050,
                "audio_in_sample_rate": 8000,
                "wake_words": ["hello"],
                "transports": [{"kind": "mqtt", "url": "mqtt://127.0.0.1:1883"}],
            }
        )
        self.assertEqual(build_audio_out_format(config).sample_rate, 22050)
        self.assertEqual(build_audio_in_format(config).sample_rate, 8000)
        self.assertEqual(config.wake_words, ["hello"])
        self.assertEqual(config.transports[0].kind, "mqtt")


if __name__ == "__main__":
    unittest.main()
