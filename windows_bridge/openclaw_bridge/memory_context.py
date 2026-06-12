"""Short-lived OpenClaw memory context passed through the Windows bridge."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


MAX_TEXT_CHARS = 4000
MAX_ITEMS = 20


@dataclass(frozen=True)
class MemoryContext:
    resident_id: str = "openclaw"
    session_id: str | None = None
    summary: str = ""
    facts: list[str] = field(default_factory=list)
    preferences: dict[str, str] = field(default_factory=dict)

    @classmethod
    def from_dict(cls, payload: dict[str, Any]) -> "MemoryContext":
        facts_raw = payload.get("facts", [])
        facts = [str(item)[:MAX_TEXT_CHARS] for item in facts_raw if isinstance(item, (str, int, float))][:MAX_ITEMS]
        preferences_raw = payload.get("preferences", {})
        preferences: dict[str, str] = {}
        if isinstance(preferences_raw, dict):
            for key, value in list(preferences_raw.items())[:MAX_ITEMS]:
                preferences[str(key)[:120]] = str(value)[:500]
        session = payload.get("session_id")
        return cls(
            resident_id=str(payload.get("resident_id") or "openclaw")[:120],
            session_id=str(session)[:120] if session else None,
            summary=str(payload.get("summary") or "")[:MAX_TEXT_CHARS],
            facts=facts,
            preferences=preferences,
        )

    @classmethod
    def from_file(cls, path: str | Path) -> "MemoryContext":
        payload = json.loads(Path(path).read_text(encoding="utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("memory context file must contain a JSON object")
        return cls.from_dict(payload)

    def to_brain_payload(self) -> dict[str, Any]:
        return {
            "resident_id": self.resident_id,
            "session_id": self.session_id,
            "summary": self.summary,
            "facts": list(self.facts),
            "preferences": dict(self.preferences),
        }

    def is_loaded(self) -> bool:
        return bool(self.summary or self.facts or self.preferences)


def load_memory_context(path: str | None) -> MemoryContext | None:
    if not path:
        return None
    return MemoryContext.from_file(path)
