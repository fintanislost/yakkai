#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


RESULT_ORDER = {"pass": 0, "review": 1, "skip": 2, "fail": 3}
COVERAGE_STATUS_ORDER = {
    "missing": 0,
    "requiresHarness": 1,
    "candidate": 2,
    "active": 3,
}


def worst_status(left: str, right: str) -> str:
    if left not in RESULT_ORDER:
        raise ValueError(f"unknown status: {left}")
    if right not in RESULT_ORDER:
        raise ValueError(f"unknown status: {right}")
    if RESULT_ORDER[left] >= RESULT_ORDER[right]:
        return left
    return right


@dataclass
class RunSummary:
    counts: dict[str, int] = field(default_factory=lambda: {"pass": 0, "review": 0, "skip": 0, "fail": 0})
    required_skips: int = 0

    def add(self, status: str, required: bool = False) -> None:
        if status not in self.counts:
            raise ValueError(f"unknown status: {status}")
        self.counts[status] += 1
        if status == "skip" and required:
            self.required_skips += 1

    def exit_code(self, *, strict: bool, require_assets: bool) -> int:
        if self.counts["fail"] > 0:
            return 1
        if strict and self.counts["review"] > 0:
            return 1
        if require_assets and self.required_skips > 0:
            return 1
        return 0


@dataclass
class FrameResult:
    name: str
    status: str
    actual: str
    baseline: str | None
    diff: str | None
    metrics: dict[str, Any]


@dataclass
class SceneResult:
    id: str
    name: str
    status: str
    required: bool
    log: str | None
    frames: list[FrameResult] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class ImageMagickCommands:
    identify: list[str]
    convert: list[str]
    compare: list[str]

    @staticmethod
    def from_path(found: dict[str, str]) -> "ImageMagickCommands":
        if "magick" in found:
            magick = found["magick"]
            return ImageMagickCommands(
                identify=[magick, "identify"],
                convert=[magick],
                compare=[magick, "compare"],
            )
        missing = [name for name in ("identify", "convert", "compare") if name not in found]
        if missing:
            raise RuntimeError("missing ImageMagick commands: " + ", ".join(missing))
        return ImageMagickCommands(
            identify=[found["identify"]],
            convert=[found["convert"]],
            compare=[found["compare"]],
        )


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def find_commands(names: list[str]) -> dict[str, str]:
    return {name: path for name in names if (path := shutil.which(name))}


def require_imagemagick() -> ImageMagickCommands:
    found = find_commands(["magick", "identify", "convert", "compare"])
    try:
        return ImageMagickCommands.from_path(found)
    except RuntimeError as exc:
        raise RuntimeError(
            f"{exc}. Install ImageMagick; the render visual gate requires PNG metric comparison."
        ) from exc


def find_ffmpeg() -> str | None:
    return shutil.which("ffmpeg")


def clear_shader_cache(cache_root: Path) -> list[Path]:
    removed: list[Path] = []
    if not cache_root.exists():
        return removed
    for path in cache_root.glob("*/spvs01"):
        if path.is_dir():
            shutil.rmtree(path)
            removed.append(path)
    return removed


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def load_coverage_matrix(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def coverage_status_meets(actual: str, required: str) -> bool:
    if actual not in COVERAGE_STATUS_ORDER:
        raise ValueError(f"unknown coverage status: {actual}")
    if required not in COVERAGE_STATUS_ORDER:
        raise ValueError(f"unknown required coverage status: {required}")
    return COVERAGE_STATUS_ORDER[actual] >= COVERAGE_STATUS_ORDER[required]


def coverage_bucket_summaries(matrix: dict[str, Any]) -> list[dict[str, Any]]:
    summaries: list[dict[str, Any]] = []
    for bucket in matrix.get("buckets", []):
        coverage = bucket.get("coverage", [])
        statuses = [entry.get("status", "missing") for entry in coverage]
        known_statuses = [status for status in statuses if status in COVERAGE_STATUS_ORDER]
        best_status = "missing"
        if known_statuses:
            best_status = max(known_statuses, key=lambda status: COVERAGE_STATUS_ORDER[status])
        minimum = str(bucket.get("minimumStatus", "candidate"))
        satisfied = minimum in COVERAGE_STATUS_ORDER and coverage_status_meets(best_status, minimum)
        summaries.append({
            "id": bucket.get("id", ""),
            "name": bucket.get("name", ""),
            "minimumStatus": minimum,
            "bestStatus": best_status,
            "satisfied": satisfied,
            "sceneIds": [str(entry.get("sceneId", "")) for entry in coverage if entry.get("sceneId")],
        })
    return summaries


def validate_coverage_matrix(matrix: dict[str, Any], manifest: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    matrix_scene_ids: set[str] = set()
    bucket_ids: set[str] = set()

    for bucket in matrix.get("buckets", []):
        bucket_id = str(bucket.get("id", ""))
        if not bucket_id:
            errors.append("coverage bucket is missing id")
            continue
        if bucket_id in bucket_ids:
            errors.append(f"duplicate coverage bucket id {bucket_id}")
        bucket_ids.add(bucket_id)

        minimum = str(bucket.get("minimumStatus", "candidate"))
        if minimum not in COVERAGE_STATUS_ORDER:
            errors.append(f"bucket {bucket_id} has unknown minimumStatus {minimum}")

        for entry in bucket.get("coverage", []):
            scene_id = str(entry.get("sceneId", ""))
            status = str(entry.get("status", ""))
            if scene_id:
                matrix_scene_ids.add(scene_id)
            else:
                errors.append(f"bucket {bucket_id} has coverage entry without sceneId")
            if status not in COVERAGE_STATUS_ORDER:
                errors.append(f"bucket {bucket_id} scene {scene_id} has unknown status {status}")

    for scene in manifest.get("scenes", []):
        scene_id = str(scene.get("id", ""))
        if scene_id and scene_id not in matrix_scene_ids:
            errors.append(f"active scene {scene_id} is missing from coverage matrix")

    for summary in coverage_bucket_summaries(matrix):
        if not summary["satisfied"]:
            errors.append(
                f"bucket {summary['id']} requires {summary['minimumStatus']} coverage but best status is {summary['bestStatus']}"
            )

    return errors


def format_coverage_markdown(summaries: list[dict[str, Any]]) -> str:
    lines = [
        "# Render Coverage Matrix",
        "",
        "| Bucket | Name | Required | Best | Satisfied | Scenes |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for summary in summaries:
        satisfied = "yes" if summary["satisfied"] else "no"
        scenes = ", ".join(summary["sceneIds"])
        lines.append(
            f"| {summary['id']} | {summary['name']} | {summary['minimumStatus']} | {summary['bestStatus']} | {satisfied} | {scenes} |"
        )
    return "\n".join(lines) + "\n"


def format_coverage_json(summaries: list[dict[str, Any]]) -> str:
    return json.dumps({"coverage": summaries}, indent=2, sort_keys=True) + "\n"


def expand_manifest_path(value: str, paths: dict[str, str], root: Path, env: dict[str, str] | None = None) -> Path:
    env = env or os.environ
    expanded = value.replace("${HOME}", env.get("HOME", ""))
    seen = set()
    expansions = 0
    max_expansions = max(32, len(paths) * 8)
    changed = True
    while changed:
        if expanded in seen:
            raise ValueError(f"cyclic manifest path token expansion: {value}")
        seen.add(expanded)
        changed = False
        for key, replacement in paths.items():
            token = "${" + key + "}"
            if token in expanded:
                expansions += 1
                if expansions > max_expansions:
                    raise ValueError(f"manifest path token expansion exceeded limit: {value}")
                expanded = expanded.replace(token, replacement.replace("${HOME}", env.get("HOME", "")))
                changed = True
    candidate = Path(os.path.expandvars(os.path.expanduser(expanded)))
    if not candidate.is_absolute():
        candidate = root / candidate
    return candidate


def select_scenes(manifest: dict[str, Any], suite: str) -> list[dict[str, Any]]:
    return [scene for scene in manifest.get("scenes", []) if suite in scene.get("gates", [])]


def merged_thresholds(manifest: dict[str, Any], scene: dict[str, Any]) -> dict[str, float]:
    thresholds = dict(manifest.get("defaults", {}).get("thresholds", {}))
    thresholds.update(scene.get("thresholds", {}))
    return thresholds


def merged_capture_size(manifest: dict[str, Any], scene: dict[str, Any]) -> dict[str, int]:
    size = dict(manifest.get("defaults", {}).get("captureSize", {}))
    size.update(scene.get("captureSize", {}))
    try:
        width = int(size["width"])
        height = int(size["height"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError("manifest captureSize must include integer width and height") from exc
    if width <= 0 or height <= 0:
        raise ValueError("manifest captureSize width and height must be positive")
    return {"width": width, "height": height}


def classify_rmse(value: float, thresholds: dict[str, float]) -> str:
    if value >= thresholds["rmseFail"]:
        return "fail"
    if value >= thresholds["rmseReview"]:
        return "review"
    return "pass"


def parse_rmse(output: str) -> float:
    start = output.rfind("(")
    end = output.rfind(")")
    if start == -1 or end == -1 or end <= start:
        raise ValueError(f"could not parse RMSE output: {output!r}")
    return float(output[start + 1:end])


def classify_motion(values: list[float], expected_motion: bool, thresholds: dict[str, float]) -> str:
    if not values:
        return "pass"
    largest = max(values)
    if expected_motion:
        return "pass" if largest >= thresholds["minMotionRmse"] else "fail"
    return "fail" if largest > thresholds["maxStaticMotionRmse"] else "pass"


def read_text_command(command: list[str]) -> str:
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
    if completed.returncode != 0:
        raise RuntimeError((completed.stderr or completed.stdout).strip())
    return completed.stdout.strip()


def image_dimensions(commands: ImageMagickCommands, image: Path) -> str:
    return read_text_command(commands.identify + ["-format", "%wx%h", str(image)])


def gray_stddev(commands: ImageMagickCommands, image: Path) -> float:
    return float(
        read_text_command(commands.convert + [str(image), "-colorspace", "Gray", "-format", "%[fx:standard_deviation]", "info:"])
    )


def unique_colors(commands: ImageMagickCommands, image: Path) -> int:
    return int(read_text_command(commands.convert + [str(image), "-resize", "100x100!", "-unique-colors", "-format", "%k", "info:"]))


def compare_rmse(commands: ImageMagickCommands, expected: Path, actual: Path, diff: Path | None = None) -> float:
    command = commands.compare + ["-metric", "RMSE", str(expected), str(actual)]
    if diff is None:
        command.append("null:")
    else:
        diff.parent.mkdir(parents=True, exist_ok=True)
        command.append(str(diff))
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
    output = (completed.stderr or completed.stdout).strip()
    if completed.returncode not in (0, 1):
        raise RuntimeError(output)
    return parse_rmse(output)


def scan_log_failures(log_path: Path | None) -> list[str]:
    if log_path is None or not log_path.exists():
        return []
    text = log_path.read_text(encoding="utf-8", errors="replace")
    patterns = [
        "shader compile failed",
        "material faild",
        "failed to load",
        "capture image is null",
        "objectCreationFailed",
    ]
    return [pattern for pattern in patterns if pattern in text]


def ensure_relative_to(root: Path, path: Path) -> Path:
    resolved_root = root.resolve()
    resolved_path = path.resolve()
    try:
        return resolved_path.relative_to(resolved_root)
    except ValueError as exc:
        raise ValueError(f"{resolved_path} is outside {resolved_root}") from exc


def timestamped_artifact_dir(base: Path, now: datetime | None = None) -> Path:
    stamp = (now or datetime.now(timezone.utc)).strftime("%Y%m%dT%H%M%S%fZ")
    for attempt in range(100):
        name = stamp if attempt == 0 else f"{stamp}-{attempt}"
        path = base / name
        try:
            path.mkdir(parents=True, exist_ok=False)
            return path
        except FileExistsError:
            continue
    raise FileExistsError(f"could not create unique artifact directory under {base}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Yakkai smoke-test render regression runner")
    parser.add_argument("--manifest", default="smoke-tests/scenes.json")
    parser.add_argument("--suite", choices=["quick", "deep", "release"], default="quick")
    parser.add_argument("--artifacts", default="/tmp/yakkai-smoke")
    parser.add_argument("--assets")
    parser.add_argument("--workshop")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--require-assets", action="store_true")
    parser.add_argument("--keep-shader-cache", action="store_true")
    parser.add_argument("--write-candidates", action="store_true")
    parser.add_argument("--promote")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--coverage", action="store_true")
    parser.add_argument("--coverage-matrix", default="smoke-tests/coverage-matrix.json")
    parser.add_argument("--coverage-format", choices=["markdown", "json"], default="markdown")
    return parser


def build_harness_base_command(
    harness: Path,
    scene: dict[str, Any],
    source: Path,
    assets: Path,
    capture_size: dict[str, int] | None = None,
) -> list[str]:
    command = [
        str(harness),
        "--backend",
        scene.get("backend", "paper"),
        "--source",
        str(source),
        "--assets",
        str(assets),
        "--fill",
        scene.get("fill", "crop"),
        "--hide-info-overlay",
    ]
    if capture_size is not None:
        command += ["--window-size", f"{capture_size['width']}x{capture_size['height']}"]
    return command


def expected_still_paths(stills_dir: Path, captures: list[dict[str, Any]]) -> list[Path]:
    return [
        stills_dir / f"frame-{int(capture['timeMs']):08d}ms.png"
        for capture in captures
    ]


def expected_sequence_paths(sequence_dir: Path, sequence: dict[str, Any]) -> list[Path]:
    return [
        sequence_dir / f"frame-{index:04d}.png"
        for index in range(int(sequence["frames"]))
    ]


def run_command(command: list[str], log_path: Path, timeout_seconds: float) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8", errors="replace") as log:
        try:
            completed = subprocess.run(
                command,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=timeout_seconds,
                check=False,
            )
        except subprocess.TimeoutExpired:
            log.write(f"\nTIMEOUT after {timeout_seconds}s: {shlex.join(command)}\n")
            return 124
        except FileNotFoundError:
            log.write(f"COMMAND NOT FOUND: {shlex.join(command)}\n")
            return 127
    return completed.returncode


def run_scene_captures(
    *,
    manifest: dict[str, Any],
    scene: dict[str, Any],
    root: Path,
    run_dir: Path,
    assets_override: str | None,
    workshop_override: str | None,
) -> tuple[str, Path | None, list[Path], list[str]]:
    paths = dict(manifest["paths"])
    if assets_override:
        paths["assets"] = assets_override
    if workshop_override:
        paths["workshop"] = workshop_override

    harness = expand_manifest_path(paths["harness"], paths, root)
    assets = expand_manifest_path(paths["assets"], paths, root)
    source = expand_manifest_path(scene["source"], paths, root)
    scene_dir = run_dir / scene["id"]
    capture_dir = scene_dir / "captures"
    log_path = scene_dir / "harness.log"
    notes: list[str] = []

    if not harness.exists():
        return "fail", None, [], [f"harness not found: {harness}"]
    if not source.exists():
        return "skip", None, [], [f"scene not installed: {source}"]
    if not assets.exists():
        return "fail", None, [], [f"assets directory not found: {assets}"]

    capture_size = merged_capture_size(manifest, scene)
    captures = scene.get("captures", [])
    times = [str(capture["timeMs"]) for capture in captures]
    actuals: list[Path] = []
    if times:
        command = build_harness_base_command(harness, scene, source, assets, capture_size)
        command += ["--capture-dir", str(capture_dir / "stills"), "--capture-times-ms", ",".join(times)]
        timeout = max(int(value) for value in times) // 1000 + 30
        code = run_command(command, log_path, timeout)
        if code != 0:
            return "fail", log_path, [], [f"harness still capture exited {code}"]

    stills_dir = capture_dir / "stills"
    expected_stills = expected_still_paths(stills_dir, captures)
    actuals.extend(path for path in expected_stills if path.exists())
    missing_stills = [path.name for path in expected_stills if not path.exists()]
    if missing_stills:
        return "fail", log_path, actuals, [f"missing still captures: {', '.join(missing_stills)}"]

    for sequence in scene.get("sequences", []):
        sequence_dir = capture_dir / sequence["name"]
        sequence_log_path = scene_dir / f"{sequence['name']}.log"
        sequence_arg = f"{sequence['startMs']}:{sequence['frames']}:{sequence['intervalMs']}"
        command = build_harness_base_command(harness, scene, source, assets, capture_size)
        command += ["--capture-dir", str(sequence_dir), "--capture-sequence", sequence_arg]
        timeout = (sequence["startMs"] + sequence["frames"] * sequence["intervalMs"]) // 1000 + 30
        code = run_command(command, sequence_log_path, timeout)
        expected_sequence = expected_sequence_paths(sequence_dir, sequence)
        sequence_actuals = [path for path in expected_sequence if path.exists()]
        if code != 0:
            actuals.extend(sequence_actuals)
            return "fail", sequence_log_path, actuals, [f"harness sequence {sequence['name']} exited {code}"]
        missing_sequence = [path.name for path in expected_sequence if not path.exists()]
        if missing_sequence:
            actuals.extend(sequence_actuals)
            return "fail", sequence_log_path, actuals, [
                f"missing sequence {sequence['name']} frames: {', '.join(missing_sequence)}"
            ]
        actuals.extend(sequence_actuals)

    return "pass", log_path, actuals, notes


def baseline_for_actual(scene: dict[str, Any], actual: Path, baseline_root: Path) -> Path | None:
    parent = actual.parent
    if parent.name == "stills":
        for capture in scene.get("captures", []):
            expected_name = f"frame-{capture['timeMs']:08d}ms.png"
            if actual.name == expected_name:
                return baseline_root / capture["baseline"]
        return None

    for sequence in scene.get("sequences", []):
        if parent.name == sequence["name"]:
            return baseline_root / sequence["baselineDir"] / actual.name
    return None


def sequence_frame_index(path: Path) -> int | None:
    stem = path.stem
    if not stem.startswith("frame-"):
        return None
    try:
        return int(stem.removeprefix("frame-"))
    except ValueError:
        return None


def baseline_candidates_for_actual(scene: dict[str, Any], actual: Path, baseline_root: Path) -> list[Path]:
    parent = actual.parent
    baseline = baseline_for_actual(scene, actual, baseline_root)
    if baseline is None:
        return []
    if parent.name == "stills":
        return [baseline]

    for sequence in scene.get("sequences", []):
        if parent.name != sequence["name"]:
            continue
        frame_index = sequence_frame_index(actual)
        tolerance = int(sequence.get("temporalToleranceFrames", 0))
        frame_count = int(sequence.get("frames", 0))
        if frame_index is None or tolerance <= 0 or frame_count <= 0:
            return [baseline]
        baseline_dir = baseline_root / sequence["baselineDir"]
        start = max(0, frame_index - tolerance)
        end = min(frame_count - 1, frame_index + tolerance)
        return [baseline_dir / f"frame-{index:04d}.png" for index in range(start, end + 1)]
    return [baseline]


def baseline_comparison_skip_reason(scene: dict[str, Any], actual: Path) -> str | None:
    parent = actual.parent
    if parent.name == "stills":
        return None

    for sequence in scene.get("sequences", []):
        if parent.name != sequence["name"]:
            continue
        frame_index = sequence_frame_index(actual)
        tolerance = int(sequence.get("temporalToleranceFrames", 0))
        frame_count = int(sequence.get("frames", 0))
        if frame_index is None or tolerance <= 0 or frame_count <= 0:
            return None
        if frame_index < tolerance or frame_index >= frame_count - tolerance:
            return "temporalEdge"
    return None


def evaluate_png(
    *,
    commands: ImageMagickCommands,
    actual: Path,
    baseline: Path | None,
    baseline_candidates: list[Path] | None = None,
    diff: Path | None,
    thresholds: dict[str, float],
    expected_dimensions: str | None = None,
    skip_baseline_reason: str | None = None,
) -> FrameResult:
    metrics: dict[str, Any] = {}
    status = "pass"
    try:
        metrics["dimensions"] = image_dimensions(commands, actual)
    except (RuntimeError, ValueError) as exc:
        metrics["error"] = str(exc)
        return FrameResult(
            name=actual.stem,
            status="fail",
            actual=str(actual),
            baseline=str(baseline) if baseline else None,
            diff=str(diff) if diff else None,
            metrics=metrics,
        )
    if expected_dimensions is not None and metrics["dimensions"] != expected_dimensions:
        status = "fail"
        metrics["expectedDimensions"] = expected_dimensions
        metrics["dimensionMismatch"] = True

    try:
        metrics["grayStddev"] = gray_stddev(commands, actual)
        metrics["uniqueColors100x100"] = unique_colors(commands, actual)
    except (RuntimeError, ValueError) as exc:
        metrics["error"] = str(exc)
        status = "fail"
        return FrameResult(
            name=actual.stem,
            status=status,
            actual=str(actual),
            baseline=str(baseline) if baseline else None,
            diff=str(diff) if diff else None,
            metrics=metrics,
        )

    if metrics["grayStddev"] < thresholds["minGrayStddev"]:
        status = "fail"
    if metrics["uniqueColors100x100"] < thresholds["minUniqueColors"]:
        status = "fail"
    if skip_baseline_reason:
        if baseline and not baseline.exists():
            status = worst_status(status, "review")
            metrics["baselineMissing"] = True
            return FrameResult(
                name=actual.stem,
                status=status,
                actual=str(actual),
                baseline=str(baseline),
                diff=str(diff) if diff else None,
                metrics=metrics,
            )
        metrics["baselineComparisonSkipped"] = skip_baseline_reason
        return FrameResult(
            name=actual.stem,
            status=status,
            actual=str(actual),
            baseline=str(baseline) if baseline else None,
            diff=str(diff) if diff else None,
            metrics=metrics,
        )

    candidates = baseline_candidates if baseline_candidates is not None else ([baseline] if baseline else [])
    existing_candidates = [candidate for candidate in candidates if candidate is not None and candidate.exists()]
    if existing_candidates:
        try:
            scores = [(compare_rmse(commands, candidate, actual), candidate) for candidate in existing_candidates]
            rmse, matched_baseline = min(scores, key=lambda item: item[0])
            if diff is not None:
                rmse = compare_rmse(commands, matched_baseline, actual, diff)
            metrics["rmse"] = rmse
            if matched_baseline != baseline:
                metrics["matchedBaseline"] = str(matched_baseline)
                actual_index = sequence_frame_index(actual)
                matched_index = sequence_frame_index(matched_baseline)
                if actual_index is not None and matched_index is not None:
                    metrics["baselineFrameOffset"] = matched_index - actual_index
            status = worst_status(status, classify_rmse(rmse, thresholds))
        except (RuntimeError, ValueError) as exc:
            metrics["compareError"] = str(exc)
            status = "fail"
    elif baseline:
        status = worst_status(status, "review")
        metrics["baselineMissing"] = True

    return FrameResult(
        name=actual.stem,
        status=status,
        actual=str(actual),
        baseline=str(baseline) if baseline else None,
        diff=str(diff) if diff else None,
        metrics=metrics,
    )


def dataclass_to_json(value: Any) -> Any:
    if hasattr(value, "__dataclass_fields__"):
        return {field_name: dataclass_to_json(getattr(value, field_name)) for field_name in value.__dataclass_fields__}
    if isinstance(value, list):
        return [dataclass_to_json(item) for item in value]
    if isinstance(value, dict):
        return {key: dataclass_to_json(item) for key, item in value.items()}
    return value


def write_review_clip(ffmpeg: str | None, frames_dir: Path, output: Path, fps: int = 30) -> str | None:
    if ffmpeg is None:
        return None
    if not sorted(frames_dir.glob("frame-*.png")):
        return None

    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        ffmpeg,
        "-y",
        "-framerate",
        str(fps),
        "-pattern_type",
        "glob",
        "-i",
        str(frames_dir / "frame-*.png"),
        "-pix_fmt",
        "yuv420p",
        str(output),
    ]
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
    if completed.returncode != 0:
        return None
    return str(output)


def promote_baselines(run_dir: Path, baseline_root: Path) -> list[Path]:
    summary_path = run_dir / "summary.json"
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    if not summary.get("writeCandidates", False):
        raise ValueError(f"{run_dir} was not created with --write-candidates")
    if int(summary.get("counts", {}).get("fail", 0)) > 0:
        raise ValueError(f"{run_dir} contains failed scenes; refusing to promote baselines")

    promoted: list[Path] = []
    for scene in summary.get("scenes", []):
        if scene.get("status") == "fail":
            raise ValueError(f"{run_dir} contains failed scene {scene.get('id', '<unknown>')}; refusing to promote baselines")
        for frame in scene.get("frames", []):
            if frame.get("status") == "fail":
                raise ValueError(
                    f"{run_dir} contains failed frame {scene.get('id', '<unknown>')}/{frame.get('name', '<unknown>')}; "
                    "refusing to promote baselines"
                )
            actual = Path(frame["actual"])
            baseline_text = frame.get("baseline")
            if not baseline_text or actual.suffix.lower() != ".png":
                continue
            ensure_relative_to(run_dir, actual)

            baseline = Path(baseline_text)
            if not baseline.is_absolute():
                baseline = baseline_root / baseline
            if baseline.suffix.lower() != ".png":
                continue
            ensure_relative_to(baseline_root, baseline)

            baseline.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(actual, baseline)
            promoted.append(baseline)
    return promoted


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = repo_root()
    manifest = load_manifest((root / args.manifest).resolve())
    scenes = select_scenes(manifest, args.suite)

    if args.coverage:
        matrix_path = Path(args.coverage_matrix)
        if not matrix_path.is_absolute():
            matrix_path = root / matrix_path
        matrix = load_coverage_matrix(matrix_path.resolve())
        summaries = coverage_bucket_summaries(matrix)
        errors = validate_coverage_matrix(matrix, manifest)
        if args.coverage_format == "json":
            print(format_coverage_json(summaries), end="")
        else:
            print(format_coverage_markdown(summaries), end="")
        for error in errors:
            print(f"coverage error: {error}", file=sys.stderr)
        return 1 if errors else 0

    if args.promote:
        paths = dict(manifest["paths"])
        baseline_root = expand_manifest_path(paths["baselines"], paths, root)
        promoted = promote_baselines(Path(args.promote), baseline_root)
        for path in promoted:
            print(f"PROMOTED {path}")
        return 0

    if args.list:
        for scene in scenes:
            print(f"{scene['id']} {scene['name']} required={scene.get('required', False)}")
        return 0

    try:
        imagemagick = require_imagemagick()
    except RuntimeError as exc:
        print(f"FAIL dependency: {exc}", file=sys.stderr)
        return 1

    ffmpeg = find_ffmpeg()
    if ffmpeg is None:
        print("WARN dependency: ffmpeg not found; review clips will be skipped", file=sys.stderr)

    if not args.keep_shader_cache:
        removed = clear_shader_cache(Path.home() / ".cache" / "wescene-renderer")
        print(f"Cleared shader cache entries: {len(removed)}")

    if args.dry_run:
        print(f"DRY-RUN suite={args.suite} scenes={len(scenes)} imagemagick={imagemagick.identify[0]}")
        return 0

    run_dir = timestamped_artifact_dir(Path(args.artifacts))
    summary = RunSummary()
    scene_results: list[SceneResult] = []
    paths = dict(manifest["paths"])
    baseline_root = expand_manifest_path(paths["baselines"], paths, root)

    for scene in scenes:
        thresholds = merged_thresholds(manifest, scene)
        capture_size = merged_capture_size(manifest, scene)
        expected_dimensions = f"{capture_size['width']}x{capture_size['height']}"
        capture_status, log_path, actuals, notes = run_scene_captures(
            manifest=manifest,
            scene=scene,
            root=root,
            run_dir=run_dir,
            assets_override=args.assets,
            workshop_override=args.workshop,
        )
        required = bool(scene.get("required", False))
        if capture_status in ("skip", "fail"):
            summary.add(capture_status, required=required)
            scene_results.append(
                SceneResult(scene["id"], scene["name"], capture_status, required, str(log_path) if log_path else None, notes=notes)
            )
            print(f"{capture_status.upper()} {scene['id']} {scene['name']}: {'; '.join(notes)}")
            continue

        scene_frames: list[FrameResult] = []
        scene_status = "pass"
        diffs_dir = run_dir / scene["id"] / "diffs"

        for actual in actuals:
            baseline = baseline_for_actual(scene, actual, baseline_root)
            baseline_candidates = baseline_candidates_for_actual(scene, actual, baseline_root)
            skip_baseline_reason = baseline_comparison_skip_reason(scene, actual)
            diff = diffs_dir / actual.name if baseline else None
            frame = evaluate_png(
                commands=imagemagick,
                actual=actual,
                baseline=baseline,
                baseline_candidates=baseline_candidates,
                diff=diff,
                thresholds=thresholds,
                expected_dimensions=expected_dimensions,
                skip_baseline_reason=skip_baseline_reason,
            )
            scene_frames.append(frame)
            scene_status = worst_status(scene_status, frame.status)
            if "error" in frame.metrics:
                notes.append(f"frame {frame.name} metric failed: {frame.metrics['error']}")
            if "compareError" in frame.metrics:
                notes.append(f"frame {frame.name} comparison failed: {frame.metrics['compareError']}")
            if frame.metrics.get("dimensionMismatch"):
                notes.append(
                    f"frame {frame.name} dimensions {frame.metrics['dimensions']} != {frame.metrics['expectedDimensions']}"
                )

        scene_log_dir = run_dir / scene["id"]
        for candidate_log in sorted(scene_log_dir.glob("*.log")):
            for failure in scan_log_failures(candidate_log):
                scene_status = "fail"
                notes.append(f"log failure in {candidate_log.name}: {failure}")

        for sequence in scene.get("sequences", []):
            sequence_dir = run_dir / scene["id"] / "captures" / sequence["name"]
            sequence_frames = sorted(sequence_dir.glob("frame-*.png"))
            motion_values = []
            motion_error = None
            for left, right in zip(sequence_frames, sequence_frames[1:]):
                try:
                    motion_values.append(compare_rmse(imagemagick, left, right))
                except (RuntimeError, ValueError) as exc:
                    motion_error = f"{left.name}->{right.name}: {exc}"
                    break
            if motion_error is not None:
                scene_status = "fail"
                notes.append(f"motion comparison failed for {sequence['name']}: {motion_error}")
                continue
            motion_status = classify_motion(
                motion_values,
                bool(scene.get("expectations", {}).get("motion", False)),
                thresholds,
            )
            if motion_status == "fail":
                scene_status = "fail"
                notes.append(
                    f"motion expectation failed for {sequence['name']}: max adjacent RMSE={max(motion_values) if motion_values else 0}"
                )

        for sequence in scene.get("sequences", []):
            frames_dir = run_dir / scene["id"] / "captures" / sequence["name"]
            clip = write_review_clip(
                ffmpeg,
                frames_dir,
                run_dir / scene["id"] / "review-clips" / f"{sequence['name']}.mp4",
            )
            if clip:
                print(f"  review clip: {clip}")

        summary.add(scene_status, required=required)
        scene_results.append(
            SceneResult(
                scene["id"],
                scene["name"],
                scene_status,
                required,
                str(log_path) if log_path else None,
                frames=scene_frames,
                notes=notes,
            )
        )
        print(f"{scene_status.upper()} {scene['id']} {scene['name']} frames={len(scene_frames)}")

    output = {
        "suite": args.suite,
        "strict": args.strict,
        "requireAssets": args.require_assets,
        "writeCandidates": args.write_candidates,
        "counts": summary.counts,
        "scenes": [dataclass_to_json(scene) for scene in scene_results],
    }
    (run_dir / "summary.json").write_text(json.dumps(output, indent=2), encoding="utf-8")
    print(f"Artifacts: {run_dir}")
    return summary.exit_code(strict=args.strict, require_assets=args.require_assets)


if __name__ == "__main__":
    raise SystemExit(main())
