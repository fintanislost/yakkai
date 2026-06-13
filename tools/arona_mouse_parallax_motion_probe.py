#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import shlex
import shutil
import subprocess
from typing import Any

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_ROOT = ROOT / "smoke-tests" / "artifacts" / "tmp" / "arona-mouse-parallax-motion"

VISUAL_CHECKLIST = [
    "motion should ease from center to left",
    "motion should cross to right",
    "motion should return to center",
    "no white halos or crop-sheet gaps",
    "ribbon/character and background should not move as a single flat layer",
]


@dataclass(frozen=True)
class MotionProbeConfig:
    repo_root: Path
    harness: Path
    source: Path
    assets: Path
    output_root: Path
    scene_id: str = "3228578419"
    window_size: str = "1600x900"
    timeline: str = "0:0.5,0.5;1000:0,0.5;3000:1,0.5;5000:0.5,0.5"
    capture_sequence: str = "0:180:33"
    record_duration_ms: int = 6000
    record_fps: int = 30
    scene_properties_json: str = ""


def sequence_dir(config: MotionProbeConfig) -> Path:
    return config.output_root / "sequence"


def sequence_frames_dir(config: MotionProbeConfig) -> Path:
    return sequence_dir(config) / "frames"


def sequence_effect_captures_dir(config: MotionProbeConfig) -> Path:
    return sequence_dir(config) / "effect-captures"


def record_path(config: MotionProbeConfig) -> Path:
    return config.output_root / "review-live.mp4"


def record_effect_captures_dir(config: MotionProbeConfig) -> Path:
    return config.output_root / "record-effect-captures"


def _base_command(config: MotionProbeConfig) -> list[str]:
    command = [
        str(config.harness),
        "--backend",
        "paper",
        "--source",
        str(config.source),
        "--assets",
        str(config.assets),
        "--fill",
        "crop",
        "--window-size",
        config.window_size,
        "--hide-info-overlay",
    ]
    if config.scene_properties_json:
        command.extend(["--scene-properties-json", config.scene_properties_json])
    return command


def build_sequence_command(config: MotionProbeConfig) -> list[str]:
    command = _base_command(config)
    command.extend(
        [
            "--capture-dir",
            str(sequence_frames_dir(config)),
            "--capture-sequence",
            config.capture_sequence,
            "--debug-effect-captures",
            str(sequence_effect_captures_dir(config)),
            "--debug-effect-capture-delay-ms",
            "0",
            "--debug-mouse-timeline",
            config.timeline,
        ]
    )
    return command


def build_record_command(config: MotionProbeConfig) -> list[str]:
    command = _base_command(config)
    command.extend(
        [
            "--debug-effect-captures",
            str(record_effect_captures_dir(config)),
            "--debug-effect-capture-delay-ms",
            "0",
            "--debug-mouse-timeline",
            config.timeline,
            "--record",
            str(record_path(config)),
            "--record-duration-ms",
            str(config.record_duration_ms),
            "--record-fps",
            str(config.record_fps),
        ]
    )
    return command


def _run_command(command: list[str], command_path: Path, stdout_path: Path, stderr_path: Path) -> int:
    command_path.parent.mkdir(parents=True, exist_ok=True)
    command_path.write_text(shlex.join(command) + "\n", encoding="utf-8")
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    stdout_path.write_text(completed.stdout, encoding="utf-8")
    stderr_path.write_text(completed.stderr, encoding="utf-8")
    return completed.returncode


def reset_path(path: Path) -> None:
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def reset_sequence_artifacts(config: MotionProbeConfig) -> None:
    reset_path(sequence_dir(config))


def reset_record_artifacts(config: MotionProbeConfig) -> None:
    reset_path(record_path(config))
    reset_path(record_effect_captures_dir(config))


def run_sequence(config: MotionProbeConfig) -> int:
    reset_sequence_artifacts(config)
    sequence_frames_dir(config).mkdir(parents=True, exist_ok=True)
    sequence_effect_captures_dir(config).mkdir(parents=True, exist_ok=True)
    return _run_command(
        build_sequence_command(config),
        sequence_dir(config) / "command.txt",
        sequence_dir(config) / "stdout.log",
        sequence_dir(config) / "stderr.log",
    )


def run_record(config: MotionProbeConfig) -> int:
    config.output_root.mkdir(parents=True, exist_ok=True)
    reset_record_artifacts(config)
    record_effect_captures_dir(config).mkdir(parents=True, exist_ok=True)
    return _run_command(
        build_record_command(config),
        config.output_root / "record-command.txt",
        config.output_root / "record-stdout.log",
        config.output_root / "record-stderr.log",
    )


def _load_manifest(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def _png_frames(path: Path) -> list[Path]:
    if not path.exists():
        return []
    return sorted(item for item in path.iterdir() if item.is_file() and item.suffix.lower() == ".png")


def _sample_frames(frames: list[Path], max_samples: int = 6) -> list[Path]:
    if len(frames) <= max_samples:
        return frames
    last = len(frames) - 1
    indices = sorted({round(index * last / (max_samples - 1)) for index in range(max_samples)})
    return [frames[index] for index in indices]


def write_contact_sheet(output: Path, frames: list[Path]) -> None:
    samples = _sample_frames(frames)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not samples:
        Image.new("RGB", (480, 270), (10, 12, 16)).save(output)
        return

    thumbs: list[tuple[str, Image.Image]] = []
    for frame in samples:
        with Image.open(frame) as image:
            thumb = image.convert("RGB")
            thumb.thumbnail((320, 180), Image.Resampling.LANCZOS)
            thumbs.append((frame.stem, thumb.copy()))

    label_height = 24
    gutter = 8
    width = sum(image.width for _, image in thumbs) + gutter * (len(thumbs) + 1)
    height = max(image.height for _, image in thumbs) + label_height + gutter * 2
    sheet = Image.new("RGB", (width, height), (10, 12, 16))
    draw = ImageDraw.Draw(sheet)
    x = gutter
    for label, image in thumbs:
        draw.text((x, gutter), label, fill=(235, 238, 242))
        sheet.paste(image, (x, gutter + label_height))
        x += image.width + gutter
    sheet.save(output)


def write_motion_report(config: MotionProbeConfig) -> None:
    config.output_root.mkdir(parents=True, exist_ok=True)
    frames = _png_frames(sequence_frames_dir(config))
    manifest_path = sequence_effect_captures_dir(config) / "manifest.json"
    manifest = _load_manifest(manifest_path)
    mouse = manifest.get("mouseParallax", {}) if isinstance(manifest, dict) else {}
    mouse = mouse if isinstance(mouse, dict) else {}

    report = {
        "timeline": config.timeline,
        "windowSize": config.window_size,
        "frameCount": len(frames),
        "mp4Path": str(record_path(config)),
        "sequenceDirectory": str(sequence_dir(config)),
        "manifestMouseParallaxInputSource": mouse.get("inputSource"),
        "manifestTimeline": mouse.get("timeline"),
        "visualChecklist": VISUAL_CHECKLIST,
    }
    (config.output_root / "motion-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Arona Mouse Parallax Motion Probe",
        "",
        f"- Timeline: `{config.timeline}`",
        f"- Window size: `{config.window_size}`",
        f"- Frame count: `{len(frames)}`",
        f"- MP4: `{record_path(config)}`",
        f"- Sequence directory: `{sequence_dir(config)}`",
        f"- Manifest mouseParallax.inputSource: `{mouse.get('inputSource', '')}`",
        f"- Manifest timeline: `{mouse.get('timeline', '')}`",
        f"- Contact sheet: `{config.output_root / 'review-contact-sheet.png'}`",
        "",
        "## Human Visual Checklist",
        "",
    ]
    lines.extend(f"- [ ] {item}" for item in VISUAL_CHECKLIST)
    (config.output_root / "motion-report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    write_contact_sheet(config.output_root / "review-contact-sheet.png", frames)


def positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"expected positive integer: {value}") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError(f"expected positive integer: {value}")
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run Arona synthetic mouse timeline motion diagnostics.")
    parser.add_argument("--scene-id", default="3228578419")
    parser.add_argument("--source", required=True)
    parser.add_argument("--assets", required=True)
    parser.add_argument("--harness", default=str(ROOT / "build/native/scene_harness/yakkai_scene_harness"))
    parser.add_argument("--output-root", default=str(DEFAULT_OUTPUT_ROOT))
    parser.add_argument("--window-size", default="1600x900")
    parser.add_argument("--timeline", default="0:0.5,0.5;1000:0,0.5;3000:1,0.5;5000:0.5,0.5")
    parser.add_argument("--capture-sequence", default="0:180:33")
    parser.add_argument("--record-duration-ms", type=positive_int, default=6000)
    parser.add_argument("--record-fps", type=positive_int, default=30)
    parser.add_argument("--scene-properties-json", default="")
    parser.add_argument("--skip-record", action="store_true")
    return parser


def config_from_args(args: argparse.Namespace) -> MotionProbeConfig:
    if int(args.record_duration_ms) <= 0:
        raise argparse.ArgumentTypeError("--record-duration-ms must be positive")
    if int(args.record_fps) <= 0:
        raise argparse.ArgumentTypeError("--record-fps must be positive")
    return MotionProbeConfig(
        repo_root=ROOT,
        harness=Path(args.harness),
        source=Path(args.source),
        assets=Path(args.assets),
        output_root=Path(args.output_root),
        scene_id=args.scene_id,
        window_size=args.window_size,
        timeline=args.timeline,
        capture_sequence=args.capture_sequence,
        record_duration_ms=int(args.record_duration_ms),
        record_fps=int(args.record_fps),
        scene_properties_json=args.scene_properties_json,
    )


def run_motion_probe(config: MotionProbeConfig, *, skip_record: bool = False) -> int:
    sequence_code = run_sequence(config)
    if sequence_code != 0:
        print(f"sequence harness run failed with exit code {sequence_code}", flush=True)
        return sequence_code
    if not skip_record:
        record_code = run_record(config)
        if record_code != 0:
            stderr_path = config.output_root / "record-stderr.log"
            stderr = stderr_path.read_text(encoding="utf-8") if stderr_path.exists() else ""
            if "--debug-mouse-timeline requires --debug-effect-captures" in stderr:
                print(
                    "NEEDS_CONTEXT: current harness requires --debug-effect-captures for "
                    "--debug-mouse-timeline during recording.",
                    flush=True,
                )
            elif "--record" in stderr and "--debug-effect-captures" in stderr:
                print(
                    "NEEDS_CONTEXT: current harness cannot combine --record with "
                    "--debug-effect-captures, so it cannot record --debug-mouse-timeline. "
                    "Minimal follow-up: allow --debug-mouse-timeline to feed live recording "
                    "without requiring effect captures, or explicitly allow record plus debug captures.",
                    flush=True,
                )
            else:
                print(f"record harness run failed with exit code {record_code}", flush=True)
            return record_code
    write_motion_report(config)
    print(f"motionReport={config.output_root / 'motion-report.md'}")
    print(f"contactSheet={config.output_root / 'review-contact-sheet.png'}")
    print(f"reviewMp4={record_path(config)}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        config = config_from_args(args)
    except argparse.ArgumentTypeError as exc:
        parser.error(str(exc))
    return run_motion_probe(config, skip_record=args.skip_record)


if __name__ == "__main__":
    raise SystemExit(main())
