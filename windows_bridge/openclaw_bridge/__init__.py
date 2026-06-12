"""Reusable OpenClaw StackChan Windows bridge helpers."""

from .body_tools import BodyTool, BodyToolResult, BodyToolRouter
from .body_client import StackChanBodyClient
from .camera_vision import FaceBox, OpenCvFaceDetector, observation_from_face_box
from .events import BodyIntent, intent_from_event, intents_from_events, strongest_intent
from .face_tracking import FaceObservation, FaceTracker, LookTarget, parse_observation
from .lifecycle import LifeCycleManager, LifeState, LifeStatus, ProactiveAction, summarize_intents
from .memory_context import MemoryContext, load_memory_context
from .resident_loop import BrainReply, DemoBrainAdapter, HttpBrainAdapter, ResidentConversationLoop, SystemTts
from .speech import SpeechCue, build_speech_cue, estimate_speech_duration_s

__all__ = [
    "BodyIntent",
    "BodyTool",
    "BodyToolResult",
    "BodyToolRouter",
    "BrainReply",
    "DemoBrainAdapter",
    "FaceBox",
    "FaceObservation",
    "FaceTracker",
    "HttpBrainAdapter",
    "LifeCycleManager",
    "LifeState",
    "LifeStatus",
    "LookTarget",
    "MemoryContext",
    "OpenCvFaceDetector",
    "ProactiveAction",
    "ResidentConversationLoop",
    "SpeechCue",
    "StackChanBodyClient",
    "SystemTts",
    "build_speech_cue",
    "estimate_speech_duration_s",
    "intent_from_event",
    "intents_from_events",
    "load_memory_context",
    "observation_from_face_box",
    "parse_observation",
    "strongest_intent",
    "summarize_intents",
]
