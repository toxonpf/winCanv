from __future__ import annotations

import configparser
import os
import re
import shlex
import shutil
from pathlib import Path


FIELD_CODE_RE = re.compile(r"%[fFuUdDnNickvm]")


def read_cmdline(pid: int | None) -> list[str]:
    if not pid:
        return []
    cmdline_path = Path(f"/proc/{pid}/cmdline")
    if not cmdline_path.exists():
        return []
    raw = cmdline_path.read_bytes().rstrip(b"\x00")
    if not raw:
        return []
    return [segment.decode("utf-8", errors="replace") for segment in raw.split(b"\x00") if segment]


def read_cwd(pid: int | None) -> str | None:
    if not pid:
        return None
    path = Path(f"/proc/{pid}/cwd")
    if not path.exists():
        return None
    try:
        return os.readlink(path)
    except OSError:
        return None


def desktop_file_candidates(desktop_file_name: str | None) -> list[Path]:
    if not desktop_file_name:
        return []
    name = desktop_file_name if desktop_file_name.endswith(".desktop") else f"{desktop_file_name}.desktop"
    roots = [
        Path.home() / ".local/share/applications",
        Path.home() / ".local/share/flatpak/exports/share/applications",
        Path("/var/lib/flatpak/exports/share/applications"),
        Path("/var/lib/snapd/desktop/applications"),
        Path("/usr/local/share/applications"),
        Path("/usr/share/applications"),
    ]
    return [root / name for root in roots]


def _normalize_exec_command(command: list[str]) -> list[str]:
    if len(command) >= 2 and command[0] == command[1]:
        return [command[0], *command[2:]]
    return command


def _command_exists(command: list[str]) -> bool:
    if not command:
        return False
    executable = command[0]
    if "/" in executable:
        return Path(executable).exists()
    return shutil.which(executable) is not None


def desktop_entry_exec(desktop_file_name: str | None) -> list[str]:
    for candidate in desktop_file_candidates(desktop_file_name):
        if not candidate.exists():
            continue
        parser = configparser.ConfigParser(interpolation=None, strict=False)
        parser.read(candidate, encoding="utf-8")
        if not parser.has_section("Desktop Entry"):
            continue
        if not parser.has_option("Desktop Entry", "Exec"):
            continue
        exec_line = parser.get("Desktop Entry", "Exec")
        cleaned = FIELD_CODE_RE.sub("", exec_line).strip()
        if cleaned:
            command = _normalize_exec_command(shlex.split(cleaned))
            if _command_exists(command):
                return command
    return []


def derive_launch_command(pid: int | None, desktop_file_name: str | None) -> tuple[list[str], str | None]:
    desktop_exec = desktop_entry_exec(desktop_file_name)
    if desktop_exec:
        return desktop_exec, "desktop"
    live_cmdline = read_cmdline(pid)
    if live_cmdline:
        return live_cmdline, "procfs"
    return [], None


def resolve_saved_launch_command(saved_command: list[str], desktop_file_name: str | None) -> tuple[list[str], str | None]:
    desktop_exec = desktop_entry_exec(desktop_file_name)
    if desktop_exec:
        return desktop_exec, "desktop"
    return list(saved_command or []), "saved"
