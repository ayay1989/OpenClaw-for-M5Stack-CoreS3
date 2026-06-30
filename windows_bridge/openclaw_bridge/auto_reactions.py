"""Map normalized body intents to local device reactions."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .events import intent_from_event


@dataclass(frozen=True)
class AutoReaction:
    key: str
    cooldown_s: float
    payload: dict[str, Any]


def auto_reactions_for_message(message: dict[str, Any]) -> list[AutoReaction]:
    kind = str(message.get("event") or "")
    intent = intent_from_event({"kind": kind, "message": message})
    if intent is None:
        return []

    if intent.action == "contact_started":
        return [
            AutoReaction("pressure_press", 1.5, {"action": "presence", "state": "listening", "emotion": "happy"}),
        ]
    if intent.action == "comfort_contact":
        return [
            AutoReaction("pressure_hold", 3.0, {"action": "presence", "state": "speaking", "emotion": "love", "mouth": True}),
            AutoReaction("pressure_hold_motion", 3.0, {"action": "motion", "gesture": "nod"}),
        ]
    if intent.action == "contact_ended":
        return [
            AutoReaction("pressure_release", 1.5, {"action": "presence", "state": "online_idle", "emotion": "happy"}),
        ]
    if intent.action == "summon":
        return [
            AutoReaction("gesture_double_tap", 1.5, {"action": "presence", "state": "listening", "emotion": "surprised"}),
        ]
    if intent.action == "sleep_toggle":
        return [
            AutoReaction("gesture_long_press", 2.0, {"action": "sleep", "enabled": True}),
        ]
    if intent.action == "wake":
        return [
            AutoReaction("button_a", 1.0, {"action": "presence", "state": "listening", "emotion": "normal"}),
        ]
    if intent.action == "interrupt":
        return [
            AutoReaction("button_b", 1.0, {"action": "presence", "state": "online_idle", "emotion": "normal"}),
        ]
    if intent.action == "safe_action":
        return [
            AutoReaction("button_c", 1.0, {"action": "motion", "gesture": "center"}),
        ]
    if intent.action == "shake":
        return [
            AutoReaction("body_shake", 2.0, {"action": "presence", "state": "listening", "emotion": "surprised"}),
        ]
    return []
