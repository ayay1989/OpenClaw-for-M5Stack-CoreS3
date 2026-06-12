from __future__ import annotations

import sys
import threading
import unittest
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "windows_bridge"))

from openclaw_bridge import DemoBrainAdapter  # noqa: E402
from openclaw_bridge import HttpBrainAdapter  # noqa: E402
from openclaw_bridge import ResidentConversationLoop  # noqa: E402
from openclaw_bridge import SystemTts  # noqa: E402
from tools.fake_openclaw_brain import make_handler  # noqa: E402
from http.server import ThreadingHTTPServer


class FakeBody:
    def __init__(self) -> None:
        self.calls: list[tuple[str, tuple[Any, ...]]] = []
        self.events: list[dict[str, Any]] = []

    def start_listening(self) -> None:
        self.calls.append(("start_listening", ()))

    def start_thinking(self) -> None:
        self.calls.append(("start_thinking", ()))

    def start_speaking(self, emotion: str = "happy") -> None:
        self.calls.append(("start_speaking", (emotion,)))

    def stop_speaking(self) -> None:
        self.calls.append(("stop_speaking", ()))

    def motion(self, gesture: str) -> None:
        self.calls.append(("motion", (gesture,)))

    def poll_events(self, limit: int = 20, after: float | None = None) -> list[dict[str, Any]]:
        self.calls.append(("poll_events", (limit, after)))
        return self.events


class ResidentLoopTest(unittest.TestCase):
    def test_run_once_drives_body_presence_sequence(self) -> None:
        body = FakeBody()
        loop = ResidentConversationLoop(body=body, brain=DemoBrainAdapter(), tts=SystemTts(False), event_limit=20)
        reply = loop.run_once("你好")
        self.assertIn("你好", reply.text)
        self.assertEqual(
            [name for name, _ in body.calls],
            ["start_listening", "poll_events", "start_thinking", "start_speaking", "motion", "stop_speaking"],
        )
        self.assertEqual(body.calls[3], ("start_speaking", ("happy",)))

    def test_tactile_hold_changes_demo_brain_reply(self) -> None:
        body = FakeBody()
        body.events = [
            {
                "kind": "pressure",
                "message": {"event": "pressure", "action": "hold", "source": "touchscreen", "intensity": 80},
            }
        ]
        loop = ResidentConversationLoop(body=body, brain=DemoBrainAdapter(), tts=SystemTts(False), event_limit=20)
        reply = loop.run_once("在吗")
        self.assertEqual(reply.emotion, "love")
        self.assertIn("摸我", reply.text)

    def test_http_brain_adapter_accepts_fake_openclaw(self) -> None:
        server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler())
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            host, port = server.server_address
            brain = HttpBrainAdapter(f"http://{host}:{port}/chat")
            reply = brain.reply(
                "你好",
                [{"kind": "pressure", "message": {"event": "pressure", "action": "hold"}}],
            )
            self.assertEqual(reply.emotion, "love")
            self.assertIn("fake OpenClaw", reply.text)
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    unittest.main()
