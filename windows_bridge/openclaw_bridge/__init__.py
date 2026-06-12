"""Reusable OpenClaw StackChan Windows bridge helpers."""

from .body_client import StackChanBodyClient
from .events import BodyIntent, intent_from_event, intents_from_events, strongest_intent
from .face_tracking import FaceObservation, FaceTracker, LookTarget, parse_observation
from .resident_loop import BrainReply, DemoBrainAdapter, HttpBrainAdapter, ResidentConversationLoop, SystemTts

__all__ = [
    "BodyIntent",
    "BrainReply",
    "DemoBrainAdapter",
    "FaceObservation",
    "FaceTracker",
    "HttpBrainAdapter",
    "LookTarget",
    "ResidentConversationLoop",
    "StackChanBodyClient",
    "SystemTts",
    "intent_from_event",
    "intents_from_events",
    "parse_observation",
    "strongest_intent",
]
