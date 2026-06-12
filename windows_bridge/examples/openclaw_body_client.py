#!/usr/bin/env python3
"""CLI demo for the reusable StackChanBodyClient."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from openclaw_bridge import StackChanBodyClient  # noqa: E402


def demo(base_url: str) -> None:
    body = StackChanBodyClient(base_url)
    print(json.dumps(body.status(), ensure_ascii=False, indent=2))
    body.start_listening()
    time.sleep(0.5)
    body.start_thinking()
    time.sleep(0.5)
    body.start_speaking("happy")
    body.motion("nod")
    time.sleep(1.0)
    body.stop_speaking()
    print(json.dumps(body.poll_events(limit=10), ensure_ascii=False, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser(description="Example client for the local StackChan bridge API")
    parser.add_argument("--base-url", default="http://127.0.0.1:8766")
    parser.add_argument("--demo", action="store_true", help="run a small body behavior demo")
    args = parser.parse_args()
    if args.demo:
        demo(args.base_url)
    else:
        print(json.dumps(StackChanBodyClient(args.base_url).status(), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
