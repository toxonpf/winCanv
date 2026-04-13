from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from textwrap import dedent
from typing import Any
from uuid import uuid4

import dbus

from .config import AppPaths
from .dbus_bridge import DBUS_INTERFACE, DBUS_OBJECT_PATH, ResultBridge


class KWinError(RuntimeError):
    pass


@dataclass(slots=True)
class RunningScript:
    request_id: str
    script_id: int
    bridge: ResultBridge
    bus: dbus.SessionBus
    source_path: Path

    def wait(self, timeout_seconds: float) -> dict[str, Any]:
        return self.bridge.wait_for(self.request_id, timeout_seconds)

    def stop(self) -> None:
        try:
            proxy = self.bus.get_object("org.kde.KWin", _script_object_path(self.script_id))
            iface = dbus.Interface(proxy, "org.kde.kwin.Script")
            iface.stop()
        except Exception:  # noqa: BLE001
            pass
        try:
            self.source_path.unlink(missing_ok=True)
        except OSError:
            pass


class KWinController:
    def __init__(self, paths: AppPaths, bridge: ResultBridge, ignored_pids: list[int] | None = None) -> None:
        self.paths = paths
        self.bridge = bridge
        self.bus = bridge.bus
        self.ignored_pids = sorted(set(ignored_pids or []))

    def capture_workspace(self) -> dict[str, Any]:
        payload = self._run_request(self._capture_script(), timeout_seconds=5.0)
        if not payload.get("ok", False):
            raise KWinError(payload.get("error", "Failed to capture workspace"))
        return payload

    def close_workspace_windows(self, except_ids: list[str] | None = None) -> dict[str, Any]:
        payload = self._run_request(self._close_script(except_ids or []), timeout_seconds=6.0)
        if not payload.get("ok", False):
            raise KWinError(payload.get("error", "Failed to close windows"))
        return payload

    def start_restore(self, targets: list[dict[str, Any]], timeout_ms: int = 20000, settle_ms: int = 900) -> RunningScript:
        return self._start_request(self._restore_script(targets=targets, timeout_ms=timeout_ms, settle_ms=settle_ms))

    def place_window(self, pid: int, geometry: dict[str, int], caption: str | None = None, timeout_ms: int = 1800) -> dict[str, Any]:
        payload = self._run_request(
            self._place_window_script(pid=pid, geometry=geometry, caption=caption, timeout_ms=timeout_ms),
            timeout_seconds=max(2.0, (timeout_ms / 1000.0) + 1.0),
        )
        if not payload.get("ok", False):
            raise KWinError(payload.get("error", "Failed to place window"))
        return payload

    def _run_request(self, script_source: str, timeout_seconds: float) -> dict[str, Any]:
        running = self._start_request(script_source)
        try:
            return running.wait(timeout_seconds)
        finally:
            running.stop()

    def _start_request(self, script_source: str) -> RunningScript:
        request_id = str(uuid4())
        source = self._wrap_script(request_id, script_source)
        source_path = self.paths.runtime_dir / f"kwin-{request_id}.js"
        source_path.write_text(source, encoding="utf-8")

        scripting = dbus.Interface(
            self.bus.get_object("org.kde.KWin", "/Scripting"),
            "org.kde.kwin.Scripting",
        )
        try:
            script_id = int(scripting.loadScript(str(source_path)))
        except dbus.DBusException as exc:
            raise KWinError(f"Unable to load KWin script: {exc.get_dbus_message()}") from exc

        try:
            script = dbus.Interface(
                self.bus.get_object("org.kde.KWin", _script_object_path(script_id)),
                "org.kde.kwin.Script",
            )
            script.run()
        except dbus.DBusException as exc:
            raise KWinError(f"Unable to run KWin script: {exc.get_dbus_message()}") from exc

        return RunningScript(
            request_id=request_id,
            script_id=script_id,
            bridge=self.bridge,
            bus=self.bus,
            source_path=source_path,
        )

    def _wrap_script(self, request_id: str, script_body: str) -> str:
        return dedent(
            f"""
            "use strict";
            const SERVICE = {json.dumps(self.bridge.service_name)};
            const PATH = {json.dumps(DBUS_OBJECT_PATH)};
            const INTERFACE = {json.dumps(DBUS_INTERFACE)};
            const REQUEST_ID = {json.dumps(request_id)};
            const IGNORED_PIDS = {json.dumps(self.ignored_pids)};

            function sendResult(payload) {{
                callDBus(SERVICE, PATH, INTERFACE, "ReportResult", REQUEST_ID, JSON.stringify(payload));
            }}

            function fail(errorMessage) {{
                sendResult({{ ok: false, error: String(errorMessage) }});
            }}

            try {{
            {script_body}
            }} catch (error) {{
                fail(error && error.toString ? error.toString() : error);
            }}
            """
        ).strip() + "\n"
    def _capture_script(self) -> str:
        return dedent(
            """
                function shouldIncludeWindow(window) {
                    if (!window) return false;
                    if (!window.managed || window.deleted) return false;
                    if (window.desktopWindow || window.dock || window.popupWindow || window.outline) return false;
                    if (window.specialWindow) return false;
                    if (!(window.normalWindow || window.dialog || window.utility)) return false;
                    if (IGNORED_PIDS.indexOf(window.pid) !== -1) return false;
                    return true;
                }

                function rectData(rect) {
                    if (!rect) return null;
                    return {
                        x: Math.round(rect.x),
                        y: Math.round(rect.y),
                        width: Math.round(rect.width),
                        height: Math.round(rect.height)
                    };
                }

                function desktopNumber(window) {
                    if (window.onAllDesktops || !window.desktops || window.desktops.length === 0) {
                        return 0;
                    }
                    const desktop = window.desktops[0];
                    if (desktop.x11DesktopNumber !== undefined) return desktop.x11DesktopNumber;
                    if (desktop.id !== undefined) return desktop.id;
                    return 0;
                }

                const outputs = (workspace.screens || []).map(function(output) {
                    return {
                        name: output.name || "",
                        geometry: rectData(output.geometry)
                    };
                });

                const windows = workspace.stackingOrder
                    .filter(shouldIncludeWindow)
                    .map(function(window) {
                        return {
                            internal_id: String(window.internalId),
                            pid: window.pid,
                            desktop_file_name: window.desktopFileName !== undefined ? window.desktopFileName : "",
                            resource_name: window.resourceName || "",
                            resource_class: window.resourceClass || "",
                            window_role: window.windowRole || "",
                            caption: window.caption || "",
                            geometry: rectData(window.frameGeometry),
                            desktop: desktopNumber(window),
                            output_name: window.output && window.output.name ? window.output.name : "",
                            output_geometry: window.output ? rectData(window.output.geometry) : null,
                            full_screen: !!window.fullScreen,
                            maximize_mode: Number(window.maximizeMode || 0)
                        };
                    });

                sendResult({
                    ok: true,
                    windows: windows,
                    outputs: outputs
                });
            """
        ).rstrip()

    def _close_script(self, except_ids: list[str]) -> str:
        payload = {"except_ids": except_ids}
        return dedent(
            f"""
                const CLOSE = {json.dumps(payload)};
                const closed = [];

                function shouldClose(window) {{
                    if (!window) return false;
                    if (!window.managed || window.deleted) return false;
                    if (window.desktopWindow || window.dock || window.popupWindow || window.outline) return false;
                    if (window.specialWindow) return false;
                    if (IGNORED_PIDS.indexOf(window.pid) !== -1) return false;
                    if (CLOSE.except_ids.indexOf(String(window.internalId)) !== -1) return false;
                    return window.closeable;
                }}

                workspace.stackingOrder.forEach(function(window) {{
                    if (!shouldClose(window)) return;
                    closed.push(String(window.internalId));
                    window.closeWindow();
                }});

                sendResult({{
                    ok: true,
                    closed_count: closed.length,
                    closed_ids: closed
                }});
            """
        ).rstrip()

    def _restore_script(self, targets: list[dict[str, Any]], timeout_ms: int, settle_ms: int) -> str:
        payload = {
            "targets": targets,
            "timeout_ms": timeout_ms,
            "settle_ms": settle_ms,
        }
        return dedent(
            f"""
                const RESTORE = {json.dumps(payload)};

                function rectData(rect) {{
                    if (!rect) return null;
                    return {{
                        x: Math.round(rect.x),
                        y: Math.round(rect.y),
                        width: Math.round(rect.width),
                        height: Math.round(rect.height)
                    }};
                }}

                function shouldUseWindow(window) {{
                    if (!window) return false;
                    if (!window.managed || window.deleted) return false;
                    if (window.desktopWindow || window.dock || window.popupWindow || window.outline) return false;
                    if (window.specialWindow) return false;
                    if (!(window.normalWindow || window.dialog || window.utility)) return false;
                    if (IGNORED_PIDS.indexOf(window.pid) !== -1) return false;
                    return true;
                }}

                function getDesktopNumber(window) {{
                    if (window.onAllDesktops || !window.desktops || window.desktops.length === 0) return 0;
                    const desktop = window.desktops[0];
                    if (desktop.x11DesktopNumber !== undefined) return desktop.x11DesktopNumber;
                    if (desktop.id !== undefined) return desktop.id;
                    return 0;
                }}

                function findDesktop(desktopNumber) {{
                    if (!desktopNumber) return null;
                    for (const desktop of workspace.desktops || []) {{
                        if (desktop.x11DesktopNumber === desktopNumber || desktop.id === desktopNumber) {{
                            return desktop;
                        }}
                    }}
                    return null;
                }}

                function findOutput(outputName) {{
                    for (const output of workspace.screens || []) {{
                        if ((output.name || "") === (outputName || "")) return output;
                    }}
                    return null;
                }}

                function buildRect(x, y, width, height) {{
                    return {{ x: x, y: y, width: width, height: height }};
                }}

                function clamp(value, minimum, maximum) {{
                    if (value < minimum) return minimum;
                    if (value > maximum) return maximum;
                    return value;
                }}

                function targetRect(target) {{
                    if (!target.geometry) return null;
                    let x = target.geometry.x;
                    let y = target.geometry.y;
                    const width = target.geometry.width;
                    const height = target.geometry.height;

                    if (target.output_name && target.output_geometry) {{
                        const currentOutput = findOutput(target.output_name);
                        if (currentOutput && currentOutput.geometry) {{
                            const dx = target.geometry.x - target.output_geometry.x;
                            const dy = target.geometry.y - target.output_geometry.y;
                            const maxX = Math.round(currentOutput.geometry.x + Math.max(0, currentOutput.geometry.width - width));
                            const maxY = Math.round(currentOutput.geometry.y + Math.max(0, currentOutput.geometry.height - height));
                            x = clamp(Math.round(currentOutput.geometry.x + dx), Math.round(currentOutput.geometry.x), maxX);
                            y = clamp(Math.round(currentOutput.geometry.y + dy), Math.round(currentOutput.geometry.y), maxY);
                        }}
                    }}
                    return buildRect(x, y, width, height);
                }}

                function score(window, target) {{
                    let value = 0;
                    if (target.desktop_file_name && window.desktopFileName === target.desktop_file_name) value += 100;
                    if (target.resource_class && window.resourceClass === target.resource_class) value += 35;
                    if (target.resource_name && window.resourceName === target.resource_name) value += 20;
                    if (target.window_role && window.windowRole === target.window_role) value += 10;
                    if (target.caption && window.caption === target.caption) value += 3;
                    return value;
                }}

                function geometryDistance(window, target) {{
                    if (!target.geometry || !window.frameGeometry) return 1000000;
                    const dx = Math.abs(window.frameGeometry.x - target.geometry.x);
                    const dy = Math.abs(window.frameGeometry.y - target.geometry.y);
                    const dw = Math.abs(window.frameGeometry.width - target.geometry.width);
                    const dh = Math.abs(window.frameGeometry.height - target.geometry.height);
                    return dx + dy + dw + dh;
                }}

                function assignWindow(window, target, force) {{
                    if (!target || !window) return;
                    const windowId = String(window.internalId);
                    if (target.applied && (!force || target.matched_window !== windowId)) return;

                    if (target.desktop) {{
                        const desktop = findDesktop(target.desktop);
                        if (desktop && !window.onAllDesktops) {{
                            window.desktops = [desktop];
                        }}
                    }}

                    const desired = targetRect(target);
                    if (desired) {{
                        if (window.fullScreen) {{
                            window.fullScreen = false;
                        }}
                        if (window.setMaximize) {{
                            window.setMaximize(false, false);
                        }}
                        const nextRect = Object.assign({{}}, window.frameGeometry);
                        nextRect.x = desired.x;
                        nextRect.y = desired.y;
                        nextRect.width = desired.width;
                        nextRect.height = desired.height;
                        window.frameGeometry = nextRect;
                    }}

                    const wantsFullScreen = !!target.full_screen;
                    const maximizeMode = Number(target.maximize_mode || 0);
                    const maximizeVertically = (maximizeMode & 1) !== 0;
                    const maximizeHorizontally = (maximizeMode & 2) !== 0;

                    if (wantsFullScreen) {{
                        if (window.setMaximize) {{
                            window.setMaximize(false, false);
                        }}
                        if (!window.fullScreen) {{
                            window.fullScreen = true;
                        }}
                    }} else {{
                        if (window.fullScreen) {{
                            window.fullScreen = false;
                        }}
                        if (window.setMaximize) {{
                            window.setMaximize(maximizeVertically, maximizeHorizontally);
                        }}
                    }}

                    target.applied = true;
                    target.matched_window = windowId;
                    target.matched_geometry = rectData(window.frameGeometry);
                }}

                function bestTargetFor(window) {{
                    let best = null;
                    let bestScore = -1;
                    let bestDistance = 1000000;
                    for (const target of RESTORE.targets) {{
                        if (target.applied) continue;
                        const currentScore = score(window, target);
                        if (currentScore <= 0) continue;
                        const currentDistance = geometryDistance(window, target);
                        if (currentScore > bestScore || (currentScore === bestScore && currentDistance < bestDistance)) {{
                            best = target;
                            bestScore = currentScore;
                            bestDistance = currentDistance;
                        }}
                    }}
                    return best;
                }}

                function sweepExistingWindows() {{
                    for (const window of workspace.stackingOrder) {{
                        if (!shouldUseWindow(window)) continue;
                        const target = bestTargetFor(window);
                        if (!target) continue;
                        assignWindow(window, target);
                    }}
                }}

                let finished = false;
                const timers = [];

                function finish(reason) {{
                    if (finished) return;
                    finished = true;
                    for (const timer of timers) {{
                        timer.stop();
                    }}
                    sendResult({{
                        ok: true,
                        reason: reason,
                        applied: RESTORE.targets.filter(function(target) {{ return target.applied; }}).map(function(target) {{
                            return {{
                                target_id: target.id,
                                matched_window: target.matched_window || "",
                                matched_geometry: target.matched_geometry || null
                            }};
                        }}),
                        unmatched: RESTORE.targets.filter(function(target) {{ return !target.applied; }}).map(function(target) {{
                            return target.id;
                        }})
                    }});
                }}

                function maybeFinish() {{
                    const pending = RESTORE.targets.filter(function(target) {{ return !target.applied; }});
                    if (pending.length === 0) {{
                        finish("complete");
                    }}
                }}

                function singleShot(delayMs, callback) {{
                    const timer = new QTimer();
                    timer.singleShot = true;
                    timer.timeout.connect(callback);
                    timer.start(delayMs);
                    timers.push(timer);
                }}

                workspace.windowAdded.connect(function(window) {{
                    if (!shouldUseWindow(window)) return;
                    singleShot(RESTORE.settle_ms, function() {{
                        const target = bestTargetFor(window);
                        if (!target) return;
                        assignWindow(window, target, false);
                        singleShot(250, function() {{
                            assignWindow(window, target, true);
                            maybeFinish();
                        }});
                    }});
                }});

                sweepExistingWindows();
                maybeFinish();
                singleShot(RESTORE.timeout_ms, function() {{ finish("timeout"); }});
            """
        ).rstrip()

    def _place_window_script(self, pid: int, geometry: dict[str, int], caption: str | None, timeout_ms: int) -> str:
        payload = {
            "pid": pid,
            "caption": caption or "",
            "geometry": geometry,
            "timeout_ms": timeout_ms,
        }
        return dedent(
            f"""
                const PLACE = {json.dumps(payload)};
                let finished = false;
                const timers = [];

                function singleShot(delayMs, callback) {{
                    const timer = new QTimer();
                    timer.singleShot = true;
                    timer.timeout.connect(callback);
                    timer.start(delayMs);
                    timers.push(timer);
                }}

                function finish(ok, extra) {{
                    if (finished) return;
                    finished = true;
                    for (const timer of timers) {{
                        timer.stop();
                    }}
                    sendResult(Object.assign({{ ok: ok }}, extra || {{}}));
                }}

                function shouldMatch(window) {{
                    if (!window || !window.managed || window.deleted) return false;
                    if (window.pid !== PLACE.pid) return false;
                    if (PLACE.caption && window.caption !== PLACE.caption) return false;
                    return true;
                }}

                function apply(window) {{
                    if (!shouldMatch(window)) return false;
                    if (window.fullScreen) {{
                        window.fullScreen = false;
                    }}
                    if (window.setMaximize) {{
                        window.setMaximize(false, false);
                    }}
                    const nextRect = Object.assign({{}}, window.frameGeometry);
                    nextRect.x = PLACE.geometry.x;
                    nextRect.y = PLACE.geometry.y;
                    nextRect.width = PLACE.geometry.width;
                    nextRect.height = PLACE.geometry.height;
                    window.frameGeometry = nextRect;
                    finish(true, {{
                        matched_window: String(window.internalId),
                        geometry: {{
                            x: nextRect.x,
                            y: nextRect.y,
                            width: nextRect.width,
                            height: nextRect.height
                        }}
                    }});
                    return true;
                }}

                for (const window of workspace.stackingOrder) {{
                    if (apply(window)) {{
                        break;
                    }}
                }}

                if (!finished) {{
                    workspace.windowAdded.connect(function(window) {{
                        apply(window);
                    }});
                    singleShot(PLACE.timeout_ms, function() {{
                        finish(false, {{ error: "Timed out waiting for panel window" }});
                    }});
                }}
            """
        ).rstrip()


def _script_object_path(script_id: int) -> str:
    return f"/Scripting/Script{int(script_id)}"
