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


def resolve_source_path(project_path: Path, file_name: object, project_type: str) -> Path | None:
    candidates: list[Path] = []

    if isinstance(file_name, str) and file_name:
        candidates.append(project_path.parent / file_name)

    if project_type == "scene":
        candidates.extend(
            [
                project_path.parent / "scene.pkg",
                project_path.parent / "scene.json",
            ]
        )

    seen: set[Path] = set()
    for candidate in candidates:
        resolved = candidate.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)

        if resolved.is_file():
            return resolved

    return None


def load_project(project_path: Path, kind: str, expected_type: str) -> dict | None:
    try:
        data = json.loads(project_path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return None

    project_type = str(data.get("type", "")).lower()
    if project_type != expected_type:
        return None

    source_path = resolve_source_path(project_path, data.get("file"), project_type)
    if source_path is None:
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

    user_properties = {}
    general = data.get("general")
    if isinstance(general, dict):
        properties = general.get("properties")
        if isinstance(properties, dict):
            user_properties = properties

    return {
        "title": str(data.get("title") or project_path.parent.name),
        "projectPath": str(project_path.resolve()),
        "folderPath": str(project_path.parent.resolve()),
        "sourcePath": str(source_path),
        "sourceKind": source_path.name,
        "previewPath": preview_path,
        "kind": kind,
        "type": project_type,
        "workshopId": workshop_id,
        "propertiesJson": json.dumps(user_properties, ensure_ascii=False, sort_keys=True),
    }


def scan_library(library_root: Path, expected_type: str) -> dict:
    payload = {
        "library": str(library_root.resolve()),
        "type": expected_type,
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
            item = load_project(project_path, kind, expected_type)
            if item is not None:
                items.append(item)

    items.sort(key=lambda item: (item["title"].casefold(), item["projectPath"]))
    payload["items"] = items
    return payload


def main(argv: list[str]) -> int:
    if len(argv) not in (2, 3):
        print(
            json.dumps(
                {
                    "library": "",
                    "type": "",
                    "items": [],
                    "errors": ["Usage: we_video_scan.py <steam-library-path> [video|web|scene]"],
                }
            )
        )
        return 1

    expected_type = argv[2].strip().lower() if len(argv) == 3 else "video"
    if expected_type not in {"video", "web", "scene"}:
        print(
            json.dumps(
                {
                    "library": "",
                    "type": expected_type,
                    "items": [],
                    "errors": ["Supported project types are: video, web, scene"],
                }
            )
        )
        return 1

    payload = scan_library(Path(argv[1]).expanduser(), expected_type)
    print(json.dumps(payload, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
