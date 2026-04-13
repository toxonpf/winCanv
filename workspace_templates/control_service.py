from __future__ import annotations

import threading

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib
from PyQt6.QtCore import QObject, pyqtSignal

from .config import APP_ID


CONTROL_OBJECT_PATH = "/io/github/toxonpf/workspacetemplates"
CONTROL_INTERFACE = APP_ID


class _ControlObject(dbus.service.Object):
    def __init__(self, bus_name: dbus.service.BusName, controller: "AppControlService") -> None:
        self._controller = controller
        super().__init__(bus_name, CONTROL_OBJECT_PATH)

    @dbus.service.method(CONTROL_INTERFACE, in_signature="", out_signature="b")
    def TogglePanel(self) -> bool:  # noqa: N802
        self._controller.command_received.emit("toggle")
        return True

    @dbus.service.method(CONTROL_INTERFACE, in_signature="", out_signature="b")
    def ShowPanel(self) -> bool:  # noqa: N802
        self._controller.command_received.emit("show")
        return True

    @dbus.service.method(CONTROL_INTERFACE, in_signature="", out_signature="b")
    def HidePanel(self) -> bool:  # noqa: N802
        self._controller.command_received.emit("hide")
        return True

    @dbus.service.method(CONTROL_INTERFACE, in_signature="", out_signature="b")
    def Quit(self) -> bool:  # noqa: N802
        self._controller.command_received.emit("quit")
        return True


class AppControlService(QObject):
    command_received = pyqtSignal(str)

    def __init__(self) -> None:
        super().__init__()
        self._loop: GLib.MainLoop | None = None
        self._thread: threading.Thread | None = None
        self._ready = threading.Event()
        self._startup_error: BaseException | None = None
        self._thread = threading.Thread(target=self._run, name="workspace-templates-control-dbus", daemon=True)
        self._thread.start()
        self._ready.wait(timeout=2.0)
        if self._startup_error is not None:
            raise RuntimeError(f"Failed to start control service: {self._startup_error}") from self._startup_error

    def _run(self) -> None:
        try:
            DBusGMainLoop(set_as_default=True)
            bus = dbus.SessionBus()
            bus_name = dbus.service.BusName(
                APP_ID,
                bus=bus,
                allow_replacement=False,
                replace_existing=False,
                do_not_queue=True,
            )
            self._object = _ControlObject(bus_name, self)
            self._loop = GLib.MainLoop()
            self._ready.set()
            self._loop.run()
        except BaseException as exc:  # noqa: BLE001
            self._startup_error = exc
            self._ready.set()

    def stop(self) -> None:
        if self._loop is not None and self._loop.is_running():
            self._loop.quit()
        if self._thread is not None:
            self._thread.join(timeout=1.0)
