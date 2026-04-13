from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from workspace_templates.desktop_entries import (
    _normalize_exec_command,
    desktop_entry_exec,
    desktop_file_candidates,
    derive_launch_command,
    resolve_saved_launch_command,
)


class DesktopEntriesTests(unittest.TestCase):
    def test_derive_launch_command_prefers_desktop_entry(self) -> None:
        with patch("workspace_templates.desktop_entries.desktop_entry_exec", return_value=["steam"]), patch(
            "workspace_templates.desktop_entries.read_cmdline",
            return_value=["steamwebhelper", "--type=renderer"],
        ):
            command, source = derive_launch_command(pid=123, desktop_file_name="steam")

        self.assertEqual(command, ["steam"])
        self.assertEqual(source, "desktop")

    def test_desktop_file_candidates_include_flatpak_paths(self) -> None:
        candidates = desktop_file_candidates("com.spotify.Client")

        self.assertIn(Path.home() / ".local/share/flatpak/exports/share/applications/com.spotify.Client.desktop", candidates)
        self.assertIn(Path("/var/lib/flatpak/exports/share/applications/com.spotify.Client.desktop"), candidates)

    def test_desktop_entry_exec_normalizes_duplicate_leading_binary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            desktop_file = Path(tmp) / "spotify.desktop"
            desktop_file.write_text(
                "[Desktop Entry]\nExec=spotify spotify --enable-features=UseOzonePlatform --ozone-platform=wayland %U\n",
                encoding="utf-8",
            )

            with patch("workspace_templates.desktop_entries.desktop_file_candidates", return_value=[desktop_file]), patch(
                "workspace_templates.desktop_entries._command_exists",
                return_value=True,
            ):
                command = desktop_entry_exec("spotify")

        self.assertEqual(command, ["spotify", "--enable-features=UseOzonePlatform", "--ozone-platform=wayland"])

    def test_normalize_exec_command_keeps_regular_command(self) -> None:
        command = _normalize_exec_command(["steam", "-silent"])

        self.assertEqual(command, ["steam", "-silent"])

    def test_resolve_saved_launch_command_prefers_desktop_entry(self) -> None:
        with patch("workspace_templates.desktop_entries.desktop_entry_exec", return_value=["spotify", "--uri"]):
            command, source = resolve_saved_launch_command(["spotify", "--type=zygote"], "spotify")

        self.assertEqual(command, ["spotify", "--uri"])
        self.assertEqual(source, "desktop")


if __name__ == "__main__":
    unittest.main()
