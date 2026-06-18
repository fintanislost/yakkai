#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
from typing import Any

MEDIA_TERMS = ("media", "music", "audio", "song", "album", "artist", "thumbnail")
SCRIPT_SIGNATURES = (
    "mediaPropertiesChanged",
    "mediaThumbnailChanged",
    "mediaTimelineChanged",
    "mediaPlaybackChanged",
    "MediaPlaybackEvent",
    "$mediaThumbnail",
    "$mediaPreviousThumbnail",
    "shared.mi",
    "engine.media",
    "engine.registerAudioBuffers",
)


def _read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8", errors="replace"))


def _walk_strings(value: Any) -> list[str]:
    found: list[str] = []
    if isinstance(value, str):
        found.append(value)
    elif isinstance(value, list):
        for item in value:
            found.extend(_walk_strings(item))
    elif isinstance(value, dict):
        for item in value.values():
            found.extend(_walk_strings(item))
    return found


def _media_property_keys(properties: dict[str, Any]) -> list[str]:
    keys: list[str] = []
    for key, value in properties.items():
        haystack = f"{key} {json.dumps(value, ensure_ascii=False).lower()}".lower()
        if any(term in haystack for term in MEDIA_TERMS):
            keys.append(key)
    return sorted(keys)


def _suggested_properties(
    properties: dict[str, Any],
    media_keys: list[str],
) -> dict[str, Any]:
    suggested: dict[str, Any] = {}
    for key in media_keys:
        prop = properties.get(key, {})
        lower_key = key.lower()
        prop_type = str(prop.get("type", "")).lower() if isinstance(prop, dict) else ""
        value = prop.get("value") if isinstance(prop, dict) else None
        if "hide" in lower_key and "media" in lower_key:
            suggested[key] = False
        elif "media" in lower_key and prop_type == "combo":
            suggested[key] = "1"
        elif "media" in lower_key and prop_type == "bool":
            suggested[key] = True
        elif lower_key in ("music", "audio") and prop_type == "bool":
            suggested[key] = True
        elif lower_key.startswith("music") and prop_type == "combo":
            suggested[key] = "1"
        elif value is not None and lower_key in ("music", "audio"):
            suggested[key] = value
    return suggested


def _scene_text(candidate_dir: Path) -> str:
    pieces: list[str] = []
    for path in (candidate_dir / "scene.json", candidate_dir / "scene" / "scene.json"):
        if path.exists():
            pieces.append(path.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(pieces)


def _source_path(candidate_dir: Path) -> str:
    for path in (
        candidate_dir / "scene" / "scene.json",
        candidate_dir / "scene.json",
        candidate_dir / "scene.pkg",
    ):
        if path.exists():
            return str(path)
    return ""


def inspect_candidate(candidate_dir: Path) -> dict[str, Any]:
    project_path = candidate_dir / "project.json"
    project = _read_json(project_path)
    properties = (project.get("general") or {}).get("properties") or {}
    media_keys = _media_property_keys(properties)
    scene_text = _scene_text(candidate_dir)
    project_text = "\n".join(_walk_strings(project))
    combined_text = f"{project_text}\n{scene_text}"
    signatures = [sig for sig in SCRIPT_SIGNATURES if sig in combined_text]

    has_metadata = any(
        sig in signatures
        for sig in (
            "mediaPropertiesChanged",
            "mediaThumbnailChanged",
            "mediaTimelineChanged",
            "mediaPlaybackChanged",
            "MediaPlaybackEvent",
            "$mediaThumbnail",
            "$mediaPreviousThumbnail",
            "shared.mi",
            "engine.media",
        )
    )
    has_audio = "engine.registerAudioBuffers" in signatures
    if has_metadata:
        classification = "metadata-widget"
    elif has_audio:
        classification = "audio-reactive"
    elif media_keys:
        classification = "property-only-media"
    else:
        classification = "none"

    return {
        "sceneId": candidate_dir.name.split("__", 1)[0],
        "title": project.get("title", ""),
        "type": project.get("type", ""),
        "path": str(candidate_dir),
        "source": _source_path(candidate_dir),
        "classification": classification,
        "mediaPropertyKeys": media_keys,
        "scriptSignatures": signatures,
        "suggestedProperties": _suggested_properties(properties, media_keys),
    }


def inspect_library(root: Path) -> list[dict[str, Any]]:
    rows = []
    for project_path in sorted(root.glob("*/project.json")):
        rows.append(inspect_candidate(project_path.parent))
    return rows


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=Path)
    parser.add_argument("--include-none", action="store_true")
    parser.add_argument("--json", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rows = inspect_library(args.library)
    if not args.include_none:
        rows = [row for row in rows if row["classification"] != "none"]
    if args.json:
        print(json.dumps(rows, indent=2, ensure_ascii=False))
    else:
        for row in rows:
            print(
                f'{row["sceneId"]}\t{row["classification"]}\t'
                f'{",".join(row["scriptSignatures"])}\t{row["title"]}'
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
