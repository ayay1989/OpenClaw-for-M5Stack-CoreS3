"""Speech timing helpers for StackChan body synchronization."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class SpeechCue:
    text: str
    emotion: str
    estimated_duration_s: float
    mouth: bool = True


def estimate_speech_duration_s(text: str, chars_per_second: float = 7.0) -> float:
    stripped = text.strip()
    if not stripped:
        return 0.0
    duration = len(stripped) / max(chars_per_second, 1.0)
    return round(min(max(duration, 0.8), 30.0), 2)


def build_speech_cue(text: str, emotion: str = "happy") -> SpeechCue:
    return SpeechCue(text=text, emotion=emotion, estimated_duration_s=estimate_speech_duration_s(text))
