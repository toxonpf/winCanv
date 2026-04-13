from __future__ import annotations

import unittest
from unittest.mock import patch

from workspace_templates.manager import WorkspaceTemplateManager
from workspace_templates.models import Rect, WindowTemplate, WorkspaceTemplate


class _FakeStore:
    def __init__(self, template: WorkspaceTemplate) -> None:
        self.template = template
        self.saved_template: WorkspaceTemplate | None = None

    def get(self, _name: str) -> WorkspaceTemplate:
        return self.template

    def save(self, template: WorkspaceTemplate) -> str:
        self.saved_template = template
        return "/tmp/fake-template.json"


class _FakeSession:
    def __init__(self, payload: dict[str, object]) -> None:
        self.payload = payload
        self.stopped = False

    def wait(self, timeout_seconds: float) -> dict[str, object]:
        return self.payload

    def stop(self) -> None:
        self.stopped = True


class _FakeKwin:
    def __init__(self, current_snapshot: dict[str, object], restore_payload: dict[str, object]) -> None:
        self.current_snapshot = current_snapshot
        self.restore_payload = restore_payload
        self.started_targets: list[dict[str, object]] | None = None
        self.closed_except_ids: list[str] | None = None

    def capture_workspace(self) -> dict[str, object]:
        return self.current_snapshot

    def close_workspace_windows(self, except_ids: list[str] | None = None) -> dict[str, object]:
        self.closed_except_ids = list(except_ids or [])
        return {"ok": True, "closed_count": 0, "closed_ids": []}

    def start_restore(self, targets: list[dict[str, object]], timeout_ms: int = 20000, settle_ms: int = 900) -> _FakeSession:
        self.started_targets = targets
        return _FakeSession(self.restore_payload)


class WorkspaceTemplateManagerTests(unittest.TestCase):
    def _make_manager(self, template: WorkspaceTemplate, current_snapshot: dict[str, object], restore_payload: dict[str, object]) -> WorkspaceTemplateManager:
        manager = object.__new__(WorkspaceTemplateManager)
        manager.store = _FakeStore(template)
        manager.kwin = _FakeKwin(current_snapshot, restore_payload)
        return manager

    def test_save_template_captures_window_state(self) -> None:
        manager = self._make_manager(WorkspaceTemplate.create("empty", []), {
            "windows": [
                {
                    "pid": 0,
                    "desktop_file_name": "org.kde.konsole",
                    "resource_class": "konsole",
                    "resource_name": "konsole",
                    "window_role": "",
                    "caption": "shell",
                    "geometry": {"x": 10, "y": 20, "width": 900, "height": 700},
                    "desktop": 1,
                    "full_screen": True,
                    "maximize_mode": 3,
                }
            ]
        }, {"applied": [], "unmatched": []})

        result = manager.save_template("dev")

        self.assertEqual(result.window_count, 1)
        saved_window = manager.store.saved_template.windows[0]
        self.assertTrue(saved_window.is_full_screen)
        self.assertEqual(saved_window.maximize_mode, 3)

    def test_save_template_infers_desktop_file_name_from_resource_class(self) -> None:
        manager = self._make_manager(WorkspaceTemplate.create("empty", []), {
            "windows": [
                {
                    "pid": 0,
                    "desktop_file_name": "",
                    "resource_class": "steam",
                    "resource_name": "steamwebhelper",
                    "window_role": "",
                    "caption": "Steam",
                    "geometry": {"x": 10, "y": 20, "width": 900, "height": 700},
                    "desktop": 1,
                }
            ]
        }, {"applied": [], "unmatched": []})

        with patch("workspace_templates.manager.derive_launch_command", return_value=(["steam"], "desktop")):
            manager.save_template("games")

        saved_window = manager.store.saved_template.windows[0]
        self.assertEqual(saved_window.desktop_file_name, "steam")

    def test_load_reuses_open_window_on_partial_match(self) -> None:
        target = WindowTemplate(
            id="target-1",
            app_name="Konsole",
            desktop_file_name="org.kde.konsole",
            resource_class="konsole",
            resource_name="konsole",
            window_role=None,
            caption="shell",
            command=["konsole"],
            geometry=Rect(10, 10, 900, 600),
            desktop=1,
        )
        template = WorkspaceTemplate.create("dev", [target])
        current_snapshot = {
            "windows": [
                {
                    "internal_id": "41",
                    "desktop_file_name": "",
                    "resource_class": "konsole",
                    "resource_name": "konsole",
                    "window_role": "",
                    "caption": "shell",
                    "geometry": {"x": 0, "y": 0, "width": 920, "height": 620},
                }
            ]
        }
        restore_payload = {"applied": [{"target_id": "target-1"}], "unmatched": []}
        manager = self._make_manager(template, current_snapshot, restore_payload)

        with patch("workspace_templates.manager.launch_command", side_effect=AssertionError("launch should not happen")):
            result = manager.load_template("dev", close_existing=False)

        self.assertEqual(result.template_name, "dev")
        self.assertEqual(result.applied_count, 1)
        self.assertEqual(len(result.launched), 0)
        self.assertEqual(manager.kwin.started_targets[0]["full_screen"], False)
        self.assertEqual(manager.kwin.started_targets[0]["maximize_mode"], 0)

    def test_close_existing_keeps_reusable_window_open(self) -> None:
        target = WindowTemplate(
            id="target-1",
            app_name="Konsole",
            desktop_file_name="org.kde.konsole",
            resource_class="konsole",
            resource_name="konsole",
            window_role=None,
            caption="shell",
            command=["konsole"],
            geometry=Rect(10, 10, 900, 600),
            desktop=1,
            is_full_screen=True,
            maximize_mode=3,
        )
        template = WorkspaceTemplate.create("dev", [target])
        current_snapshot = {
            "windows": [
                {
                    "internal_id": "42",
                    "desktop_file_name": "",
                    "resource_class": "konsole",
                    "resource_name": "konsole",
                    "window_role": "",
                    "caption": "shell",
                    "geometry": {"x": 0, "y": 0, "width": 920, "height": 620},
                }
            ]
        }
        restore_payload = {"applied": [{"target_id": "target-1"}], "unmatched": []}
        manager = self._make_manager(template, current_snapshot, restore_payload)

        with patch("workspace_templates.manager.launch_command", side_effect=AssertionError("launch should not happen")):
            result = manager.load_template("dev", close_existing=True)

        self.assertEqual(result.applied_count, 1)
        self.assertEqual(len(result.launched), 0)
        self.assertEqual(manager.kwin.closed_except_ids, ["42"])
        self.assertEqual(manager.kwin.started_targets[0]["full_screen"], True)
        self.assertEqual(manager.kwin.started_targets[0]["maximize_mode"], 3)

    def test_load_still_launches_missing_window(self) -> None:
        target = WindowTemplate(
            id="target-1",
            app_name="Konsole",
            desktop_file_name="org.kde.konsole",
            resource_class="konsole",
            resource_name="konsole",
            window_role=None,
            caption=None,
            command=["konsole"],
            geometry=Rect(10, 10, 900, 600),
            desktop=1,
        )
        template = WorkspaceTemplate.create("dev", [target])
        manager = self._make_manager(template, {"windows": []}, {"applied": [], "unmatched": ["target-1"]})

        with patch("workspace_templates.manager.launch_command", return_value=object()) as launch_mock:
            result = manager.load_template("dev", close_existing=False)

        self.assertEqual(launch_mock.call_count, 1)
        self.assertEqual(len(result.launched), 1)
        self.assertEqual(result.unmatched_target_ids, ["target-1"])

    def test_load_prefers_desktop_command_for_saved_template(self) -> None:
        target = WindowTemplate(
            id="target-1",
            app_name="Steam",
            desktop_file_name=None,
            resource_class="steam",
            resource_name="steam",
            window_role=None,
            caption=None,
            command=["steamwebhelper", "--type=renderer"],
            geometry=Rect(10, 10, 900, 600),
            desktop=1,
        )
        template = WorkspaceTemplate.create("games", [target])
        manager = self._make_manager(template, {"windows": []}, {"applied": [], "unmatched": ["target-1"]})

        with patch("workspace_templates.manager.resolve_saved_launch_command", return_value=(["steam"], "desktop")) as resolve_mock, patch(
            "workspace_templates.manager.launch_command",
            return_value=object(),
        ) as launch_mock:
            manager.load_template("games", close_existing=False)

        resolve_mock.assert_called_once_with(["steamwebhelper", "--type=renderer"], "steam")
        launch_mock.assert_called_once_with(["steam"], cwd=None)


if __name__ == "__main__":
    unittest.main()
