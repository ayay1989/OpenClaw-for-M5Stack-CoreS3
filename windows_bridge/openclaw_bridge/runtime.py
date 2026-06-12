"""Configurable local runtime helpers for OpenClaw + StackChan."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .body_client import StackChanBodyClient
from .face_tracking import FaceObservation, FaceTracker, LookTarget
from .resident_loop import ResidentConversationLoop, SystemTts, build_brain


@dataclass
class RuntimeConfig:
    bridge_url: str = "http://127.0.0.1:8766"
    openclaw_url: str | None = None
    tts_enabled: bool = False
    event_limit: int = 20
    face_yaw_limit: int = 35
    face_lost_timeout_s: float = 2.0

    @classmethod
    def from_dict(cls, payload: dict[str, Any]) -> "RuntimeConfig":
        return cls(
            bridge_url=str(payload.get("bridge_url") or cls.bridge_url),
            openclaw_url=payload.get("openclaw_url") or None,
            tts_enabled=bool(payload.get("tts_enabled", False)),
            event_limit=int(payload.get("event_limit", 20)),
            face_yaw_limit=int(payload.get("face_yaw_limit", 35)),
            face_lost_timeout_s=float(payload.get("face_lost_timeout_s", 2.0)),
        )


def load_config(path: str | None) -> RuntimeConfig:
    if not path:
        return RuntimeConfig()
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError("runtime config must be a JSON object")
    return RuntimeConfig.from_dict(payload)


def build_body(config: RuntimeConfig) -> StackChanBodyClient:
    return StackChanBodyClient(config.bridge_url)


def build_conversation_loop(config: RuntimeConfig) -> ResidentConversationLoop:
    return ResidentConversationLoop(
        body=build_body(config),
        brain=build_brain(config.openclaw_url),
        tts=SystemTts(config.tts_enabled),
        event_limit=config.event_limit,
    )


def build_face_tracker(config: RuntimeConfig) -> FaceTracker:
    return FaceTracker(yaw_limit=config.face_yaw_limit, lost_timeout_s=config.face_lost_timeout_s)


def face_simulation_targets(config: RuntimeConfig) -> list[LookTarget]:
    tracker = build_face_tracker(config)
    samples = [
        FaceObservation(0.50, 0.50, 1.0, timestamp=1.0),
        FaceObservation(0.70, 0.48, 1.0, timestamp=1.35),
        FaceObservation(0.82, 0.45, 1.0, timestamp=1.70),
        FaceObservation(0.25, 0.55, 1.0, timestamp=2.05),
        FaceObservation(0.50, 0.50, 0.0, timestamp=2.40),
    ]
    targets: list[LookTarget] = []
    for sample in samples:
        target = tracker.update(sample)
        if target is not None:
            targets.append(target)
    lost = tracker.mark_lost(5.0)
    if lost is not None:
        targets.append(lost)
    return targets
