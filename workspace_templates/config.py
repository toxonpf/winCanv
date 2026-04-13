from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


APP_ID = "io.github.toxonpf.workspacetemplates"
APP_NAME = "Workspace Templates"
APP_ORGANIZATION = "toxonpf"
APP_DOMAIN = "github.io"


def _xdg_path(env_name: str, fallback: str) -> Path:
    value = os.environ.get(env_name)
    if value:
        return Path(value).expanduser()
    return Path.home() / fallback


@dataclass(slots=True)
class AppPaths:
    base_data_dir: Path
    base_state_dir: Path
    base_runtime_dir: Path

    @property
    def data_dir(self) -> Path:
        return self.base_data_dir / "workspace-templates"

    @property
    def state_dir(self) -> Path:
        return self.base_state_dir / "workspace-templates"

    @property
    def runtime_dir(self) -> Path:
        return self.base_runtime_dir / "workspace-templates"

    @property
    def templates_dir(self) -> Path:
        return self.data_dir / "templates"

    @property
    def logs_dir(self) -> Path:
        return self.state_dir / "logs"

    def ensure(self) -> "AppPaths":
        for path in (
            self.data_dir,
            self.state_dir,
            self.runtime_dir,
            self.templates_dir,
            self.logs_dir,
        ):
            path.mkdir(parents=True, exist_ok=True)
        return self


def default_paths() -> AppPaths:
    runtime_default = Path("/tmp") if "XDG_RUNTIME_DIR" not in os.environ else Path(os.environ["XDG_RUNTIME_DIR"])
    return AppPaths(
        base_data_dir=_xdg_path("XDG_DATA_HOME", ".local/share"),
        base_state_dir=_xdg_path("XDG_STATE_HOME", ".local/state"),
        base_runtime_dir=runtime_default,
    ).ensure()
