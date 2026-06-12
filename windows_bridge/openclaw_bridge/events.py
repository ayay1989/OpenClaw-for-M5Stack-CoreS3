"""Normalize StackChan body events into OpenClaw-facing intents."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class BodyIntent:
    kind: str
    action: str
    description: str
    priority: int = 1


def message_from_event(event: dict[str, Any]) -> dict[str, Any]:
    message = event.get("message")
    if isinstance(message, dict):
        return message
    return event


def intent_from_event(event: dict[str, Any]) -> BodyIntent | None:
    message = message_from_event(event)
    kind = str(event.get("kind") or message.get("event") or "")

    if kind == "body_input":
        input_kind = str(message.get("input") or "")
        action = str(message.get("action") or "")
        if input_kind == "touch":
            if action in {"press", "contact"}:
                return BodyIntent("body_input", "contact_started", "User touched StackChan.", priority=2)
            if action == "hold":
                return BodyIntent("body_input", "comfort_contact", "User is holding or petting StackChan.", priority=3)
            if action == "release":
                return BodyIntent("body_input", "contact_ended", "User stopped touching StackChan.", priority=1)
        if input_kind == "gesture":
            if action == "double_tap":
                return BodyIntent("body_input", "summon", "Double tap summons OpenClaw attention.", priority=3)
            if action == "long_press":
                return BodyIntent("body_input", "sleep_toggle", "Long press toggles quiet or sleep behavior.", priority=3)
            if action.startswith("swipe_"):
                return BodyIntent("body_input", "browse_mood", "Swipe gesture asks to browse mood or UI state.", priority=1)
            if action == "tap":
                return BodyIntent("body_input", "attention", "Tap asks for attention.", priority=2)
        if input_kind == "button":
            if action == "press":
                source = str(message.get("source") or "")
                if source == "A":
                    return BodyIntent("body_input", "wake", "Button A asks OpenClaw to listen.", priority=3)
                if source == "B":
                    return BodyIntent("body_input", "interrupt", "Button B asks OpenClaw to interrupt or stop speaking.", priority=4)
                if source == "C":
                    return BodyIntent("body_input", "safe_action", "Button C asks for a safe local action.", priority=2)
            return None
        if input_kind == "motion" and action == "shake":
            return BodyIntent("body_input", "shake", "StackChan was shaken or moved.", priority=3)
        return BodyIntent("body_input", action or "input", "StackChan received body interaction input.", priority=1)

    if kind == "pressure":
        action = str(message.get("action") or "")
        if action == "press":
            return BodyIntent("tactile", "contact_started", "User touched StackChan.", priority=2)
        if action == "hold":
            return BodyIntent("tactile", "comfort_contact", "User is holding or petting StackChan.", priority=3)
        if action == "release":
            return BodyIntent("tactile", "contact_ended", "User stopped touching StackChan.", priority=1)
        return BodyIntent("tactile", "contact", "StackChan received tactile contact.", priority=1)

    if kind == "button":
        pin = str(message.get("pin") or "")
        action = str(message.get("action") or "")
        if action != "press":
            return None
        if pin == "A":
            return BodyIntent("button", "wake", "Button A asks OpenClaw to listen.", priority=3)
        if pin == "B":
            return BodyIntent("button", "interrupt", "Button B asks OpenClaw to interrupt or stop speaking.", priority=4)
        if pin == "C":
            return BodyIntent("button", "safe_action", "Button C asks for a safe local action.", priority=2)
        return BodyIntent("button", "press", "A body button was pressed.", priority=1)

    if kind == "gesture":
        gesture = str(message.get("gesture") or "")
        if gesture == "double_tap":
            return BodyIntent("gesture", "summon", "Double tap summons OpenClaw attention.", priority=3)
        if gesture == "long_press":
            return BodyIntent("gesture", "sleep_toggle", "Long press toggles quiet or sleep behavior.", priority=3)
        if gesture.startswith("swipe_"):
            return BodyIntent("gesture", "browse_mood", "Swipe gesture asks to browse mood or UI state.", priority=1)
        if gesture == "tap":
            return BodyIntent("gesture", "attention", "Tap asks for attention.", priority=2)

    if kind == "heartbeat":
        return BodyIntent("status", "heartbeat", "StackChan heartbeat received.", priority=0)

    return None


def intents_from_events(events: list[dict[str, Any]]) -> list[BodyIntent]:
    intents: list[BodyIntent] = []
    for event in events:
        intent = intent_from_event(event)
        if intent is not None:
            intents.append(intent)
    return intents


def strongest_intent(events: list[dict[str, Any]]) -> BodyIntent | None:
    intents = intents_from_events(events)
    if not intents:
        return None
    return max(intents, key=lambda intent: intent.priority)
