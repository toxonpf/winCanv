from __future__ import annotations

import json
import os
import threading
from dataclasses import dataclass
from typing import Any
from uuid import uuid4

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib


DBUS_OBJECT_PATH = "/io/github/toxonpf/WorkspaceTemplates"
DBUS_INTERFACE = "io.github.toxonpf.WorkspaceTemplates"


@dataclass(slots=True)
class PendingResult:
    payload: dict[str, Any] | None = None


class BridgeObject(dbus.service.Object):
    def __init__(self, bus_name: dbus.service.BusName, controller: "ResultBridge") -> None:
        self._controller = controller
        super().__init__(bus_name, DBUS_OBJECT_PATH)

    @dbus.service.method(DBUS_INTERFACE, in_signature="ss", out_signature="b")
    def ReportResult(self, request_id: str, payload_json: str) -> bool:  # noqa: N802
        self._controller.report_result(request_id, payload_json)
        return True


class ResultBridge:
    def __init__(self) -> None:
        DBusGMainLoop(set_as_default=True)
        self.bus = dbus.SessionBus()
        self.service_name = f"{DBUS_INTERFACE}.Instance{os.getpid()}{uuid4().hex}"
        self._bus_name = dbus.service.BusName(self.service_name, bus=self.bus, allow_replacement=False)
        self._object = BridgeObject(self._bus_name, self)
        self._results: dict[str, PendingResult] = {}
        self._condition = threading.Condition()
        self._loop = GLib.MainLoop()
        self._thread = threading.Thread(target=self._loop.run, name="workspace-templates-dbus", daemon=True)
        self._thread.start()

    def report_result(self, request_id: str, payload_json: str) -> None:
        payload = json.loads(payload_json)
        with self._condition:
            self._results[request_id] = PendingResult(payload=payload)
            self._condition.notify_all()

    def wait_for(self, request_id: str, timeout: float) -> dict[str, Any]:
        with self._condition:
            matched = self._condition.wait_for(lambda: request_id in self._results, timeout=timeout)
            if not matched:
                raise TimeoutError(f"Timed out waiting for KWin response for request {request_id}")
            return self._results.pop(request_id).payload or {}

    def stop(self) -> None:
        if self._loop.is_running():
            self._loop.quit()
        self._thread.join(timeout=1.0)
