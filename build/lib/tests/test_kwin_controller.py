from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from workspace_templates.config import AppPaths
from workspace_templates.kwin_controller import KWinController, _script_object_path


class _FakeScriptInterface:
    def __init__(self) -> None:
        self.run_called = False

    def run(self) -> None:
        self.run_called = True


class _FakeScriptingInterface:
    def __init__(self, script_id: int) -> None:
        self.script_id = script_id
        self.loaded_paths: list[str] = []

    def loadScript(self, source_path: str) -> int:  # noqa: N802
        self.loaded_paths.append(source_path)
        return self.script_id


class _FakeBus:
    def __init__(self, scripting_interface: _FakeScriptingInterface, script_interface: _FakeScriptInterface) -> None:
        self.scripting_interface = scripting_interface
        self.script_interface = script_interface
        self.object_paths: list[tuple[str, str]] = []

    def get_object(self, service_name: str, object_path: str) -> object:
        self.object_paths.append((service_name, object_path))
        if object_path == "/Scripting":
            return self.scripting_interface
        if object_path == _script_object_path(self.scripting_interface.script_id):
            return self.script_interface
        raise AssertionError(f"Unexpected object path: {object_path}")


class _FakeBridge:
    def __init__(self, bus: _FakeBus) -> None:
        self.bus = bus
        self.service_name = "test.service"


class KWinControllerTests(unittest.TestCase):
    def test_start_request_uses_kwin_script_object_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            runtime_root = Path(tmp)
            paths = AppPaths(runtime_root, runtime_root, runtime_root).ensure()
            scripting_interface = _FakeScriptingInterface(script_id=13)
            script_interface = _FakeScriptInterface()
            bus = _FakeBus(scripting_interface, script_interface)
            bridge = _FakeBridge(bus)
            controller = KWinController(paths, bridge)

            with patch("workspace_templates.kwin_controller.dbus.Interface", side_effect=lambda proxy, _iface: proxy):
                running = controller._start_request('sendResult({ ok: true });')

            self.assertEqual(running.script_id, 13)
            self.assertTrue(script_interface.run_called)
            self.assertIn(("org.kde.KWin", "/Scripting"), bus.object_paths)
            self.assertIn(("org.kde.KWin", "/Scripting/Script13"), bus.object_paths)
            self.assertNotIn(("org.kde.KWin", "/13"), bus.object_paths)

    def test_generated_scripts_include_window_state_fields(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            runtime_root = Path(tmp)
            paths = AppPaths(runtime_root, runtime_root, runtime_root).ensure()
            scripting_interface = _FakeScriptingInterface(script_id=13)
            script_interface = _FakeScriptInterface()
            bus = _FakeBus(scripting_interface, script_interface)
            bridge = _FakeBridge(bus)
            controller = KWinController(paths, bridge)

            capture_script = controller._capture_script()
            restore_script = controller._restore_script(
                targets=[{"id": "1", "full_screen": True, "maximize_mode": 3, "geometry": None}],
                timeout_ms=1000,
                settle_ms=100,
            )

            self.assertIn("full_screen: !!window.fullScreen", capture_script)
            self.assertIn("maximize_mode: Number(window.maximizeMode || 0)", capture_script)
            self.assertIn("target.full_screen", restore_script)
            self.assertIn("target.maximize_mode", restore_script)


if __name__ == "__main__":
    unittest.main()
