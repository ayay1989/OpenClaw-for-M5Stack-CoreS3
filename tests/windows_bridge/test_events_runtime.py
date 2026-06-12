from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "windows_bridge"))

from openclaw_bridge import intent_from_event, strongest_intent  # noqa: E402
from openclaw_bridge.runtime import RuntimeConfig, face_simulation_targets, load_config  # noqa: E402


class EventsRuntimeTest(unittest.TestCase):
    def test_pressure_and_button_events_map_to_intents(self) -> None:
        hold = intent_from_event({"kind": "pressure", "message": {"event": "pressure", "action": "hold"}})
        self.assertIsNotNone(hold)
        assert hold is not None
        self.assertEqual(hold.action, "comfort_contact")
        interrupt = intent_from_event({"kind": "button", "message": {"event": "button", "pin": "B", "action": "press"}})
        self.assertIsNotNone(interrupt)
        assert interrupt is not None
        self.assertEqual(interrupt.action, "interrupt")

    def test_strongest_intent_prefers_priority(self) -> None:
        intent = strongest_intent(
            [
                {"kind": "heartbeat", "message": {"event": "heartbeat"}},
                {"kind": "pressure", "message": {"event": "pressure", "action": "hold"}},
                {"kind": "button", "message": {"event": "button", "pin": "B", "action": "press"}},
            ]
        )
        self.assertIsNotNone(intent)
        assert intent is not None
        self.assertEqual(intent.action, "interrupt")

    def test_runtime_config_loads_from_json(self) -> None:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", suffix=".json", delete=False) as fh:
            json.dump({"bridge_url": "http://127.0.0.1:9999", "tts_enabled": True, "face_yaw_limit": 25}, fh)
            path = fh.name
        config = load_config(path)
        self.assertEqual(config.bridge_url, "http://127.0.0.1:9999")
        self.assertTrue(config.tts_enabled)
        self.assertEqual(config.face_yaw_limit, 25)

    def test_face_simulation_targets_are_safe(self) -> None:
        targets = face_simulation_targets(RuntimeConfig(face_yaw_limit=30))
        self.assertGreaterEqual(len(targets), 2)
        for target in targets:
            self.assertGreaterEqual(target.yaw, -30)
            self.assertLessEqual(target.yaw, 30)
            self.assertGreaterEqual(target.pitch, 5)
            self.assertLessEqual(target.pitch, 60)
        self.assertEqual((targets[-1].yaw, targets[-1].pitch), (0, 30))


if __name__ == "__main__":
    unittest.main()
