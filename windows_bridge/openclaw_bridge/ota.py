"""OTA planning models for future firmware update workflows."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class OtaPackage:
    path: str
    version: str
    sha256: str

    def exists(self) -> bool:
        return Path(self.path).exists()


@dataclass(frozen=True)
class OtaPlan:
    package: OtaPackage
    target_device_id: str
    transport: str = "tcp-json"
    require_user_confirmation: bool = True

    def ready_for_dispatch(self) -> bool:
        return self.package.exists() and len(self.package.sha256) == 64 and self.require_user_confirmation
