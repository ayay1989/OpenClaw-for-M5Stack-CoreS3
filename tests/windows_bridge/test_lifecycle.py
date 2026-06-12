from __future__ import annotations

import sys
import unittest
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "windows_bridge"))

from openclaw_bridge import DemoBrainAdapter, LifeCycleManager, LifeState, SystemTts, summarize_intents  # noqa: E402


class FakeBody:
    def __init__(self) -> None:
        self.calls: list[tuple[str, tuple[Any, ...]]] = []

    def set_presence(self, state: str, emotion: str | None = None, mouth: bool | None = None) -> None:
        self.calls.append(("set_presence", (state, emotion, mouth)))

    def breath(self, r: int, g: int, b: int, speed: int = 3) -> None:
        self.calls.append(("breath", (r, g, b, speed)))

    def motion(self, gesture: str) -> None:
        self.calls.append(("motion", (gesture,)))

    def start_thinking(self) -> None:
        self.calls.append(("start_thinking", ()))

    def start_speaking(self, emotion: str = "happy") -> None:
        self.calls.append(("start_speaking", (emotion,)))

    def stop_speaking(self) -> None:
        self.calls.append(("stop_speaking", ()))


class LifeCycleTest(unittest.TestCase):
    def make_manager(self) -> tuple[LifeCycleManager, FakeBody]:
        body = FakeBody()
        manager = LifeCycleManager(body=body, brain=DemoBrainAdapter(), tts=SystemTts(False), sleep_timeout_s=10.0, proactive_cooldown_s=0.0)
        return manager, body

    def test_idle_timeout_enters_sleep(self) -> None:
        manager, body = self.make_manager()
        manager.wake(1.0)
        manager.tick(12.0)
        self.assertEqual(manager.state, LifeState.SLEEPING)
        self.assertIn(("set_presence", ("sleeping", "sleepy", False)), body.calls)

    def test_touch_press_wakes(self) -> None:
        manager, body = self.make_manager()
        manager.sleep()
        actions = manager.handle_events([{"kind": "pressure", "message": {"event": "pressure", "action": "press"}}], now=20.0)
        self.assertEqual(manager.state, LifeState.AWAKE)
        self.assertEqual(actions, [])
        self.assertIn(("set_presence", ("online_idle", "happy", False)), body.calls)

    def test_hold_contact_creates_proactive_action(self) -> None:
        manager, _ = self.make_manager()
        actions = manager.handle_events([{"kind": "pressure", "message": {"event": "pressure", "action": "hold"}}], now=1.0)
        self.assertEqual(len(actions), 1)
        self.assertEqual(actions[0].intent.action, "comfort_contact")

    def test_speak_proactively_drives_body(self) -> None:
        manager, body = self.make_manager()
        actions = manager.handle_events([{"kind": "pressure", "message": {"event": "pressure", "action": "hold"}}], now=1.0)
        reply = manager.speak_proactively(actions[0], [{"kind": "pressure", "message": {"event": "pressure", "action": "hold"}}])
        self.assertEqual(reply.emotion, "love")
        self.assertEqual(manager.state, LifeState.AWAKE)
        self.assertIn(("start_thinking", ()), body.calls)
        self.assertIn(("stop_speaking", ()), body.calls)

    def test_status_overlay_lines(self) -> None:
        manager, _ = self.make_manager()
        manager.wake(5.0)
        status = manager.status(8.0, {"state": {"connected": True}})
        lines = status.overlay_lines()
        self.assertIn("state=awake", lines)
        self.assertIn("body=online", lines)

    def test_summarize_intents(self) -> None:
        summary = summarize_intents(
            [
                {"kind": "button", "message": {"event": "button", "pin": "B", "action": "press"}},
                {"kind": "gesture", "message": {"event": "gesture", "gesture": "double_tap"}},
            ]
        )
        self.assertEqual(summary, ["button:interrupt", "gesture:summon"])


if __name__ == "__main__":
    unittest.main()
