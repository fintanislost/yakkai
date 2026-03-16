#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
from pathlib import Path


def candidate_roots(library_root: Path) -> list[tuple[str, Path]]:
    return [
        ("workshop", library_root / "steamapps" / "workshop" / "content" / "431960"),
        ("defaultprojects", library_root / "steamapps" / "common" / "wallpaper_engine" / "projects" / "defaultprojects"),
        ("myprojects", library_root / "steamapps" / "common" / "wallpaper_engine" / "projects" / "myprojects"),
    ]


def load_project(project_path: Path, kind: str) -> dict | None:
    try:
        data = json.loads(project_path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return None

    if str(data.get("type", "")).lower() != "video":
        return None

    file_name = data.get("file")
    if not isinstance(file_name, str) or not file_name:
        return None

    source_path = (project_path.parent / file_name).resolve()
    if not source_path.is_file():
        return None

    preview_name = data.get("preview")
    preview_path = ""
    if isinstance(preview_name, str) and preview_name:
        candidate_preview = (project_path.parent / preview_name).resolve()
        if candidate_preview.exists():
            preview_path = str(candidate_preview)

    workshop_id = ""
    if kind == "workshop" and project_path.parent.name.isdigit():
        workshop_id = project_path.parent.name

    return {
        "title": str(data.get("title") or project_path.parent.name),
        "projectPath": str(project_path.resolve()),
        "folderPath": str(project_path.parent.resolve()),
        "sourcePath": str(source_path),
        "previewPath": preview_path,
        "kind": kind,
        "workshopId": workshop_id,
    }


def scan_library(library_root: Path) -> dict:
    payload = {
        "library": str(library_root.resolve()),
        "items": [],
        "errors": [],
    }

    if not library_root.exists():
        payload["errors"].append("Steam library path does not exist.")
        return payload

    if not library_root.is_dir():
        payload["errors"].append("Steam library path is not a directory.")
        return payload

    items: list[dict] = []

    for kind, root in candidate_roots(library_root):
        if not root.is_dir():
            continue

        for project_path in root.glob("*/project.json"):
            item = load_project(project_path, kind)
            if item is not None:
                items.append(item)

    items.sort(key=lambda item: (item["title"].casefold(), item["projectPath"]))
    payload["items"] = items
    return payload


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(
            json.dumps(
                {
                    "library": "",
                    "items": [],
                    "errors": ["Usage: we_video_scan.py <steam-library-path>"],
                }
            )
        )
        return 1

    payload = scan_library(Path(argv[1]).expanduser())
    print(json.dumps(payload, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
