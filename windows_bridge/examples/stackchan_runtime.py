#!/usr/bin/env python3
"""Unified local runtime entrypoint for OpenClaw + StackChan."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from openclaw_bridge.runtime import build_body, build_conversation_loop, face_simulation_targets, load_config  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description="Unified StackChan local runtime")
    parser.add_argument("--config", default=None, help="runtime JSON config path")
    parser.add_argument("--status", action="store_true", help="print bridge/body status")
    parser.add_argument("--once", default=None, help="run one conversation turn")
    parser.add_argument("--chat", action="store_true", help="run keyboard-driven conversation loop")
    parser.add_argument("--face-sim", action="store_true", help="print deterministic face tracking targets")
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

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
