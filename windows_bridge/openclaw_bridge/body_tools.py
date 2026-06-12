"""MCP-style body tools OpenClaw can call through the Windows bridge."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable

from .body_client import StackChanBodyClient


@dataclass(frozen=True)
class BodyTool:
    name: str
    description: str
    parameters: dict[str, Any]


@dataclass(frozen=True)
class BodyToolResult:
    ok: bool
    tool: str
    device_command: dict[str, Any] | None = None
    message: str = ""


def _int_arg(args: dict[str, Any], name: str, default: int, minimum: int, maximum: int) -> int:
    value = int(args.get(name, default))
    return max(minimum, min(maximum, value))


class BodyToolRouter:
    def __init__(self, body: StackChanBodyClient) -> None:
        self.body = body
        self._handlers: dict[str, Callable[[dict[str, Any]], dict[str, Any]]] = {
            "self.emotion.set": self._emotion,
            "self.presence.set": self._presence,
            "self.led.set_color": self._led,
            "self.led.breath": self._breath,
            "self.motion.look_at": self._look_at,
            "self.motion.center": lambda _args: self.body.motion("center"),
            "self.motion.nod": lambda _args: self.body.motion("nod"),
            "self.motion.shake": lambda _args: self.body.motion("shake"),
            "self.motion.tilt": lambda _args: self.body.motion("tilt"),
            "self.audio.beep": self._beep,
            "self.audio.stream_start": self._audio_stream_start,
            "self.audio.stream_stop": self._audio_stream_stop,
            "self.sleep.set": self._sleep,
            "self.memory.cue": self._memory_cue,
            "self.experience.start_speaking": self._experience_start_speaking,
            "self.experience.react_to_touch": self._experience_react_to_touch,
            "self.experience.sleep_mode": self._experience_sleep_mode,
        }

    def list_tools(self, features: dict[str, Any] | None = None) -> list[BodyTool]:
        features = features or {}
        tools = [
            BodyTool("self.emotion.set", "Set StackChan face emotion.", {"emotion": "happy|normal|sad|angry|surprised|sleepy|shy|love"}),
            BodyTool("self.presence.set", "Set resident presence state.", {"state": "listening|thinking|speaking|online_idle|sleeping", "emotion": "optional", "mouth": "optional boolean"}),
            BodyTool("self.led.set_color", "Set RGB LED color.", {"r": "0..255", "g": "0..255", "b": "0..255"}),
            BodyTool("self.led.breath", "Set RGB breathing light.", {"r": "0..255", "g": "0..255", "b": "0..255", "speed": "1..10"}),
            BodyTool("self.sleep.set", "Enter or leave quiet sleep state.", {"enabled": "boolean"}),
            BodyTool("self.memory.cue", "Show that OpenClaw memory context is active.", {"emotion": "optional", "ttl_ms": "100..60000"}),
            BodyTool("self.experience.start_speaking", "Start a speaking body state with emotion and mouth enabled.", {"emotion": "optional"}),
            BodyTool("self.experience.react_to_touch", "Use a warm touch reaction preset.", {"emotion": "optional"}),
            BodyTool("self.experience.sleep_mode", "Use the quiet sleep body preset.", {}),
        ]
        if features.get("motion", False) or features.get("servo", False):
            tools.extend(
                [
                    BodyTool("self.motion.look_at", "Look at a yaw/pitch target.", {"yaw": "-45..45", "pitch": "5..60", "duration_ms": "50..3000"}),
                    BodyTool("self.motion.center", "Return head to center.", {}),
                    BodyTool("self.motion.nod", "Nod head.", {}),
                    BodyTool("self.motion.shake", "Shake head.", {}),
                    BodyTool("self.motion.tilt", "Tilt head.", {}),
                ]
            )
        if features.get("audio_out", False):
            tools.append(BodyTool("self.audio.beep", "Play a short local beep.", {"freq": "80..4000", "duration_ms": "20..2000", "volume": "0..100"}))
        if features.get("audio_stream_out", False):
            tools.extend(
                [
                    BodyTool("self.audio.stream_start", "Start a future CoreS3 PCM stream.", {"stream_id": "string", "sample_rate": "8000..48000", "channels": "1..2"}),
                    BodyTool("self.audio.stream_stop", "Stop a future CoreS3 PCM stream.", {"stream_id": "string"}),
                ]
            )
        return tools

    def call(self, name: str, args: dict[str, Any] | None = None) -> BodyToolResult:
        handler = self._handlers.get(name)
        if handler is None:
            return BodyToolResult(False, name, message="unknown body tool")
        try:
            response = handler(args or {})
        except (TypeError, ValueError) as exc:
            return BodyToolResult(False, name, message=str(exc))
        command = response.get("device_command") if isinstance(response, dict) else None
        return BodyToolResult(bool(response.get("ok", True)), name, command if isinstance(command, dict) else None)

    def _emotion(self, args: dict[str, Any]) -> dict[str, Any]:
        return self.body.set_emotion(str(args.get("emotion") or args.get("value") or "happy"))

    def _presence(self, args: dict[str, Any]) -> dict[str, Any]:
        state = str(args.get("state") or "online_idle")
        emotion = args.get("emotion")
        mouth = args.get("mouth")
        return self.body.set_presence(state, str(emotion) if emotion is not None else None, bool(mouth) if mouth is not None else None)

    def _led(self, args: dict[str, Any]) -> dict[str, Any]:
        return self.body.led(_int_arg(args, "r", 0, 0, 255), _int_arg(args, "g", 0, 0, 255), _int_arg(args, "b", 0, 0, 255))

    def _breath(self, args: dict[str, Any]) -> dict[str, Any]:
        return self.body.breath(
            _int_arg(args, "r", 0, 0, 255),
            _int_arg(args, "g", 100, 0, 255),
            _int_arg(args, "b", 255, 0, 255),
            _int_arg(args, "speed", 3, 1, 10),
        )

    def _look_at(self, args: dict[str, Any]) -> dict[str, Any]:
        return self.body.look_at(
            _int_arg(args, "yaw", 0, -45, 45),
            _int_arg(args, "pitch", 30, 5, 60),
            _int_arg(args, "duration_ms", 350, 50, 3000),
        )

    def _beep(self, args: dict[str, Any]) -> dict[str, Any]:
        return self.body.beep(
            _int_arg(args, "freq", int(args.get("frequency_hz", 880)), 80, 4000),
            _int_arg(args, "duration_ms", 120, 20, 2000),
            _int_arg(args, "volume", 30, 0, 100),
        )

    def _audio_stream_start(self, args: dict[str, Any]) -> dict[str, Any]:
        stream_id = str(args.get("stream_id") or "tts-1")
        return self.body.audio_stream_start(
            stream_id,
            direction=str(args.get("direction") or "tts_out"),
            sample_rate=_int_arg(args, "sample_rate", 24000, 8000, 48000),
            channels=_int_arg(args, "channels", 1, 1, 2),
            fmt=str(args.get("format") or "pcm_s16le"),
        )

    def _audio_stream_stop(self, args: dict[str, Any]) -> dict[str, Any]:
        return self.body.audio_stream_stop(str(args.get("stream_id") or "tts-1"))

    def _sleep(self, args: dict[str, Any]) -> dict[str, Any]:
        return self.body.device_command({"action": "sleep", "enabled": bool(args.get("enabled", True))})

    def _memory_cue(self, args: dict[str, Any]) -> dict[str, Any]:
        return self.body.device_command(
            {
                "action": "memory_cue",
                "emotion": str(args.get("emotion") or "love"),
                "ttl_ms": _int_arg(args, "ttl_ms", 3000, 100, 60000),
            }
        )

    def _experience_start_speaking(self, args: dict[str, Any]) -> dict[str, Any]:
        return self.body.set_presence("speaking", str(args.get("emotion") or "happy"), mouth=True)

    def _experience_react_to_touch(self, args: dict[str, Any]) -> dict[str, Any]:
        response = self.body.set_presence("speaking", str(args.get("emotion") or "love"), mouth=True)
        self.body.motion("nod")
        return response

    def _experience_sleep_mode(self, args: dict[str, Any]) -> dict[str, Any]:
        response = self.body.set_presence("sleeping", "sleepy", mouth=False)
        self.body.breath(0, 20, 60, 2)
        self.body.motion("center")
        return response
