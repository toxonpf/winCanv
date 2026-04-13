from __future__ import annotations

import os
import time
from dataclasses import dataclass
from typing import Any

from .config import AppPaths, default_paths
from .dbus_bridge import ResultBridge
from .desktop_entries import derive_launch_command, read_cwd, resolve_saved_launch_command
from .kwin_controller import KWinController
from .launcher import LaunchResult, launch_command
from .models import Rect, WindowTemplate, WorkspaceTemplate
from .template_store import TemplateStore


@dataclass(slots=True)
class SaveResult:
    name: str
    path: str
    window_count: int


@dataclass(slots=True)
class LoadResult:
    template_name: str
    requested_windows: int
    launched: list[LaunchResult]
    unmatched_target_ids: list[str]
    applied_count: int
    close_summary: dict[str, Any] | None


class WorkspaceTemplateManager:
    def __init__(self, paths: AppPaths | None = None) -> None:
        self.paths = paths or default_paths()
        self.store = TemplateStore(self.paths.templates_dir)
        self.bridge = ResultBridge()
        self.kwin = KWinController(self.paths, self.bridge, ignored_pids=[os.getpid()])

    def shutdown(self) -> None:
        self.bridge.stop()

    def list_templates(self) -> list[WorkspaceTemplate]:
        return self.store.list_templates()

    def delete_template(self, name: str) -> None:
        self.store.delete(name)

    def save_template(self, name: str) -> SaveResult:
        snapshot = self.kwin.capture_workspace()
        windows: list[WindowTemplate] = []

        for item in snapshot.get("windows", []):
            pid = int(item.get("pid") or 0)
            desktop_lookup_name = self._desktop_lookup_name(
                item.get("desktop_file_name"),
                item.get("resource_class"),
                item.get("app_name"),
                item.get("caption"),
            )
            command, command_source = derive_launch_command(pid=pid, desktop_file_name=desktop_lookup_name)
            window = WindowTemplate(
                id=WindowTemplate.new_id(),
                app_name=self._app_name(item),
                desktop_file_name=(item.get("desktop_file_name") or desktop_lookup_name) if command_source == "desktop" else (item.get("desktop_file_name") or None),
                resource_class=item.get("resource_class") or None,
                resource_name=item.get("resource_name") or None,
                window_role=item.get("window_role") or None,
                caption=item.get("caption") or None,
                command=command,
                command_source=command_source,
                cwd=read_cwd(pid),
                geometry=Rect.from_dict(item.get("geometry")),
                desktop=int(item.get("desktop") or 0),
                output_name=item.get("output_name") or None,
                output_geometry=Rect.from_dict(item.get("output_geometry")),
                is_full_screen=bool(item.get("full_screen", False)),
                maximize_mode=int(item.get("maximize_mode", 0) or 0),
            )
            windows.append(window)

        template = WorkspaceTemplate.create(name=name, windows=windows)
        path = self.store.save(template)
        return SaveResult(name=template.name, path=str(path), window_count=len(template.windows))

    def load_template(self, name: str, close_existing: bool = False) -> LoadResult:
        template = self.store.get(name)
        current = self.kwin.capture_workspace()
        reusable_matches = self._find_reusable_targets(template.windows, current.get("windows", []))
        reusable_target_ids = set(reusable_matches)

        close_summary = None
        if close_existing:
            keep_window_ids = sorted({window_id for window_id in reusable_matches.values() if window_id})
            close_summary = self.kwin.close_workspace_windows(except_ids=keep_window_ids)
            time.sleep(1.0)

        targets: list[dict[str, Any]] = []
        launches: list[WindowTemplate] = []
        for window in template.windows:
            target = {
                "id": window.id,
                "app_name": window.app_name,
                "desktop_file_name": window.desktop_file_name or "",
                "resource_class": window.resource_class or "",
                "resource_name": window.resource_name or "",
                "window_role": window.window_role or "",
                "caption": window.caption or "",
                "geometry": window.geometry.to_dict() if window.geometry else None,
                "desktop": window.desktop or 0,
                "output_name": window.output_name or "",
                "output_geometry": window.output_geometry.to_dict() if window.output_geometry else None,
                "full_screen": window.is_full_screen,
                "maximize_mode": window.maximize_mode,
            }
            targets.append(target)
            if window.id not in reusable_target_ids:
                launches.append(window)

        session = self.kwin.start_restore(targets=targets, timeout_ms=20000, settle_ms=900)
        launched: list[LaunchResult] = []
        try:
            for window in launches:
                desktop_lookup_name = self._desktop_lookup_name(window.desktop_file_name, window.resource_class, window.app_name, window.caption)
                command, _command_source = resolve_saved_launch_command(window.command, desktop_lookup_name)
                result = launch_command(command, cwd=window.cwd)
                launched.append(result)
                time.sleep(0.18)
            restore_result = session.wait(timeout_seconds=22.0)
        finally:
            session.stop()

        return LoadResult(
            template_name=template.name,
            requested_windows=len(template.windows),
            launched=launched,
            unmatched_target_ids=list(restore_result.get("unmatched", [])),
            applied_count=len(restore_result.get("applied", [])),
            close_summary=close_summary,
        )

    @staticmethod
    def _find_reusable_targets(
        template_windows: list[WindowTemplate],
        current_windows: list[dict[str, Any]],
    ) -> dict[str, str]:
        reused_target_ids: dict[str, str] = {}
        used_current_indexes: set[int] = set()

        for target in template_windows:
            best_index = -1
            best_score = 0
            best_distance = 1_000_000
            for index, current in enumerate(current_windows):
                if index in used_current_indexes:
                    continue
                current_score = WorkspaceTemplateManager._match_score(current, target)
                if current_score <= 0:
                    continue
                current_distance = WorkspaceTemplateManager._geometry_distance(current, target)
                if current_score > best_score or (current_score == best_score and current_distance < best_distance):
                    best_index = index
                    best_score = current_score
                    best_distance = current_distance
            if best_index >= 0:
                used_current_indexes.add(best_index)
                reused_target_ids[target.id] = str(current_windows[best_index].get("internal_id") or "")
        return reused_target_ids

    @staticmethod
    def _match_score(current: dict[str, Any], target: WindowTemplate) -> int:
        score = 0
        if WorkspaceTemplateManager._same_value(current.get("desktop_file_name"), target.desktop_file_name):
            score += 100
        if WorkspaceTemplateManager._same_value(current.get("resource_class"), target.resource_class):
            score += 35
        if WorkspaceTemplateManager._same_value(current.get("resource_name"), target.resource_name):
            score += 20
        if WorkspaceTemplateManager._same_value(current.get("window_role"), target.window_role):
            score += 10
        if WorkspaceTemplateManager._same_value(current.get("caption"), target.caption):
            score += 3
        return score

    @staticmethod
    def _same_value(current: Any, target: str | None) -> bool:
        left = str(current or "").strip().lower()
        right = str(target or "").strip().lower()
        return bool(left and right and left == right)

    @staticmethod
    def _geometry_distance(current: dict[str, Any], target: WindowTemplate) -> int:
        current_geometry = Rect.from_dict(current.get("geometry"))
        target_geometry = target.geometry
        if current_geometry is None or target_geometry is None:
            return 1_000_000
        return (
            abs(current_geometry.x - target_geometry.x)
            + abs(current_geometry.y - target_geometry.y)
            + abs(current_geometry.width - target_geometry.width)
            + abs(current_geometry.height - target_geometry.height)
        )

    @staticmethod
    def _app_name(window: dict[str, Any]) -> str:
        for key in ("desktop_file_name", "resource_class", "resource_name", "caption"):
            value = str(window.get(key) or "").strip()
            if value:
                return value
        return "unknown-app"

    @staticmethod
    def _desktop_lookup_name(*values: object) -> str | None:
        for value in values:
            text = str(value or "").strip()
            if text:
                return text
        return None
