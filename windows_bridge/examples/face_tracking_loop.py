#!/usr/bin/env python3
"""CLI scaffold for StackChan face tracking."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from openclaw_bridge import FaceObservation, FaceTracker, LookTarget, StackChanBodyClient, parse_observation  # noqa: E402


def send_target(body: StackChanBodyClient, target: LookTarget | None, dry_run: bool) -> None:
    if target is None:
        return
    print(f"[face] look yaw={target.yaw} pitch={target.pitch} duration_ms={target.duration_ms}")
    if not dry_run:
        body.look_at(target.yaw, target.pitch, target.duration_ms)


def run_simulation(body: StackChanBodyClient, tracker: FaceTracker, dry_run: bool) -> None:
    samples = [
        FaceObservation(0.50, 0.50, 1.0),
        FaceObservation(0.70, 0.48, 1.0),
        FaceObservation(0.82, 0.45, 1.0),
        FaceObservation(0.25, 0.55, 1.0),
        FaceObservation(0.50, 0.50, 0.0),
    ]
    start = time.time()
    for index, sample in enumerate(samples):
        sample.timestamp = start + index * 0.35
        send_target(body, tracker.update(sample), dry_run)
    send_target(body, tracker.mark_lost(start + 4.0), dry_run)


def run_stdin(body: StackChanBodyClient, tracker: FaceTracker, dry_run: bool) -> None:
    print("Enter normalized face x y [confidence], JSON, or 'lost'. Use /quit to stop.")
    while True:
        line = input("face> ").strip()
        if line in {"/quit", "/exit"}:
            send_target(body, LookTarget(0, tracker.pitch_center, 500), dry_run)
            return
        try:
            observation = parse_observation(line)
        except (ValueError, json.JSONDecodeError) as exc:
            print(f"[face] bad input: {exc}")
            continue
        if observation is None:
            continue
        target = tracker.update(observation)
        if observation.confidence < tracker.min_confidence:
            target = tracker.mark_lost()
        send_target(body, target, dry_run)


def main() -> int:
    parser = argparse.ArgumentParser(description="StackChan face tracking adapter scaffold")
    parser.add_argument("--bridge-url", default="http://127.0.0.1:8766", help="Windows Bridge control API")
    parser.add_argument("--simulate", action="store_true", help="run a deterministic face-position simulation")
    parser.add_argument("--dry-run", action="store_true", help="print targets without sending look commands")
    parser.add_argument("--yaw-limit", default=35, type=int)
    parser.add_argument("--lost-timeout", default=2.0, type=float)
    args = parser.parse_args()

    body = StackChanBodyClient(args.bridge_url)
    tracker = FaceTracker(yaw_limit=args.yaw_limit, lost_timeout_s=args.lost_timeout)
    if args.simulate:
        run_simulation(body, tracker, args.dry_run)
    else:
        run_stdin(body, tracker, args.dry_run)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
