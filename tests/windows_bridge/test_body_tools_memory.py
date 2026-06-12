from __future__ import annotations

import json
import sys
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "windows_bridge"))

from openclaw_bridge import BodyToolRouter, ExternalCommandAsr, HttpBrainAdapter, MemoryContext, build_speech_cue, estimate_speech_duration_s  # noqa: E402
from openclaw_bridge.runtime import RuntimeConfig, build_memory_context, load_config  # noqa: E402
from tools.fake_openclaw_brain import make_handler  # noqa: E402


class FakeBody:
    def __init__(self) -> None:
        self.commands: list[dict[str, Any]] = []

    def set_emotion(self, emotion: str) -> dict[str, Any]:
        return self.device_command({"action": "emotion", "value": emotion})

    def set_presence(self, state: str, emotion: str | None = None, mouth: bool | None = None) -> dict[str, Any]:
        payload: dict[str, Any] = {"action": "presence", "state": state}
        if emotion is not None:
            payload["emotion"] = emotion
        if mouth is not None:
            payload["mouth"] = mouth
        return self.device_command(payload)

    def led(self, r: int, g: int, b: int) -> dict[str, Any]:
        return self.device_command({"action": "led", "r": r, "g": g, "b": b})

    def breath(self, r: int, g: int, b: int, speed: int = 3) -> dict[str, Any]:
        return self.device_command({"action": "led_effect", "effect": "breath", "r": r, "g": g, "b": b, "speed": speed})

    def look_at(self, yaw: int, pitch: int, duration_ms: int = 350) -> dict[str, Any]:
        return self.device_command({"action": "look", "yaw": yaw, "pitch": pitch, "duration_ms": duration_ms})

    def motion(self, gesture: str) -> dict[str, Any]:
        return self.device_command({"action": "motion", "gesture": gesture})

    def beep(self, freq: int = 880, duration_ms: int = 120, volume: int = 30) -> dict[str, Any]:
        return self.device_command({"action": "beep", "freq": freq, "duration_ms": duration_ms, "volume": volume})

    def device_command(self, payload: dict[str, Any]) -> dict[str, Any]:
        self.commands.append(payload)
        return {"ok": True, "device_command": payload}


class BodyToolsMemoryTest(unittest.TestCase):
    def test_body_tool_router_clamps_and_dispatches(self) -> None:
        body = FakeBody()
        router = BodyToolRouter(body)  # type: ignore[arg-type]
        result = router.call("self.motion.look_at", {"yaw": 99, "pitch": 0, "duration_ms": 9999})
        self.assertTrue(result.ok)
        self.assertEqual(result.device_command, {"action": "look", "yaw": 45, "pitch": 5, "duration_ms": 3000})

    def test_body_tool_router_lists_feature_gated_tools(self) -> None:
        router = BodyToolRouter(FakeBody())  # type: ignore[arg-type]
        names = [tool.name for tool in router.list_tools({"motion": False, "servo": False, "audio_out": False})]
        self.assertIn("self.emotion.set", names)
        self.assertIn("self.experience.react_to_touch", names)
        self.assertNotIn("self.motion.look_at", names)
        self.assertNotIn("self.audio.beep", names)

    def test_experience_touch_tool_runs_preset(self) -> None:
        body = FakeBody()
        router = BodyToolRouter(body)  # type: ignore[arg-type]
        result = router.call("self.experience.react_to_touch", {})
        self.assertTrue(result.ok)
        self.assertEqual(body.commands[0], {"action": "presence", "state": "speaking", "emotion": "love", "mouth": True})
        self.assertEqual(body.commands[1], {"action": "motion", "gesture": "nod"})

    def test_memory_context_loads_and_sanitizes(self) -> None:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", suffix=".json", delete=False) as fh:
            json.dump({"summary": "住进 StackChan", "facts": ["喜欢蓝色"], "preferences": {"voice": "soft"}}, fh, ensure_ascii=False)
            path = fh.name
        config = load_config(path=None)
        config = RuntimeConfig(**{**config.__dict__, "memory_context_path": path})
        context = build_memory_context(config)
        self.assertIsNotNone(context)
        assert context is not None
        self.assertTrue(context.is_loaded())
        self.assertEqual(context.to_brain_payload()["facts"], ["喜欢蓝色"])

    def test_http_brain_adapter_sends_memory_context(self) -> None:
        server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler())
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            host, port = server.server_address
            brain = HttpBrainAdapter(f"http://{host}:{port}/chat")
            reply = brain.reply("你还记得吗", [], MemoryContext(summary="我住在 StackChan 里"))
            self.assertEqual(reply.emotion, "love")
            self.assertIn("我住在 StackChan", reply.text)
        finally:
            server.shutdown()
            server.server_close()

    def test_speech_cue_estimates_duration(self) -> None:
        self.assertGreater(estimate_speech_duration_s("你好 StackChan"), 0.8)
        cue = build_speech_cue("你好", "happy")
        self.assertEqual(cue.emotion, "happy")
        self.assertTrue(cue.mouth)

    def test_external_command_asr_reads_stdout(self) -> None:
        source = ExternalCommandAsr([sys.executable, "-c", "print('你好 StackChan')"])
        transcript = source.listen_once()
        self.assertEqual(transcript.text, "你好 StackChan")
        self.assertEqual(transcript.source, "external_command")

    def test_external_command_asr_reports_errors(self) -> None:
        source = ExternalCommandAsr([sys.executable, "-c", "import sys; print('bad', file=sys.stderr); sys.exit(2)"])
        with self.assertRaisesRegex(RuntimeError, "bad"):
            source.listen_once()


if __name__ == "__main__":
    unittest.main()
