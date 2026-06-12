"""Face position to StackChan look-at target conversion."""

from __future__ import annotations

import json
import math
import time
from dataclasses import dataclass


@dataclass
class FaceObservation:
    x: float
    y: float
    confidence: float = 1.0
    timestamp: float | None = None


@dataclass
class LookTarget:
    yaw: int
    pitch: int
    duration_ms: int = 250


class FaceTracker:
    def __init__(
        self,
        yaw_limit: int = 35,
        pitch_center: int = 30,
        pitch_min: int = 12,
        pitch_max: int = 55,
        smoothing: float = 0.35,
        min_confidence: float = 0.45,
        update_interval_s: float = 0.25,
        lost_timeout_s: float = 2.0,
    ) -> None:
        self.yaw_limit = yaw_limit
        self.pitch_center = pitch_center
        self.pitch_min = pitch_min
        self.pitch_max = pitch_max
        self.smoothing = smoothing
        self.min_confidence = min_confidence
        self.update_interval_s = update_interval_s
        self.lost_timeout_s = lost_timeout_s
        self._smoothed_yaw = 0.0
        self._smoothed_pitch = float(pitch_center)
        self._last_sent_at = 0.0
        self._last_seen_at = 0.0
        self._center_sent = False

    def update(self, observation: FaceObservation) -> LookTarget | None:
        now = observation.timestamp if observation.timestamp is not None else time.time()
        if observation.confidence < self.min_confidence:
            return self.mark_lost(now)

        yaw_raw = (self._clamp01(observation.x) - 0.5) * 2.0 * self.yaw_limit
        pitch_span_up = self.pitch_center - self.pitch_min
        pitch_span_down = self.pitch_max - self.pitch_center
        y_offset = (self._clamp01(observation.y) - 0.5) * 2.0
        if y_offset < 0:
            pitch_raw = self.pitch_center + y_offset * pitch_span_up
        else:
            pitch_raw = self.pitch_center + y_offset * pitch_span_down

        self._last_seen_at = now
        self._center_sent = False
        self._smoothed_yaw = self._smooth(self._smoothed_yaw, yaw_raw)
        self._smoothed_pitch = self._smooth(self._smoothed_pitch, pitch_raw)
        if now - self._last_sent_at < self.update_interval_s:
            return None
        self._last_sent_at = now
        return LookTarget(yaw=int(round(self._smoothed_yaw)), pitch=int(round(self._smoothed_pitch)))

    def mark_lost(self, now: float | None = None) -> LookTarget | None:
        current = now if now is not None else time.time()
        if self._last_seen_at <= 0:
            return None
        if current - self._last_seen_at < self.lost_timeout_s:
            return None
        if self._center_sent:
            return None
        self._center_sent = True
        self._smoothed_yaw = 0.0
        self._smoothed_pitch = float(self.pitch_center)
        self._last_sent_at = current
        return LookTarget(yaw=0, pitch=self.pitch_center, duration_ms=500)

    def _smooth(self, current: float, target: float) -> float:
        alpha = self.smoothing
        if math.isnan(current):
            return target
        return current * (1.0 - alpha) + target * alpha

    @staticmethod
    def _clamp01(value: float) -> float:
        if value < 0.0:
            return 0.0
        if value > 1.0:
            return 1.0
        return value


def parse_observation(line: str) -> FaceObservation | None:
    stripped = line.strip()
    if not stripped:
        return None
    if stripped.lower() in {"lost", "none"}:
        return FaceObservation(x=0.5, y=0.5, confidence=0.0)
    if stripped.startswith("{"):
        payload = json.loads(stripped)
        return FaceObservation(
            x=float(payload.get("x", 0.5)),
            y=float(payload.get("y", 0.5)),
            confidence=float(payload.get("confidence", payload.get("conf", 1.0))),
        )
    parts = stripped.split()
    if len(parts) not in {2, 3}:
        raise ValueError("expected: <x> <y> [confidence], JSON object, or lost")
    return FaceObservation(
        x=float(parts[0]),
        y=float(parts[1]),
        confidence=float(parts[2]) if len(parts) == 3 else 1.0,
    )
