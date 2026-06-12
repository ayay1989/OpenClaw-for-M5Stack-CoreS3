"""Life-cycle behaviors for StackChan as an OpenClaw body."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Any

from .body_client import StackChanBodyClient
from .events import BodyIntent, intent_from_event, intents_from_events
from .memory_context import MemoryContext
from .resident_loop import BrainAdapter, BrainReply, SystemTts


class LifeState(str, Enum):
    AWAKE = "awake"
    LISTENING = "listening"
    THINKING = "thinking"
    SPEAKING = "speaking"
    SLEEPING = "sleeping"


@dataclass
class LifeStatus:
    state: LifeState
    idle_s: float
    memory_context_loaded: bool
    bridge_connected: bool
    openclaw_connected: bool
    face_tracking_enabled: bool
    last_intent: str | None = None

    def overlay_lines(self) -> list[str]:
        return [
            f"state={self.state.value}",
            f"idle={self.idle_s:.1f}s",
            f"body={'online' if self.bridge_connected else 'offline'}",
            f"openclaw={'online' if self.openclaw_connected else 'demo'}",
            f"memory={'loaded' if self.memory_context_loaded else 'not_loaded'}",
            f"face={'tracking' if self.face_tracking_enabled else 'off'}",
            f"intent={self.last_intent or '-'}",
        ]


@dataclass
class ProactiveAction:
    prompt: str
    intent: BodyIntent


class LifeCycleManager:
    def __init__(
        self,
        body: StackChanBodyClient,
        brain: BrainAdapter,
        tts: SystemTts,
        sleep_timeout_s: float = 300.0,
        proactive_cooldown_s: float = 8.0,
        memory_context: MemoryContext | None = None,
    ) -> None:
        self.body = body
        self.brain = brain
        self.tts = tts
        self.sleep_timeout_s = sleep_timeout_s
        self.proactive_cooldown_s = proactive_cooldown_s
        self.state = LifeState.AWAKE
        self.last_activity_at = 0.0
        self.last_proactive_at = 0.0
        self.last_intent: BodyIntent | None = None
        self.memory_context_loaded = False
        self.openclaw_connected = False
        self.face_tracking_enabled = False
        self.memory_context = memory_context
        if memory_context is not None:
            self.memory_context_loaded = memory_context.is_loaded()

    def mark_activity(self, now: float) -> None:
        self.last_activity_at = now

    def wake(self, now: float, emotion: str = "happy") -> None:
        self.state = LifeState.AWAKE
        self.mark_activity(now)
        self.body.set_presence("online_idle", emotion, mouth=False)
        self.body.breath(0, 100, 255, 3)

    def sleep(self) -> None:
        self.state = LifeState.SLEEPING
        self.body.set_presence("sleeping", "sleepy", mouth=False)
        self.body.breath(0, 20, 60, 2)
        self.body.motion("center")

    def tick(self, now: float) -> None:
        if self.state != LifeState.SLEEPING and self.last_activity_at > 0:
            if now - self.last_activity_at >= self.sleep_timeout_s:
                self.sleep()

    def handle_events(self, events: list[dict[str, Any]], now: float) -> list[ProactiveAction]:
        actions: list[ProactiveAction] = []
        for event in events:
            intent = intent_from_event(event)
            if intent is None:
                continue
            self.last_intent = intent
            if intent.priority > 0:
                self.mark_activity(now)
            if intent.action in {"wake", "summon", "contact_started"}:
                self.wake(now)
            elif intent.action == "sleep_toggle":
                if self.state == LifeState.SLEEPING:
                    self.wake(now)
                else:
                    self.sleep()
            elif intent.action == "interrupt":
                self.state = LifeState.AWAKE
                self.body.stop_speaking()
            action = self._proactive_action_for_intent(intent, now)
            if action is not None:
                actions.append(action)
        return actions

    def speak_proactively(self, action: ProactiveAction, recent_events: list[dict[str, Any]]) -> BrainReply:
        self.state = LifeState.THINKING
        self.body.start_thinking()
        reply = self.brain.reply(action.prompt, recent_events, self.memory_context)
        self.state = LifeState.SPEAKING
        self.body.start_speaking(reply.emotion)
        if reply.gesture:
            self.body.motion(reply.gesture)
        self.tts.speak(reply.text)
        self.state = LifeState.AWAKE
        self.body.stop_speaking()
        return reply

    def status(self, now: float, bridge_state: dict[str, Any] | None = None) -> LifeStatus:
        idle = now - self.last_activity_at if self.last_activity_at > 0 else 0.0
        connected = False
        if bridge_state:
            state = bridge_state.get("state")
            if isinstance(state, dict):
                connected = bool(state.get("connected"))
            else:
                connected = bool(bridge_state.get("connected"))
        return LifeStatus(
            state=self.state,
            idle_s=idle,
            memory_context_loaded=self.memory_context_loaded,
            bridge_connected=connected,
            openclaw_connected=self.openclaw_connected,
            face_tracking_enabled=self.face_tracking_enabled,
            last_intent=self.last_intent.action if self.last_intent else None,
        )

    def _proactive_action_for_intent(self, intent: BodyIntent, now: float) -> ProactiveAction | None:
        if now - self.last_proactive_at < self.proactive_cooldown_s:
            return None
        prompts = {
            "comfort_contact": "The user is petting StackChan. Respond warmly in one short sentence.",
            "summon": "The user double-tapped StackChan to summon attention. Respond briefly.",
            "wake": "The user pressed the wake button. Say you are listening.",
        }
        prompt = prompts.get(intent.action)
        if prompt is None:
            return None
        self.last_proactive_at = now
        return ProactiveAction(prompt=prompt, intent=intent)


def summarize_intents(events: list[dict[str, Any]]) -> list[str]:
    return [f"{intent.kind}:{intent.action}" for intent in intents_from_events(events)]
