"""Audio pipeline models for future StackChan speaker/microphone work."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class AudioCodec(str, Enum):
    PCM_S16LE = "pcm_s16le"


@dataclass(frozen=True)
class AudioFormat:
    sample_rate: int = 24000
    channels: int = 1
    codec: AudioCodec = AudioCodec.PCM_S16LE

    def bytes_per_second(self) -> int:
        sample_width = 2 if self.codec == AudioCodec.PCM_S16LE else 1
        return self.sample_rate * self.channels * sample_width


@dataclass
class AudioStreamSession:
    session_id: str
    direction: str
    audio_format: AudioFormat
    total_bytes: int = 0
    chunks: int = 0
    closed: bool = False

    def add_chunk(self, data: bytes) -> None:
        if self.closed:
            raise RuntimeError("audio stream is closed")
        self.total_bytes += len(data)
        self.chunks += 1

    def estimated_duration_s(self) -> float:
        bps = self.audio_format.bytes_per_second()
        if bps <= 0:
            return 0.0
        return round(self.total_bytes / bps, 3)

    def close(self) -> None:
        self.closed = True


def make_tts_stream(session_id: str, sample_rate: int = 24000, channels: int = 1) -> AudioStreamSession:
    return AudioStreamSession(session_id=session_id, direction="tts_out", audio_format=AudioFormat(sample_rate=sample_rate, channels=channels))


def make_microphone_stream(session_id: str, sample_rate: int = 16000, channels: int = 1) -> AudioStreamSession:
    return AudioStreamSession(session_id=session_id, direction="mic_in", audio_format=AudioFormat(sample_rate=sample_rate, channels=channels))
