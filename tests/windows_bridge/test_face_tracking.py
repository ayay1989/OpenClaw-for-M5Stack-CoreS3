from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "windows_bridge" / "examples"))

from face_tracking_loop import FaceObservation  # noqa: E402
from face_tracking_loop import FaceTracker  # noqa: E402
from face_tracking_loop import parse_observation  # noqa: E402


class FaceTrackingTest(unittest.TestCase):
    def test_center_face_keeps_center_target(self) -> None:
        tracker = FaceTracker(smoothing=1.0, update_interval_s=0.0)
        target = tracker.update(FaceObservation(0.5, 0.5, 1.0, timestamp=1.0))
        self.assertIsNotNone(target)
        assert target is not None
        self.assertEqual(target.yaw, 0)
        self.assertEqual(target.pitch, 30)

    def test_right_high_face_maps_to_safe_angles(self) -> None:
        tracker = FaceTracker(smoothing=1.0, update_interval_s=0.0, yaw_limit=35)
        target = tracker.update(FaceObservation(1.0, 0.0, 1.0, timestamp=1.0))
        self.assertIsNotNone(target)
        assert target is not None
        self.assertEqual(target.yaw, 35)
        self.assertEqual(target.pitch, 12)

    def test_rate_limit_suppresses_fast_updates(self) -> None:
        tracker = FaceTracker(smoothing=1.0, update_interval_s=0.5)
        first = tracker.update(FaceObservation(0.8, 0.5, 1.0, timestamp=1.0))
        second = tracker.update(FaceObservation(0.2, 0.5, 1.0, timestamp=1.2))
        self.assertIsNotNone(first)
        self.assertIsNone(second)

    def test_lost_face_recenters_once_after_timeout(self) -> None:
        tracker = FaceTracker(smoothing=1.0, update_interval_s=0.0, lost_timeout_s=2.0)
        tracker.update(FaceObservation(0.8, 0.5, 1.0, timestamp=1.0))
        self.assertIsNone(tracker.mark_lost(2.0))
        target = tracker.mark_lost(3.2)
        self.assertIsNotNone(target)
        assert target is not None
        self.assertEqual((target.yaw, target.pitch), (0, 30))
        self.assertIsNone(tracker.mark_lost(4.0))

    def test_parse_observation_accepts_text_json_and_lost(self) -> None:
        text = parse_observation("0.25 0.75 0.9")
        self.assertEqual((text.x, text.y, text.confidence), (0.25, 0.75, 0.9))
        payload = parse_observation('{"x":0.1,"y":0.2,"conf":0.3}')
        self.assertEqual((payload.x, payload.y, payload.confidence), (0.1, 0.2, 0.3))
        lost = parse_observation("lost")
        self.assertEqual(lost.confidence, 0.0)


if __name__ == "__main__":
    unittest.main()
