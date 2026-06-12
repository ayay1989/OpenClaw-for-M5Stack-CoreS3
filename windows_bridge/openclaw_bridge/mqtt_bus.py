"""Minimal MQTT 3.1.1 client for home-network StackChan routing."""

from __future__ import annotations

import json
import socket
import threading
import time
from dataclasses import dataclass
from typing import Any, Callable


MessageHandler = Callable[[str, dict[str, Any]], None]


@dataclass(frozen=True)
class MqttConfig:
    host: str
    port: int = 1883
    client_id: str = "openclaw-stackchan-bridge"
    topic_prefix: str = "openclaw/home"
    username: str | None = None
    password: str | None = None
    keepalive_s: int = 30

    @property
    def command_topic(self) -> str:
        return f"{self.topic_prefix.rstrip('/')}/devices/+/command"

    @property
    def event_topic(self) -> str:
        return f"{self.topic_prefix.rstrip('/')}/events"


class MqttBusClient:
    """Small blocking MQTT client used by the Windows Bridge.

    It intentionally supports only the subset we need now: clean-session
    MQTT 3.1.1, QoS 0 publish, and command subscription.
    """

    def __init__(self, config: MqttConfig, on_message: MessageHandler) -> None:
        self.config = config
        self.on_message = on_message
        self._socket: socket.socket | None = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._packet_id = 1

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, name="mqtt-bus", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        with self._lock:
            sock = self._socket
            self._socket = None
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass

    def publish_json(self, topic: str, payload: dict[str, Any]) -> bool:
        data = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        packet = mqtt_publish_packet(topic, data)
        with self._lock:
            sock = self._socket
        if sock is None:
            return False
        try:
            sock.sendall(packet)
            return True
        except OSError:
            self.stop()
            return False

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                self._connect_once()
                self._read_loop()
            except OSError as exc:
                print(f"[mqtt] connection error: {exc}")
            except ValueError as exc:
                print(f"[mqtt] protocol error: {exc}")
            self.stop()
            if not self._stop.is_set():
                time.sleep(5)

    def _connect_once(self) -> None:
        sock = socket.create_connection((self.config.host, self.config.port), timeout=10)
        sock.settimeout(1.0)
        sock.sendall(mqtt_connect_packet(self.config))
        connack = self._read_packet(sock)
        if connack[0] != 0x20 or connack[1] != b"\x00\x00":
            raise ValueError("MQTT CONNACK rejected")
        sock.sendall(mqtt_subscribe_packet(self._next_packet_id(), self.config.command_topic))
        suback = self._read_packet(sock)
        if suback[0] != 0x90:
            raise ValueError("MQTT SUBACK missing")
        with self._lock:
            self._socket = sock
        print(f"[mqtt] connected to {self.config.host}:{self.config.port}, subscribed {self.config.command_topic}")

    def _read_loop(self) -> None:
        while not self._stop.is_set():
            with self._lock:
                sock = self._socket
            if sock is None:
                return
            try:
                packet_type, payload = self._read_packet(sock)
            except socket.timeout:
                try:
                    sock.sendall(b"\xC0\x00")
                except OSError:
                    return
                continue
            if packet_type == 0x30:
                topic, data = mqtt_parse_publish(payload)
                try:
                    message = json.loads(data.decode("utf-8"))
                except json.JSONDecodeError:
                    continue
                if isinstance(message, dict):
                    self.on_message(topic, message)

    def _read_packet(self, sock: socket.socket) -> tuple[int, bytes]:
        first = _recv_exact(sock, 1)
        remaining_length = _mqtt_decode_remaining_length(sock)
        payload = _recv_exact(sock, remaining_length)
        return first[0], payload

    def _next_packet_id(self) -> int:
        packet_id = self._packet_id
        self._packet_id += 1
        if self._packet_id > 65535:
            self._packet_id = 1
        return packet_id


def mqtt_connect_packet(config: MqttConfig) -> bytes:
    flags = 0x02
    fields = [_mqtt_string(config.client_id)]
    if config.username is not None:
        flags |= 0x80
        fields.append(_mqtt_string(config.username))
    if config.password is not None:
        flags |= 0x40
        fields.append(_mqtt_string(config.password))
    variable_header = _mqtt_string("MQTT") + bytes([4, flags]) + config.keepalive_s.to_bytes(2, "big")
    payload = b"".join(fields)
    return bytes([0x10]) + _mqtt_remaining_length(len(variable_header) + len(payload)) + variable_header + payload


def mqtt_subscribe_packet(packet_id: int, topic: str) -> bytes:
    payload = packet_id.to_bytes(2, "big") + _mqtt_string(topic) + b"\x00"
    return bytes([0x82]) + _mqtt_remaining_length(len(payload)) + payload


def mqtt_publish_packet(topic: str, payload: bytes) -> bytes:
    body = _mqtt_string(topic) + payload
    return bytes([0x30]) + _mqtt_remaining_length(len(body)) + body


def mqtt_parse_publish(payload: bytes) -> tuple[str, bytes]:
    if len(payload) < 2:
        raise ValueError("MQTT PUBLISH missing topic")
    topic_len = int.from_bytes(payload[:2], "big")
    topic_start = 2
    topic_end = topic_start + topic_len
    if len(payload) < topic_end:
        raise ValueError("MQTT PUBLISH topic truncated")
    topic = payload[topic_start:topic_end].decode("utf-8")
    return topic, payload[topic_end:]


def _mqtt_string(value: str) -> bytes:
    data = value.encode("utf-8")
    return len(data).to_bytes(2, "big") + data


def _mqtt_remaining_length(value: int) -> bytes:
    if value < 0:
        raise ValueError("remaining length must be non-negative")
    encoded = bytearray()
    while True:
        digit = value % 128
        value //= 128
        if value > 0:
            digit |= 0x80
        encoded.append(digit)
        if value == 0:
            return bytes(encoded)


def _mqtt_decode_remaining_length(sock: socket.socket) -> int:
    multiplier = 1
    value = 0
    for _ in range(4):
        encoded = _recv_exact(sock, 1)[0]
        value += (encoded & 127) * multiplier
        if encoded & 128 == 0:
            return value
        multiplier *= 128
    raise ValueError("malformed MQTT remaining length")


def _recv_exact(sock: socket.socket, length: int) -> bytes:
    data = b""
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise OSError("socket closed")
        data += chunk
    return data
