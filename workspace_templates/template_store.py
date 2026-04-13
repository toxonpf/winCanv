from __future__ import annotations

import json
import re
from pathlib import Path

from .models import WorkspaceTemplate


def slugify(value: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9._-]+", "-", value.strip()).strip("-").lower()
    return slug or "template"


class TemplateStore:
    def __init__(self, templates_dir: Path) -> None:
        self.templates_dir = templates_dir
        self.templates_dir.mkdir(parents=True, exist_ok=True)

    def path_for_name(self, name: str) -> Path:
        return self.templates_dir / f"{slugify(name)}.json"

    def list_templates(self) -> list[WorkspaceTemplate]:
        items: list[WorkspaceTemplate] = []
        for path in sorted(self.templates_dir.glob("*.json")):
            items.append(self._read_path(path))
        items.sort(key=lambda item: item.created_at, reverse=True)
        return items

    def get(self, name: str) -> WorkspaceTemplate:
        path = self.path_for_name(name)
        if not path.exists():
            raise FileNotFoundError(f"Template '{name}' does not exist")
        return self._read_path(path)

    def save(self, template: WorkspaceTemplate) -> Path:
        path = self.path_for_name(template.name)
        path.write_text(json.dumps(template.to_dict(), indent=2, sort_keys=False) + "\n", encoding="utf-8")
        return path

    def delete(self, name: str) -> None:
        path = self.path_for_name(name)
        if path.exists():
            path.unlink()

    def _read_path(self, path: Path) -> WorkspaceTemplate:
        data = json.loads(path.read_text(encoding="utf-8"))
        return WorkspaceTemplate.from_dict(data)
