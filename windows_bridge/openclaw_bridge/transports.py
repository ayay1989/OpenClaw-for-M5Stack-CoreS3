"""Transport abstractions for home-network StackChan deployments."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any, Protocol


@dataclass(frozen=True)
class TransportEndpoint:
    kind: str
    url: str
    room: str = "default"
    device_id: str | None = None
    secure: bool = False
    options: dict[str, Any] = field(default_factory=dict)

    def is_websocket(self) -> bool:
        return self.kind in {"websocket", "ws", "wss"} or self.url.startswith(("ws://", "wss://"))

    def is_mqtt(self) -> bool:
        return self.kind == "mqtt" or self.url.startswith("mqtt://")

    def topic_prefix(self) -> str:
        return str(self.options.get("topic_prefix") or "openclaw/home").rstrip("/")

    def command_topic(self, device_id: str | None = None) -> str:
        target = device_id or self.device_id or "+"
        return f"{self.topic_prefix()}/devices/{target}/command"


@dataclass(frozen=True)
class OutboundMessage:
    topic: str
    payload: dict[str, Any]
    qos: int = 0

    def to_json_line(self) -> str:
        return json.dumps(self.payload, ensure_ascii=False, separators=(",", ":")) + "\n"

    def to_envelope(self) -> dict[str, Any]:
        return {"topic": self.topic, "payload": self.payload, "qos": self.qos}


class MessageTransport(Protocol):
    endpoint: TransportEndpoint

    def publish(self, message: OutboundMessage) -> None:
        ...

    def close(self) -> None:
        ...


class MemoryTransport:
    """In-memory transport used for tests and dry-run orchestration."""

    def __init__(self, endpoint: TransportEndpoint) -> None:
        self.endpoint = endpoint
        self.messages: list[OutboundMessage] = []
        self.closed = False

    def publish(self, message: OutboundMessage) -> None:
        if self.closed:
            raise RuntimeError("transport is closed")
        self.messages.append(message)

    def close(self) -> None:
        self.closed = True


def transport_from_config(payload: dict[str, Any]) -> TransportEndpoint:
    kind = str(payload.get("kind") or "tcp-json")
    url = str(payload.get("url") or payload.get("base_url") or "")
    if not url:
        raise ValueError("transport url is required")
    return TransportEndpoint(
        kind=kind,
        url=url,
        room=str(payload.get("room") or "default"),
        device_id=str(payload["device_id"]) if payload.get("device_id") else None,
        secure=bool(payload.get("secure", False)),
        options=payload.get("options") if isinstance(payload.get("options"), dict) else {},
    )


def websocket_endpoint(url: str, room: str = "default", device_id: str | None = None, **options: Any) -> TransportEndpoint:
    return TransportEndpoint(kind="websocket", url=url, room=room, device_id=device_id, secure=url.startswith("wss://"), options=dict(options))


def mqtt_endpoint(url: str, room: str = "default", device_id: str | None = None, topic_prefix: str = "openclaw/home", **options: Any) -> TransportEndpoint:
    merged = {"topic_prefix": topic_prefix, **options}
    return TransportEndpoint(kind="mqtt", url=url, room=room, device_id=device_id, secure=url.startswith("mqtts://"), options=merged)


def websocket_text_frame(payload: str) -> bytes:
    data = payload.encode("utf-8")
    length = len(data)
    if length < 126:
        header = bytes([0x81, length])
    elif length <= 65535:
        header = bytes([0x81, 126]) + length.to_bytes(2, "big")
    else:
        header = bytes([0x81, 127]) + length.to_bytes(8, "big")
    return header + data


def mqtt_publish_packet(topic: str, payload: bytes, packet_id: int | None = None) -> bytes:
    topic_bytes = topic.encode("utf-8")
    variable_header = len(topic_bytes).to_bytes(2, "big") + topic_bytes
    if packet_id is not None:
        variable_header += packet_id.to_bytes(2, "big")
    remaining = variable_header + payload
    return bytes([0x30]) + _mqtt_remaining_length(len(remaining)) + remaining


def _mqtt_remaining_length(value: int) -> bytes:
    if value < 0:
        raise ValueError("remaining length must be positive")
    encoded = bytearray()
    while True:
        digit = value % 128
        value //= 128
        if value > 0:
            digit |= 0x80
        encoded.append(digit)
        if value == 0:
            return bytes(encoded)
