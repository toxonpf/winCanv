from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any
from uuid import uuid4


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


@dataclass(slots=True)
class Rect:
    x: int
    y: int
    width: int
    height: int

    @classmethod
    def from_dict(cls, data: dict[str, Any] | None) -> "Rect | None":
        if not data:
            return None
        return cls(
            x=int(data["x"]),
            y=int(data["y"]),
            width=int(data["width"]),
            height=int(data["height"]),
        )

    def to_dict(self) -> dict[str, int]:
        return {
            "x": self.x,
            "y": self.y,
            "width": self.width,
            "height": self.height,
        }


@dataclass(slots=True)
class WindowTemplate:
    id: str
    app_name: str
    desktop_file_name: str | None
    resource_class: str | None
    resource_name: str | None
    window_role: str | None
    caption: str | None
    command: list[str] = field(default_factory=list)
    command_source: str | None = None
    cwd: str | None = None
    geometry: Rect | None = None
    desktop: int | None = None
    output_name: str | None = None
    output_geometry: Rect | None = None
    is_full_screen: bool = False
    maximize_mode: int = 0

    @classmethod
    def new_id(cls) -> str:
        return str(uuid4())

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "WindowTemplate":
        return cls(
            id=str(data["id"]),
            app_name=str(data["app_name"]),
            desktop_file_name=data.get("desktop_file_name"),
            resource_class=data.get("resource_class"),
            resource_name=data.get("resource_name"),
            window_role=data.get("window_role"),
            caption=data.get("caption"),
            command=list(data.get("command") or []),
            command_source=data.get("command_source"),
            cwd=data.get("cwd"),
            geometry=Rect.from_dict(data.get("geometry")),
            desktop=data.get("desktop"),
            output_name=data.get("output_name"),
            output_geometry=Rect.from_dict(data.get("output_geometry")),
            is_full_screen=bool(data.get("is_full_screen", False)),
            maximize_mode=int(data.get("maximize_mode", 0) or 0),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "app_name": self.app_name,
            "desktop_file_name": self.desktop_file_name,
            "resource_class": self.resource_class,
            "resource_name": self.resource_name,
            "window_role": self.window_role,
            "caption": self.caption,
            "command": self.command,
            "command_source": self.command_source,
            "cwd": self.cwd,
            "geometry": self.geometry.to_dict() if self.geometry else None,
            "desktop": self.desktop,
            "output_name": self.output_name,
            "output_geometry": self.output_geometry.to_dict() if self.output_geometry else None,
            "is_full_screen": self.is_full_screen,
            "maximize_mode": self.maximize_mode,
        }

    def match_signature(self) -> tuple[str, str, str]:
        return (
            (self.desktop_file_name or "").strip().lower(),
            (self.resource_class or "").strip().lower(),
            (self.resource_name or "").strip().lower(),
        )


@dataclass(slots=True)
class WorkspaceTemplate:
    name: str
    created_at: str
    version: int
    windows: list[WindowTemplate]

    @classmethod
    def create(cls, name: str, windows: list[WindowTemplate]) -> "WorkspaceTemplate":
        return cls(
            name=name,
            created_at=utc_now_iso(),
            version=2,
            windows=windows,
        )

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "WorkspaceTemplate":
        return cls(
            name=str(data["name"]),
            created_at=str(data["created_at"]),
            version=int(data.get("version", 1)),
            windows=[WindowTemplate.from_dict(item) for item in data.get("windows", [])],
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "created_at": self.created_at,
            "version": self.version,
            "windows": [window.to_dict() for window in self.windows],
        }
