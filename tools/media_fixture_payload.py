#!/usr/bin/env python3
import argparse
import json
import subprocess
from pathlib import Path
from typing import Any


def _run_json(command: list[str]) -> dict[str, Any]:
    completed = subprocess.run(
        command,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return json.loads(completed.stdout)


def _tag(tags: dict[str, Any], name: str) -> str:
    lower_name = name.lower()
    for key, value in tags.items():
        if key.lower() == lower_name and isinstance(value, str):
            return value
    return ""


def read_metadata(media_path: Path) -> dict[str, Any]:
    if not media_path.exists():
        raise FileNotFoundError(media_path)

    info = _run_json(
        [
            "ffprobe",
            "-v",
            "error",
            "-show_entries",
            "format=duration:format_tags",
            "-of",
            "json",
            str(media_path),
        ]
    )
    media_format = info.get("format", {})
    tags = media_format.get("tags", {})
    duration = float(media_format.get("duration", 0.0) or 0.0)
    return {
        "title": _tag(tags, "title") or media_path.stem,
        "artist": _tag(tags, "artist"),
        "album": _tag(tags, "album"),
        "duration": duration,
    }


def extract_album_art(media_path: Path, output_dir: Path) -> Path | None:
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / f"{media_path.stem}-album-art.png"
    command = [
        "ffmpeg",
        "-y",
        "-loglevel",
        "error",
        "-i",
        str(media_path),
        "-an",
        "-map",
        "0:v:0",
        "-frames:v",
        "1",
        str(output_path),
    ]
    try:
        subprocess.run(
            command,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except subprocess.CalledProcessError:
        return None
    return output_path if output_path.exists() else None


def _scene_property_value(value: Any) -> dict[str, Any]:
    return {"value": value}


def build_scene_properties(
    media_path: Path,
    output_dir: Path,
    extra_properties: dict[str, Any] | None = None,
    *,
    available: bool = True,
    playing: bool = True,
    position: float = 42.0,
    position_ratio: float | None = None,
    clock_time: str | None = None,
    settle_seconds: float | None = None,
) -> dict[str, Any]:
    metadata = read_metadata(media_path)
    if position_ratio is not None:
        if position_ratio < 0.0 or position_ratio > 1.0:
            raise ValueError("position_ratio must be between 0.0 and 1.0")
        position = metadata["duration"] * position_ratio
    album_art = extract_album_art(media_path, output_dir)
    media_payload: dict[str, Any] = {
        "available": available,
        "playing": playing,
        "title": metadata["title"],
        "artist": metadata["artist"],
        "album": metadata["album"],
        "duration": metadata["duration"],
        "position": position,
    }
    if album_art is not None:
        media_payload["albumArtPath"] = str(album_art.resolve())
    if clock_time is not None:
        media_payload["clockTime"] = clock_time
    if settle_seconds is not None:
        media_payload["settleSeconds"] = settle_seconds

    scene_properties = {
        key: _scene_property_value(value)
        for key, value in (extra_properties or {}).items()
    }
    scene_properties["__yakkaiMedia"] = media_payload
    return scene_properties


def _parse_value(raw: str) -> Any:
    lowered = raw.lower()
    if lowered == "true":
        return True
    if lowered == "false":
        return False
    return raw


def parse_property(raw: str) -> tuple[str, Any]:
    if "=" not in raw:
        raise argparse.ArgumentTypeError("properties must be NAME=VALUE")
    name, value = raw.split("=", 1)
    if not name:
        raise argparse.ArgumentTypeError("property name must not be empty")
    return name, _parse_value(value)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Build a Yakkai scene-properties JSON object from a media fixture."
        )
    )
    parser.add_argument("media", type=Path, help="Media file with tags/artwork")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path.cwd() / "tmp" / "yakkai-media-fixture-payload",
        help="Directory for extracted album art",
    )
    parser.add_argument(
        "--property",
        dest="properties",
        action="append",
        default=[],
        type=parse_property,
        metavar="NAME=VALUE",
        help="Add a normal Wallpaper Engine scene property",
    )
    position_group = parser.add_mutually_exclusive_group()
    position_group.add_argument(
        "--position",
        type=float,
        default=42.0,
        help="Synthetic media playback position in seconds",
    )
    position_group.add_argument(
        "--position-ratio",
        type=float,
        help="Synthetic playback position as a 0.0-1.0 fraction of the media duration",
    )
    playback_group = parser.add_mutually_exclusive_group()
    playback_group.add_argument(
        "--paused",
        action="store_true",
        help="Build available media metadata with PLAYBACK_PAUSED instead of PLAYBACK_PLAYING",
    )
    playback_group.add_argument(
        "--stopped",
        action="store_true",
        help="Build unavailable media state with PLAYBACK_STOPPED",
    )
    parser.add_argument(
        "--clock-time",
        help="Freeze SceneScript new Date() as HH:MM or HH:MM:SS for clock-widget references",
    )
    parser.add_argument(
        "--settle-seconds",
        type=float,
        help="Synthetic media settle time for SceneScript fade/layout checks",
    )
    parser.add_argument(
        "--pretty",
        action="store_true",
        help="Pretty-print JSON instead of one-line harness input",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    scene_properties = build_scene_properties(
        args.media,
        args.output_dir,
        dict(args.properties),
        available=not args.stopped,
        playing=not args.paused and not args.stopped,
        position=args.position,
        position_ratio=args.position_ratio,
        clock_time=args.clock_time,
        settle_seconds=args.settle_seconds,
    )
    if args.pretty:
        print(json.dumps(scene_properties, indent=2, sort_keys=True))
    else:
        print(json.dumps(scene_properties, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
