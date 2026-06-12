#!/usr/bin/env python3
"""Resident conversation loop scaffold for OpenClaw + StackChan.

This is not a full ASR/OpenClaw/TTS implementation. It is the glue shape:
text input stands in for ASR, an optional HTTP brain stands in for OpenClaw,
and system speech can stand in for TTS while StackChan follows the body state.
"""

from __future__ import annotations

import argparse
import json
import platform
import subprocess
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any, Protocol

from openclaw_body_client import StackChanBodyClient


class BrainAdapter(Protocol):
    def reply(self, user_text: str, recent_events: list[dict[str, Any]]) -> "BrainReply":
        ...


@dataclass
class BrainReply:
    text: str
    emotion: str = "happy"
    gesture: str | None = "nod"


class DemoBrainAdapter:
    def reply(self, user_text: str, recent_events: list[dict[str, Any]]) -> BrainReply:
        tactile = [event for event in recent_events if event.get("kind") == "pressure"]
        if tactile:
            action = tactile[-1].get("message", {}).get("action", "touch")
            if action == "hold":
                return BrainReply("我感受到你在摸我。这个身体反馈已经传到 OpenClaw 这边了。", "love", "nod")
            if action == "press":
                return BrainReply("我在这里，已经注意到你碰我了。", "happy", "nod")
        if any(word in user_text.lower() for word in ("sad", "难过", "不开心")):
            return BrainReply("我听见了。我们慢一点说，我会陪着你。", "shy", "tilt")
        if any(word in user_text.lower() for word in ("sleep", "困", "睡")):
            return BrainReply("那我先安静一点，等你需要我再醒来。", "sleepy", None)
        return BrainReply(f"我收到啦：{user_text}", "happy", "nod")


class HttpBrainAdapter:
    """Generic local OpenClaw HTTP adapter.

    Expected request:
    {"text": "...", "events": [...]}

    Accepted response fields:
    {"text": "...", "emotion": "happy", "gesture": "nod"}
    """

    def __init__(self, url: str) -> None:
        self.url = url

    def reply(self, user_text: str, recent_events: list[dict[str, Any]]) -> BrainReply:
        body = json.dumps({"text": user_text, "events": recent_events}, ensure_ascii=False).encode("utf-8")
        request = urllib.request.Request(
            self.url,
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                payload = json.loads(response.read().decode("utf-8"))
        except (OSError, urllib.error.HTTPError, json.JSONDecodeError) as exc:
            return BrainReply(f"OpenClaw 本地接口暂时不可用：{exc}", "sad", None)
        text = str(payload.get("text") or payload.get("reply") or "")
        if not text:
            text = "OpenClaw 返回了空回复。"
        emotion = str(payload.get("emotion") or "happy")
        gesture_value = payload.get("gesture", "nod")
        gesture = str(gesture_value) if gesture_value else None
        return BrainReply(text=text, emotion=emotion, gesture=gesture)


class SystemTts:
    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled

    def speak(self, text: str) -> None:
        if not self.enabled:
            print(f"[tts-off] {text}")
            return
        system = platform.system().lower()
        if system == "windows":
            script = (
                "Add-Type -AssemblyName System.Speech; "
                "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
                "$s.Speak([Console]::In.ReadToEnd())"
            )
            subprocess.run(["powershell", "-NoProfile", "-Command", script], input=text, text=True, check=False)
        elif system == "darwin":
            subprocess.run(["say", text], check=False)
        else:
            print(f"[tts] {text}")


class ResidentConversationLoop:
    def __init__(self, body: StackChanBodyClient, brain: BrainAdapter, tts: SystemTts, event_limit: int) -> None:
        self.body = body
        self.brain = brain
        self.tts = tts
        self.event_limit = event_limit

    def run_once(self, user_text: str) -> BrainReply:
        self.body.start_listening()
        recent_events = self.body.poll_events(limit=self.event_limit)
        self.body.start_thinking()
        reply = self.brain.reply(user_text, recent_events)
        self.body.start_speaking(reply.emotion)
        if reply.gesture:
            self.body.motion(reply.gesture)
        self.tts.speak(reply.text)
        self.body.stop_speaking()
        return reply

    def run_console(self) -> None:
        print("Resident loop started. Type text as ASR input. Use /quit to stop.")
        while True:
            user_text = input("you> ").strip()
            if user_text in {"/quit", "/exit"}:
                self.body.stop_speaking()
                return
            if not user_text:
                continue
            reply = self.run_once(user_text)
            print(f"openclaw> {reply.text}")
            time.sleep(0.1)


def build_brain(openclaw_url: str | None) -> BrainAdapter:
    if openclaw_url:
        return HttpBrainAdapter(openclaw_url)
    return DemoBrainAdapter()


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
