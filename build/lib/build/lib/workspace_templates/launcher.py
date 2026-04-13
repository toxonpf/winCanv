from __future__ import annotations

import subprocess
from dataclasses import dataclass


@dataclass(slots=True)
class LaunchResult:
    command: list[str]
    ok: bool
    message: str
    pid: int | None = None


def launch_command(command: list[str], cwd: str | None = None) -> LaunchResult:
    if not command:
        return LaunchResult(command=command, ok=False, message="No launch command available")
    try:
        proc = subprocess.Popen(
            command,
            cwd=cwd or None,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        return LaunchResult(command=command, ok=True, message="launched", pid=proc.pid)
    except FileNotFoundError:
        return LaunchResult(command=command, ok=False, message=f"Executable not found: {command[0]}")
    except Exception as exc:  # noqa: BLE001
        return LaunchResult(command=command, ok=False, message=str(exc))
