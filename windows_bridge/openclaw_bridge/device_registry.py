"""Device registry and room routing for multi-StackChan homes."""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Any


@dataclass
class StackChanDevice:
    device_id: str
    room: str = "default"
    name: str = "StackChan"
    features: dict[str, Any] = field(default_factory=dict)
    last_seen_at: float = 0.0
    endpoint: str | None = None

    def is_stale(self, now: float, timeout_s: float = 30.0) -> bool:
        return self.last_seen_at > 0 and now - self.last_seen_at > timeout_s

    def supports(self, feature: str | None) -> bool:
        return feature is None or bool(self.features.get(feature))


class DeviceRegistry:
    def __init__(self) -> None:
        self._devices: dict[str, StackChanDevice] = {}

    def upsert_from_hello(self, hello: dict[str, Any], endpoint: str | None = None, now: float | None = None) -> StackChanDevice:
        device_info = hello.get("device") if isinstance(hello.get("device"), dict) else {}
        device_id = str(device_info.get("device_id") or hello.get("device_id") or hello.get("name") or "stackchan")
        room = str(device_info.get("room") or hello.get("room") or "default")
        name = str(device_info.get("name") or hello.get("name") or "StackChan")
        features = hello.get("features") if isinstance(hello.get("features"), dict) else {}
        device = self._devices.get(device_id) or StackChanDevice(device_id=device_id)
        device.room = room
        device.name = name
        device.features = dict(features)
        device.last_seen_at = time.time() if now is None else now
        device.endpoint = endpoint
        self._devices[device_id] = device
        return device

    def get(self, device_id: str) -> StackChanDevice | None:
        return self._devices.get(device_id)

    def by_room(self, room: str) -> list[StackChanDevice]:
        return [device for device in self._devices.values() if device.room == room]

    def route_targets(
        self,
        room: str | None = None,
        device_id: str | None = None,
        feature: str | None = None,
        include_stale: bool = False,
        now: float | None = None,
    ) -> list[StackChanDevice]:
        current = time.time() if now is None else now
        if device_id:
            device = self.get(device_id)
            if device is None or not device.supports(feature):
                return []
            if device.is_stale(current) and not include_stale:
                return []
            return [device]
        candidates = self.by_room(room or "default")
        return [
            device
            for device in sorted(candidates, key=lambda item: item.last_seen_at, reverse=True)
            if device.supports(feature) and (include_stale or not device.is_stale(current))
        ]

    def route_target(self, room: str | None = None, device_id: str | None = None) -> StackChanDevice | None:
        targets = self.route_targets(room=room, device_id=device_id, include_stale=True)
        return targets[0] if targets else None

    def command_messages(
        self,
        payload: dict[str, Any],
        room: str | None = None,
        device_id: str | None = None,
        feature: str | None = None,
        include_stale: bool = False,
        now: float | None = None,
    ) -> list[dict[str, Any]]:
        return [
            {
                "device_id": device.device_id,
                "room": device.room,
                "endpoint": device.endpoint,
                "payload": payload,
            }
            for device in self.route_targets(room=room, device_id=device_id, feature=feature, include_stale=include_stale, now=now)
        ]

    def snapshot(self, now: float | None = None) -> list[dict[str, Any]]:
        current = time.time() if now is None else now
        return [
            {
                "device_id": device.device_id,
                "room": device.room,
                "name": device.name,
                "features": device.features,
                "endpoint": device.endpoint,
                "stale": device.is_stale(current),
            }
            for device in sorted(self._devices.values(), key=lambda item: (item.room, item.device_id))
        ]
