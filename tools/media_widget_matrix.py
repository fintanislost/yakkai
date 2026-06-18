#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

import media_fixture_payload


CLASS_PRIORITY = {
    "metadata-widget": 0,
    "property-only-media": 1,
    "audio-reactive": 2,
}
TIMEOUT_EXIT_CODE = 124


def load_inventory(path: Path) -> list[dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, list):
        raise ValueError(f"{path} must contain a JSON array")
    return [row for row in data if isinstance(row, dict)]


def select_candidates(
    rows: list[dict[str, Any]],
    *,
    scene_ids: list[str] | None = None,
    limit: int | None = None,
) -> list[dict[str, Any]]:
    wanted = set(scene_ids or [])
    selected: list[dict[str, Any]] = []
    for row in rows:
        classification = str(row.get("classification", ""))
        if classification not in CLASS_PRIORITY:
            continue
        if not row.get("source"):
            continue
        scene_id = str(row.get("sceneId", ""))
        if wanted and scene_id not in wanted:
            continue
        selected.append(row)

    selected.sort(
        key=lambda row: (
            CLASS_PRIORITY[str(row.get("classification", ""))],
            str(row.get("sceneId", "")),
        )
    )
    if limit is not None:
        selected = selected[:limit]
    return selected


def build_media_scene_properties(
    row: dict[str, Any],
    fixture: Path,
    output_dir: Path,
) -> dict[str, Any]:
    extra_properties = dict(row.get("suggestedProperties") or {})
    if str(row.get("sceneId", "")) == "3228578419":
        extra_properties.setdefault("timeofday", "1")
        extra_properties.setdefault("mediaintegration", "1")
        extra_properties.setdefault("hidemediaintegration", False)

    return media_fixture_payload.build_scene_properties(
        fixture,
        output_dir / "album-art",
        extra_properties,
        position=42.0,
        settle_seconds=1.25,
    )


def _safe_slug(row: dict[str, Any]) -> str:
    title = str(row.get("title", "")).strip()
    raw = f"{row.get('sceneId', 'unknown')}-{title}" if title else str(row.get("sceneId", "unknown"))
    slug = "".join(ch.lower() if ch.isalnum() else "-" for ch in raw)
    while "--" in slug:
        slug = slug.replace("--", "-")
    return slug.strip("-") or "unknown"


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def run_command(
    command: list[str],
    log_path: Path,
    *,
    timeout_seconds: float | None = None,
) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log_file:
        log_file.write("$ " + " ".join(command) + "\n\n")
        log_file.flush()
        try:
            completed = subprocess.run(
                command,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=timeout_seconds,
            )
        except subprocess.TimeoutExpired:
            log_file.write(
                f"\nCommand timed out after {timeout_seconds:.1f} seconds\n"
            )
            return TIMEOUT_EXIT_CODE
    return completed.returncode


def _harness_command(
    *,
    harness: Path,
    source: Path,
    assets: Path,
    scene_properties: dict[str, Any],
    output_dir: Path,
    window_size: str,
    capture_delay_ms: int,
    debug: bool,
) -> list[str]:
    command = [
        str(harness),
        "--backend",
        "paper",
        "--source",
        str(source),
        "--assets",
        str(assets),
        "--fill",
        "crop",
        "--window-size",
        window_size,
        "--hide-info-overlay",
        "--capture",
        str(output_dir / "capture.png"),
        "--capture-delay-ms",
        str(capture_delay_ms),
        "--scene-properties-json",
        json.dumps(scene_properties, separators=(",", ":")),
    ]
    if debug:
        command.extend(
            [
                "--debug-effect-captures",
                str(output_dir / "effect-captures"),
                "--debug-effect-capture-delay-ms",
                str(capture_delay_ms),
            ]
        )
    return command


def run_candidate(
    row: dict[str, Any],
    *,
    fixture: Path,
    output_root: Path,
    harness: Path,
    assets: Path,
    window_size: str,
    capture_delay_ms: int,
    timeout_seconds: float | None,
    debug: bool,
) -> dict[str, Any]:
    output_dir = output_root / _safe_slug(row)
    output_dir.mkdir(parents=True, exist_ok=True)
    scene_properties = build_media_scene_properties(row, fixture, output_dir)
    scene_properties_path = output_dir / "scene-properties.json"
    write_json(scene_properties_path, scene_properties)

    command = _harness_command(
        harness=harness,
        source=Path(str(row["source"])),
        assets=assets,
        scene_properties=scene_properties,
        output_dir=output_dir,
        window_size=window_size,
        capture_delay_ms=capture_delay_ms,
        debug=debug,
    )
    exit_code = run_command(
        command,
        output_dir / "harness.log",
        timeout_seconds=timeout_seconds,
    )

    diagnostics: dict[str, str] = {}
    manifest_path = output_dir / "effect-captures" / "manifest.json"
    capture_path = output_dir / "capture.png"
    if debug and exit_code == 0 and manifest_path.exists() and capture_path.exists():
        diagnostics_dir = output_dir / "media-text-diagnostics"
        diagnostics_command = [
            str(Path(__file__).resolve().parent / "media_text_diagnostics.py"),
            "--manifest",
            str(manifest_path),
            "--capture",
            str(capture_path),
            "--output-dir",
            str(diagnostics_dir),
        ]
        diagnostics_exit = run_command(
            diagnostics_command,
            output_dir / "media-text-diagnostics.log",
        )
        if diagnostics_exit == 0:
            diagnostics["markdown"] = str(diagnostics_dir / "media-text-diagnostics.md")
            diagnostics["json"] = str(diagnostics_dir / "media-text-diagnostics.json")
            contact_sheet = diagnostics_dir / "media-text-contact-sheet.png"
            if contact_sheet.exists():
                diagnostics["contactSheet"] = str(contact_sheet)

    result = {
        "sceneId": row.get("sceneId", ""),
        "title": row.get("title", ""),
        "classification": row.get("classification", ""),
        "source": row.get("source", ""),
        "outputDir": str(output_dir),
        "capture": str(capture_path) if capture_path.exists() else "",
        "manifest": str(manifest_path) if manifest_path.exists() else "",
        "sceneProperties": str(scene_properties_path),
        "harnessLog": str(output_dir / "harness.log"),
        "exitCode": exit_code,
        "diagnostics": diagnostics,
    }
    write_json(output_dir / "result.json", result)
    return result


def write_summary(results: list[dict[str, Any]], output_root: Path) -> None:
    write_json(output_root / "summary.json", {"results": results})
    lines = [
        "# Media Widget Matrix",
        "",
        "| scene | class | exit | capture | diagnostics |",
        "| --- | --- | ---: | --- | --- |",
    ]
    for result in results:
        scene = f"{result.get('sceneId', '')} {result.get('title', '')}".strip()
        capture = result.get("capture", "")
        diagnostics = (result.get("diagnostics") or {}).get("markdown", "")
        lines.append(
            f"| `{scene}` | `{result.get('classification', '')}` | "
            f"`{result.get('exitCode', '')}` | `{capture}` | `{diagnostics}` |"
        )
    output_root.mkdir(parents=True, exist_ok=True)
    (output_root / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run fixture-backed synthetic media checks against candidate scene wallpapers."
    )
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument(
        "--fixture",
        type=Path,
        default=Path("native/scene_harness/tests/fixtures/media/instalock.mp3"),
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--harness",
        type=Path,
        default=Path("build/native/scene_harness/yakkai_scene_harness"),
    )
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--scene-id", action="append", default=[])
    parser.add_argument("--limit", type=int)
    parser.add_argument("--window-size", default="1600x900")
    parser.add_argument("--capture-delay-ms", type=int, default=10000)
    parser.add_argument(
        "--timeout-seconds",
        type=float,
        default=90.0,
        help="Per-scene harness timeout. Use 0 to disable.",
    )
    parser.add_argument("--debug", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    rows = load_inventory(args.inventory)
    candidates = select_candidates(rows, scene_ids=args.scene_id, limit=args.limit)
    if not candidates:
        print("No matching media widget candidates", file=sys.stderr)
        return 2

    results = [
        run_candidate(
            row,
            fixture=args.fixture,
            output_root=args.output_dir,
            harness=args.harness,
            assets=args.assets,
            window_size=args.window_size,
            capture_delay_ms=args.capture_delay_ms,
            timeout_seconds=args.timeout_seconds if args.timeout_seconds > 0 else None,
            debug=args.debug,
        )
        for row in candidates
    ]
    write_summary(results, args.output_dir)
    return 1 if any(result["exitCode"] != 0 for result in results) else 0


if __name__ == "__main__":
    raise SystemExit(main())
