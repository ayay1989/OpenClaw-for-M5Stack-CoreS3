"""Edge-TTS helper for Windows Bridge speech output."""

from __future__ import annotations

import base64
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


DeviceCommandSender = Callable[[dict[str, object]], bool]


@dataclass(frozen=True)
class EdgeTtsResult:
    ok: bool
    mode: str
    voice: str
    message: str


class EdgeTtsBridge:
    def __init__(self, voice: str = "zh-CN-YunxiNeural", sample_rate: int = 24000, chunk_size: int = 4096) -> None:
        self.voice = voice
        self.sample_rate = sample_rate
        self.chunk_size = chunk_size

    def speak(
        self,
        text: str,
        send_command: DeviceCommandSender,
        device_audio_stream_available: bool,
        prefer_device: bool = True,
    ) -> EdgeTtsResult:
        text = text.strip()
        if not text:
            return EdgeTtsResult(False, "unavailable", self.voice, "text is empty")

        if prefer_device and device_audio_stream_available:
            result = self._speak_to_device(text, send_command)
            if result.ok:
                return result

        return self._speak_on_windows(text)

    def _speak_to_device(self, text: str, send_command: DeviceCommandSender) -> EdgeTtsResult:
        ffmpeg = shutil.which("ffmpeg")
        if ffmpeg is None:
            return EdgeTtsResult(False, "unavailable", self.voice, "ffmpeg is required for device PCM streaming")
        with tempfile.TemporaryDirectory(prefix="openclaw_tts_") as tmp:
            mp3_path = Path(tmp) / "speech.mp3"
            pcm_path = Path(tmp) / "speech.pcm"
            edge = self._run_edge_tts(text, mp3_path)
            if edge.returncode != 0:
                return EdgeTtsResult(False, "unavailable", self.voice, edge.stderr.strip() or "edge-tts failed")
            convert = subprocess.run(
                [
                    ffmpeg,
                    "-y",
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-i",
                    str(mp3_path),
                    "-f",
                    "s16le",
                    "-ac",
                    "1",
                    "-ar",
                    str(self.sample_rate),
                    str(pcm_path),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            if convert.returncode != 0:
                return EdgeTtsResult(False, "unavailable", self.voice, convert.stderr.strip() or "ffmpeg conversion failed")
            stream_id = "edge-tts-1"
            send_command(
                {
                    "action": "audio_stream",
                    "op": "start",
                    "stream_id": stream_id,
                    "direction": "tts_out",
                    "sample_rate": self.sample_rate,
                    "channels": 1,
                    "format": "pcm_s16le",
                }
            )
            seq = 0
            with pcm_path.open("rb") as fh:
                while True:
                    chunk = fh.read(self.chunk_size)
                    if not chunk:
                        break
                    send_command(
                        {
                            "action": "audio_stream",
                            "op": "chunk",
                            "stream_id": stream_id,
                            "seq": seq,
                            "data_b64": base64.b64encode(chunk).decode("ascii"),
                        }
                    )
                    seq += 1
            send_command({"action": "audio_stream", "op": "stop", "stream_id": stream_id})
            return EdgeTtsResult(True, "device_stream", self.voice, f"streamed {seq} PCM chunks")

    def _speak_on_windows(self, text: str) -> EdgeTtsResult:
        edge_playback = shutil.which("edge-playback")
        if edge_playback is not None:
            subprocess.Popen([edge_playback, "--voice", self.voice, "--text", text])
            return EdgeTtsResult(True, "windows_playback", self.voice, "started edge-playback")
        fd, raw_path = tempfile.mkstemp(prefix="openclaw_tts_", suffix=".mp3")
        os.close(fd)
        mp3_path = Path(raw_path)
        edge = self._run_edge_tts(text, mp3_path)
        if edge.returncode != 0:
            return EdgeTtsResult(False, "unavailable", self.voice, edge.stderr.strip() or "edge-tts failed")
        if sys.platform.startswith("win"):
            powershell = shutil.which("powershell") or shutil.which("pwsh")
            if powershell is not None:
                script = (
                    "Add-Type -AssemblyName presentationCore;"
                    f"$p=New-Object System.Windows.Media.MediaPlayer;"
                    f"$p.Open([uri]'{mp3_path.as_posix()}');"
                    "$p.Play(); Start-Sleep -Seconds 5"
                )
                subprocess.Popen([powershell, "-NoProfile", "-Command", script])
                return EdgeTtsResult(True, "windows_playback", self.voice, f"started Windows MediaPlayer: {mp3_path}")
        return EdgeTtsResult(True, "file_generated", self.voice, f"generated {mp3_path}")

    def _run_edge_tts(self, text: str, media_path: Path) -> subprocess.CompletedProcess[str]:
        env = {**os.environ, "PYTHONIOENCODING": "utf-8"}
        return subprocess.run(
            [
                sys.executable,
                "-m",
                "edge_tts",
                "--voice",
                self.voice,
                "--text",
                text,
                "--write-media",
                str(media_path),
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=env,
            check=False,
        )
