"""ASR input abstractions for the Windows-side resident loop."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from typing import Protocol


@dataclass(frozen=True)
class Transcript:
    text: str
    confidence: float = 1.0
    source: str = "unknown"

    def is_empty(self) -> bool:
        return not self.text.strip()


class TranscriptSource(Protocol):
    def listen_once(self) -> Transcript:
        ...


class KeyboardTranscriptSource:
    def listen_once(self) -> Transcript:
        return Transcript(input("you> ").strip(), source="keyboard")


class ExternalCommandAsr:
    """Run an external ASR command that prints one transcript to stdout."""

    def __init__(self, command: list[str], timeout_s: float = 30.0) -> None:
        if not command:
            raise ValueError("ASR command must not be empty")
        self.command = command
        self.timeout_s = timeout_s

    def listen_once(self) -> Transcript:
        completed = subprocess.run(
            self.command,
            text=True,
            capture_output=True,
            timeout=self.timeout_s,
            check=False,
        )
        if completed.returncode != 0:
            message = completed.stderr.strip() or f"ASR command failed with exit code {completed.returncode}"
            raise RuntimeError(message)
        return Transcript(completed.stdout.strip(), source="external_command")
