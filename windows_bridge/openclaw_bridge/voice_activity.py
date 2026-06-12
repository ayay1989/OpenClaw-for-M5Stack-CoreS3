"""Wake word, VAD, interruption, and echo-control abstractions."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class VoiceFrame:
    energy: float
    text_hint: str = ""
    timestamp: float = 0.0


@dataclass(frozen=True)
class VoiceDecision:
    active: bool
    wake: bool = False
    interrupt: bool = False
    reason: str = ""


class EnergyVad:
    def __init__(self, threshold: float = 0.2, interrupt_threshold: float = 0.8) -> None:
        self.threshold = threshold
        self.interrupt_threshold = interrupt_threshold

    def decide(self, frame: VoiceFrame, speaking: bool = False) -> VoiceDecision:
        active = frame.energy >= self.threshold
        interrupt = speaking and frame.energy >= self.interrupt_threshold
        return VoiceDecision(active=active, interrupt=interrupt, reason="energy")


class KeywordWakeWord:
    def __init__(self, keywords: list[str] | None = None) -> None:
        self.keywords = [keyword.lower() for keyword in (keywords or ["stackchan", "openclaw", "槐序"])]

    def detect(self, frame: VoiceFrame) -> VoiceDecision:
        text = frame.text_hint.lower()
        wake = any(keyword in text for keyword in self.keywords)
        return VoiceDecision(active=bool(text), wake=wake, reason="keyword" if wake else "no_keyword")


@dataclass(frozen=True)
class EchoControlPlan:
    enabled: bool = False
    playback_reference: str | None = None
    microphone_source: str | None = None

    def requires_reference(self) -> bool:
        return self.enabled and bool(self.playback_reference and self.microphone_source)
