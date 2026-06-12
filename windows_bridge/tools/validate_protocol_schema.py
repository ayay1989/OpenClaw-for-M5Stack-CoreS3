#!/usr/bin/env python3
"""Validate protocol schema and bundled sample messages without extra deps."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "docs" / "schema" / "openclaw-stackchan-message.schema.json"
SAMPLES = ROOT / "docs" / "examples" / "protocol-valid-messages.jsonl"


REQUIRED_TOP_LEVEL = {"$schema", "title", "oneOf", "$defs"}
REQUIRED_DEFS = {"legacy_action", "event", "hello", "hello_ack", "memory_context", "status_response", "mcp_envelope"}


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def fail(message: str) -> None:
    raise AssertionError(message)


def validate_schema_shape(schema: dict[str, Any]) -> None:
    missing = REQUIRED_TOP_LEVEL - set(schema)
    if missing:
        fail(f"schema missing top-level keys: {sorted(missing)}")
    defs = schema.get("$defs")
    if not isinstance(defs, dict):
        fail("schema $defs must be an object")
    missing_defs = REQUIRED_DEFS - set(defs)
    if missing_defs:
        fail(f"schema missing defs: {sorted(missing_defs)}")


def validate_sample_against_contract(message: dict[str, Any]) -> None:
    if "action" in message:
        if not isinstance(message["action"], str):
            fail("action must be a string")
        if message["action"] == "emotion" and message.get("value") not in {"happy", "normal", "sad", "angry", "surprised", "sleepy", "shy", "love"}:
            fail("emotion action requires a supported value")
        if message["action"] == "look":
            yaw = int(message.get("yaw", 0))
            pitch = int(message.get("pitch", 30))
            if not -45 <= yaw <= 45 or not 5 <= pitch <= 60:
                fail("look target is outside safe range")
        if message["action"] == "audio_stream":
            if message.get("op") not in {"start", "chunk", "stop"}:
                fail("audio_stream requires op=start|chunk|stop")
            if not message.get("stream_id"):
                fail("audio_stream requires stream_id")
        return
    if message.get("type") in {"hello_ack", "memory_context"}:
        return
    if message.get("type") == "hello":
        if message.get("protocol") != "openclaw-stackchan":
            fail("hello protocol must be openclaw-stackchan")
        if not isinstance(message.get("features"), dict):
            fail("hello requires features object")
        return
    if message.get("type") == "mcp":
        payload = message.get("payload")
        if not isinstance(payload, dict) or payload.get("jsonrpc") != "2.0":
            fail("mcp payload must be JSON-RPC 2.0")
        return
    if "event" in message:
        if message["event"] not in {"button", "touch", "pressure", "gesture", "body_input", "heartbeat", "self_test"}:
            fail("unknown event type")
        if message["event"] == "body_input" and not message.get("input"):
            fail("body_input requires input")
        return
    if "status" in message:
        if message["status"] not in {"ok", "error"}:
            fail("status must be ok or error")
        return
    fail(f"sample does not match known protocol family: {message}")


def main() -> int:
    schema = load_json(SCHEMA)
    if not isinstance(schema, dict):
        fail("schema root must be an object")
    validate_schema_shape(schema)
    count = 0
    for line_number, line in enumerate(SAMPLES.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip():
            continue
        payload = json.loads(line)
        if not isinstance(payload, dict):
            fail(f"sample line {line_number} must be a JSON object")
        validate_sample_against_contract(payload)
        count += 1
    print(f"[schema] validated {count} bundled protocol samples")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, json.JSONDecodeError) as exc:
        print(f"[schema] validation failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
