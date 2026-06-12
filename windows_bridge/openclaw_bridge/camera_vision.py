"""Optional Windows camera face detection adapter."""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Iterator

from .face_tracking import FaceObservation


@dataclass(frozen=True)
class FaceBox:
    x: int
    y: int
    width: int
    height: int
    confidence: float = 1.0


def observation_from_face_box(frame_width: int, frame_height: int, box: FaceBox, timestamp: float | None = None) -> FaceObservation:
    if frame_width <= 0 or frame_height <= 0:
        raise ValueError("frame dimensions must be positive")
    center_x = (box.x + box.width / 2.0) / frame_width
    center_y = (box.y + box.height / 2.0) / frame_height
    return FaceObservation(
        x=max(0.0, min(1.0, center_x)),
        y=max(0.0, min(1.0, center_y)),
        confidence=max(0.0, min(1.0, box.confidence)),
        timestamp=time.time() if timestamp is None else timestamp,
    )


class OpenCvFaceDetector:
    """Small optional OpenCV Haar detector."""

    def __init__(self, camera_index: int = 0, min_size: int = 80) -> None:
        try:
            import cv2  # type: ignore[import-not-found]
        except ImportError as exc:
            raise RuntimeError("OpenCV is not installed; install opencv-python to use camera mode") from exc
        self.cv2 = cv2
        self.camera_index = camera_index
        self.min_size = min_size
        cascade_path = cv2.data.haarcascades + "haarcascade_frontalface_default.xml"
        self.classifier = cv2.CascadeClassifier(cascade_path)
        if self.classifier.empty():
            raise RuntimeError("OpenCV face cascade could not be loaded")

    def observations(self) -> Iterator[FaceObservation]:
        capture = self.cv2.VideoCapture(self.camera_index)
        if not capture.isOpened():
            raise RuntimeError(f"camera {self.camera_index} could not be opened")
        try:
            while True:
                ok, frame = capture.read()
                if not ok:
                    yield FaceObservation(0.5, 0.5, 0.0)
                    continue
                gray = self.cv2.cvtColor(frame, self.cv2.COLOR_BGR2GRAY)
                faces = self.classifier.detectMultiScale(gray, scaleFactor=1.1, minNeighbors=5, minSize=(self.min_size, self.min_size))
                if len(faces) == 0:
                    yield FaceObservation(0.5, 0.5, 0.0)
                    continue
                x, y, width, height = max(faces, key=lambda item: item[2] * item[3])
                frame_height, frame_width = frame.shape[:2]
                yield observation_from_face_box(frame_width, frame_height, FaceBox(int(x), int(y), int(width), int(height)))
        finally:
            capture.release()
