#!/usr/bin/env python3
"""Fake OpenClaw brain HTTP server for no-hardware integration tests."""

from __future__ import annotations

import argparse
import json
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any


def build_reply(payload: dict[str, Any]) -> dict[str, Any]:
    text = str(payload.get("text") or "")
    events = payload.get("events")
    if not isinstance(events, list):
        events = []
    memory_context = payload.get("memory_context")
    if isinstance(memory_context, dict) and memory_context.get("summary") and "记得" in text:
        return {
            "text": f"fake OpenClaw 带着记忆回应：{memory_context.get('summary')}",
            "emotion": "love",
            "gesture": "nod",
        }
    tactile = [event for event in events if event.get("kind") == "pressure"]
    if tactile:
        action = tactile[-1].get("message", {}).get("action")
        if action == "hold":
            return {"text": "我感受到你在摸我，这是 fake OpenClaw 的回应。", "emotion": "love", "gesture": "nod"}
    if "困" in text or "sleep" in text.lower():
        return {"text": "那我安静一点，先进入 sleepy 状态。", "emotion": "sleepy", "gesture": None}
    return {"text": f"fake OpenClaw 收到：{text}", "emotion": "happy", "gesture": "nod"}


def make_handler() -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def do_POST(self) -> None:  # noqa: N802
            if self.path != "/chat":
                self._send_json(HTTPStatus.NOT_FOUND, {"error": "not found"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                body = self.rfile.read(length).decode("utf-8")
                payload = json.loads(body)
                if not isinstance(payload, dict):
                    raise ValueError("body must be an object")
            except (ValueError, json.JSONDecodeError) as exc:
                self._send_json(HTTPStatus.BAD_REQUEST, {"error": str(exc)})
                return
            self._send_json(HTTPStatus.OK, build_reply(payload))

        def log_message(self, fmt: str, *args: Any) -> None:
            print(f"[fake-openclaw] {self.address_string()} {fmt % args}")

        def _send_json(self, status: HTTPStatus, payload: dict[str, Any]) -> None:
            data = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser(description="Fake OpenClaw brain server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=8899, type=int)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), make_handler())
    print(f"[fake-openclaw] listening on http://{args.host}:{args.port}/chat")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[fake-openclaw] stopping")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
