#!/usr/bin/env python3
"""Unified local runtime entrypoint for OpenClaw + StackChan."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from openclaw_bridge import DemoBrainAdapter, LifeCycleManager, SystemTts  # noqa: E402
from openclaw_bridge.runtime import build_body, build_conversation_loop, face_simulation_targets, load_config  # noqa: E402


class DryRunBody:
    def __init__(self) -> None:
        self.calls: list[tuple[str, tuple[Any, ...]]] = []

    def set_presence(self, state: str, emotion: str | None = None, mouth: bool | None = None) -> None:
        self.calls.append(("set_presence", (state, emotion, mouth)))

    def breath(self, r: int, g: int, b: int, speed: int = 3) -> None:
        self.calls.append(("breath", (r, g, b, speed)))

    def motion(self, gesture: str) -> None:
        self.calls.append(("motion", (gesture,)))

    def start_thinking(self) -> None:
        self.calls.append(("start_thinking", ()))

    def start_speaking(self, emotion: str = "happy") -> None:
        self.calls.append(("start_speaking", (emotion,)))

    def stop_speaking(self) -> None:
        self.calls.append(("stop_speaking", ()))


def build_dry_lifecycle(config) -> tuple[LifeCycleManager, DryRunBody]:
    body = DryRunBody()
    manager = LifeCycleManager(
        body=body,
        brain=DemoBrainAdapter(),
        tts=SystemTts(False),
        sleep_timeout_s=config.sleep_timeout_s,
        proactive_cooldown_s=0.0,
    )
    return manager, body


def main() -> int:
    parser = argparse.ArgumentParser(description="Unified StackChan local runtime")
    parser.add_argument("--config", default=None, help="runtime JSON config path")
    parser.add_argument("--status", action="store_true", help="print bridge/body status")
    parser.add_argument("--once", default=None, help="run one conversation turn")
    parser.add_argument("--chat", action="store_true", help="run keyboard-driven conversation loop")
    parser.add_argument("--face-sim", action="store_true", help="print deterministic face tracking targets")
    parser.add_argument("--life-status", action="store_true", help="print local life-cycle status overlay lines")
    parser.add_argument("--life-demo", action="store_true", help="run a deterministic life-cycle demo")
    args = parser.parse_args()

    config = load_config(args.config)
    if args.status:
        print(json.dumps(build_body(config).status(), ensure_ascii=False, indent=2))
        return 0
    if args.once is not None:
        reply = build_conversation_loop(config).run_once(args.once)
        print(json.dumps(reply.__dict__, ensure_ascii=False, indent=2))
        return 0
    if args.chat:
        build_conversation_loop(config).run_console()
        return 0
    if args.face_sim:
        targets = [target.__dict__ for target in face_simulation_targets(config)]
        print(json.dumps(targets, ensure_ascii=False, indent=2))
        return 0
    if args.life_status:
        manager, _ = build_dry_lifecycle(config)
        print(json.dumps(manager.status(0.0).overlay_lines(), ensure_ascii=False, indent=2))
        return 0
    if args.life_demo:
        manager, body = build_dry_lifecycle(config)
        manager.wake(1.0)
        actions = manager.handle_events([{"kind": "pressure", "message": {"event": "pressure", "action": "hold"}}], now=2.0)
        output = {
            "status": manager.status(2.0).overlay_lines(),
            "actions": [{"prompt": action.prompt, "intent": action.intent.action} for action in actions],
            "body_calls": body.calls,
        }
        print(json.dumps(output, ensure_ascii=False, indent=2))
        return 0

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
