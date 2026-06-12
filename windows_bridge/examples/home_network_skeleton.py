#!/usr/bin/env python3
"""Dry-run home-network bridge skeleton for multiple StackChan devices."""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from openclaw_bridge import (  # noqa: E402
    DeviceRegistry,
    EnergyVad,
    KeywordWakeWord,
    MemoryTransport,
    OutboundMessage,
    VoiceFrame,
    mqtt_endpoint,
    websocket_endpoint,
)


def main() -> int:
    registry = DeviceRegistry()
    registry.upsert_from_hello(
        {"device": {"device_id": "stackchan-living", "room": "living_room", "name": "Living Room StackChan"}, "features": {"body": True, "audio_in": True}},
        endpoint="ws://127.0.0.1:8787/stackchan-living",
        now=10.0,
    )
    registry.upsert_from_hello(
        {"device": {"device_id": "stackchan-desk", "room": "desk", "name": "Desk StackChan"}, "features": {"body": True, "audio_in": True}},
        endpoint="mqtt://home-broker",
        now=11.0,
    )

    endpoint = mqtt_endpoint("mqtt://home-broker", room="living_room", topic_prefix="openclaw/home")
    transport = MemoryTransport(endpoint)
    for target in registry.command_messages({"action": "presence", "state": "listening"}, room="living_room", feature="body", now=12.0):
        transport.publish(OutboundMessage(endpoint.command_topic(target["device_id"]), target["payload"]))

    frame = VoiceFrame(energy=0.9, text_hint="hey openclaw", timestamp=12.0)
    output = {
        "websocket": websocket_endpoint("ws://127.0.0.1:8787/bridge").__dict__,
        "mqtt_command_topic": endpoint.command_topic("stackchan-living"),
        "registry": registry.snapshot(now=12.0),
        "published": [message.to_envelope() for message in transport.messages],
        "voice": {
            "vad": EnergyVad(threshold=0.2).decide(frame).__dict__,
            "wakeword": KeywordWakeWord(["hey openclaw"]).detect(frame).__dict__,
        },
    }
    print(json.dumps(output, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
