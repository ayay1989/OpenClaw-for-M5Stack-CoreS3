#!/usr/bin/env python3
"""CLI scaffold for the resident conversation loop."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from openclaw_bridge import ResidentConversationLoop, StackChanBodyClient, SystemTts  # noqa: E402
from openclaw_bridge.resident_loop import build_brain  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description="Resident conversation loop scaffold")
    parser.add_argument("--bridge-url", default="http://127.0.0.1:8766", help="Windows Bridge control API")
    parser.add_argument("--openclaw-url", default=None, help="optional local OpenClaw chat endpoint")
    parser.add_argument("--tts", action="store_true", help="speak replies with the operating system TTS")
    parser.add_argument("--event-limit", default=20, type=int, help="recent body events sent to brain")
    parser.add_argument("--once", default=None, help="run one text turn and exit")
    args = parser.parse_args()

    loop = ResidentConversationLoop(
        body=StackChanBodyClient(args.bridge_url),
        brain=build_brain(args.openclaw_url),
        tts=SystemTts(enabled=args.tts),
        event_limit=args.event_limit,
    )
    if args.once is not None:
        reply = loop.run_once(args.once)
        print(json.dumps(reply.__dict__, ensure_ascii=False, indent=2))
    else:
        loop.run_console()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
