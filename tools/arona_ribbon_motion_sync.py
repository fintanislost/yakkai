#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import sys
from typing import Any

import numpy as np
from PIL import Image, ImageDraw


DEFAULT_ROI_BASE_SIZE = (1600, 900)
MIN_CONFIDENT_FRAMES = 10
MOVING_AMPLITUDE_PX = 2.0
STATIC_AMPLITUDE_PX = 1.0
SYNC_LAG_TOLERANCE_FRAMES = 2
SAMPLE_INDICES = (0, 10, 20, 30, 40)


@dataclass(frozen=True)
class MotionSeries:
    centroids_y: list[float | None]
    signal_pixels: list[int]
    confident_frames: int
    amplitude: float
    mean_y: float | None


def parse_roi(value: str) -> tuple[int, int, int, int]:
    parts = [part.strip() for part in value.split(",")]
    if len(parts) != 4:
        raise ValueError(f"ROI must have four comma-separated integers: {value}")
    try:
        x, y, width, height = [int(part) for part in parts]
    except ValueError as exc:
        raise ValueError(f"ROI must have four comma-separated integers: {value}") from exc
    if x < 0 or y < 0 or width <= 0 or height <= 0:
        raise ValueError(f"ROI values must be non-negative with positive size: {value}")
    return (x, y, width, height)


def parse_size(value: str) -> tuple[int, int]:
    parts = [part.strip() for part in value.lower().split("x")]
    if len(parts) != 2:
        raise ValueError(f"size must be WIDTHxHEIGHT: {value}")
    try:
        width, height = [int(part) for part in parts]
    except ValueError as exc:
        raise ValueError(f"size must be WIDTHxHEIGHT: {value}") from exc
    if width <= 0 or height <= 0:
        raise ValueError(f"size must be positive: {value}")
    return (width, height)


def load_sequence(path: Path) -> list[Path]:
    frames = sorted(
        item
        for item in path.iterdir()
        if item.is_file() and item.suffix.lower() in {".png", ".jpg", ".jpeg"}
    )
    if not frames:
        raise ValueError(f"no image frames found in {path}")
    return frames


def image_size(path: Path) -> tuple[int, int]:
    with Image.open(path) as image:
        return image.size


def scale_roi(
    roi: tuple[int, int, int, int],
    *,
    from_size: tuple[int, int],
    to_size: tuple[int, int],
) -> tuple[int, int, int, int]:
    if from_size == to_size:
        return roi
    scale_x = to_size[0] / from_size[0]
    scale_y = to_size[1] / from_size[1]
    x, y, width, height = roi
    return (
        max(0, round(x * scale_x)),
        max(0, round(y * scale_y)),
        max(1, round(width * scale_x)),
        max(1, round(height * scale_y)),
    )


def clamp_roi(
    roi: tuple[int, int, int, int],
    size: tuple[int, int],
) -> tuple[int, int, int, int]:
    x, y, width, height = roi
    image_width, image_height = size
    x = min(max(0, x), max(0, image_width - 1))
    y = min(max(0, y), max(0, image_height - 1))
    width = max(1, min(width, image_width - x))
    height = max(1, min(height, image_height - y))
    return (x, y, width, height)


def crop_rgba(path: Path, roi: tuple[int, int, int, int]) -> np.ndarray:
    with Image.open(path).convert("RGBA") as image:
        clamped = clamp_roi(roi, image.size)
        x, y, width, height = clamped
        crop = image.crop((x, y, x + width, y + height))
        return np.asarray(crop, dtype=np.float32) / 255.0


def salient_weights(crop: np.ndarray) -> np.ndarray:
    rgb = crop[..., :3]
    alpha = crop[..., 3]
    luminance = (0.2126 * rgb[..., 0]) + (0.7152 * rgb[..., 1]) + (0.0722 * rgb[..., 2])
    visible = alpha > 0.05
    if not np.any(visible):
        return np.zeros(luminance.shape, dtype=np.float32)

    gx = np.zeros_like(luminance, dtype=np.float32)
    gy = np.zeros_like(luminance, dtype=np.float32)
    gx[:, 1:-1] = luminance[:, 2:] - luminance[:, :-2]
    gy[1:-1, :] = luminance[2:, :] - luminance[:-2, :]
    gradient = np.sqrt(gx * gx + gy * gy)
    edge_values = gradient[visible]
    edge_threshold = max(float(np.percentile(edge_values, 85.0)), 0.015)
    edge_mask = visible & (gradient >= edge_threshold)
    if np.count_nonzero(edge_mask) > 0:
        return gradient * edge_mask.astype(np.float32)

    visible_values = luminance[visible]
    threshold = max(float(np.percentile(visible_values, 80.0)), float(np.mean(visible_values) + 0.5 * np.std(visible_values)))
    threshold = max(threshold, 0.08)
    fallback_mask = visible & (luminance >= threshold)
    return luminance * fallback_mask.astype(np.float32)


def motion_series(frames: list[Path], roi: tuple[int, int, int, int]) -> MotionSeries:
    centroids: list[float | None] = []
    signal_pixels: list[int] = []
    for frame in frames:
        crop = crop_rgba(frame, roi)
        weights = salient_weights(crop)
        count = int(np.count_nonzero(weights > 0.0))
        signal_pixels.append(count)
        if count == 0:
            centroids.append(None)
            continue
        ys = np.indices(weights.shape, dtype=np.float32)[0]
        total_weight = float(np.sum(weights))
        if total_weight <= 1.0e-8:
            centroids.append(None)
            continue
        centroids.append(float(np.sum(ys * weights) / total_weight))

    valid = [value for value in centroids if value is not None]
    amplitude = float(max(valid) - min(valid)) if valid else 0.0
    mean_y = float(np.mean(valid)) if valid else None
    return MotionSeries(
        centroids_y=centroids,
        signal_pixels=signal_pixels,
        confident_frames=len(valid),
        amplitude=amplitude,
        mean_y=mean_y,
    )


def dense_values(series: MotionSeries) -> np.ndarray:
    if series.mean_y is None:
        return np.asarray([], dtype=np.float32)
    return np.asarray(
        [series.mean_y if value is None else value for value in series.centroids_y],
        dtype=np.float32,
    )


def best_lag_and_correlation(
    left: MotionSeries,
    right: MotionSeries,
    *,
    max_lag: int | None = None,
) -> tuple[int | None, float | None]:
    left_values = dense_values(left)
    right_values = dense_values(right)
    if left_values.size == 0 or right_values.size == 0:
        return (None, None)
    count = min(left_values.size, right_values.size)
    left_values = left_values[:count] - float(np.mean(left_values[:count]))
    right_values = right_values[:count] - float(np.mean(right_values[:count]))
    if float(np.std(left_values)) <= 1.0e-6 or float(np.std(right_values)) <= 1.0e-6:
        return (0, 1.0 if left.amplitude < STATIC_AMPLITUDE_PX and right.amplitude < STATIC_AMPLITUDE_PX else 0.0)
    if max_lag is None:
        max_lag = max(1, count // 2)

    best_lag: int | None = None
    best_correlation: float | None = None
    for lag in range(-max_lag, max_lag + 1):
        if lag < 0:
            l_values = left_values[-lag:]
            r_values = right_values[: count + lag]
        elif lag > 0:
            l_values = left_values[: count - lag]
            r_values = right_values[lag:]
        else:
            l_values = left_values
            r_values = right_values
        if l_values.size < max(4, count // 3):
            continue
        denom = float(np.linalg.norm(l_values) * np.linalg.norm(r_values))
        if denom <= 1.0e-8:
            continue
        correlation = float(np.dot(l_values, r_values) / denom)
        if best_correlation is None or correlation > best_correlation:
            best_correlation = correlation
            best_lag = lag
    return (best_lag, best_correlation)


def classify_motion_pair(tip: MotionSeries, main: MotionSeries) -> dict[str, Any]:
    if tip.confident_frames < MIN_CONFIDENT_FRAMES or main.confident_frames < MIN_CONFIDENT_FRAMES:
        return {
            "classification": "low-confidence",
            "reason": "not enough salient frames in one or both ROIs",
            "tip": series_summary(tip),
            "main": series_summary(main),
            "bestLagFrames": None,
            "correlation": None,
        }

    tip_moving = tip.amplitude >= MOVING_AMPLITUDE_PX
    main_moving = main.amplitude >= MOVING_AMPLITUDE_PX
    tip_static = tip.amplitude < STATIC_AMPLITUDE_PX
    main_static = main.amplitude < STATIC_AMPLITUDE_PX

    lag, correlation = best_lag_and_correlation(tip, main)
    if tip_moving and main_static:
        classification = "tip-only"
        reason = "tip moves while main ribbon remains nearly static"
    elif main_moving and tip_static:
        classification = "main-only"
        reason = "main ribbon moves while tip remains nearly static"
    elif tip_moving and main_moving:
        if lag is not None and abs(lag) <= SYNC_LAG_TOLERANCE_FRAMES and (correlation is None or correlation >= 0.35):
            classification = "in-sync"
            reason = "tip and main ribbon motion have matching phase"
        else:
            classification = "out-of-sync"
            reason = "tip and main ribbon motion both move but phase differs"
    else:
        classification = "in-sync"
        reason = "both regions are stable"

    return {
        "classification": classification,
        "reason": reason,
        "tip": series_summary(tip),
        "main": series_summary(main),
        "bestLagFrames": lag,
        "correlation": round(correlation, 6) if correlation is not None else None,
    }


def series_summary(series: MotionSeries) -> dict[str, Any]:
    return {
        "confidentFrames": series.confident_frames,
        "amplitude": round(series.amplitude, 6),
        "meanY": round(series.mean_y, 6) if series.mean_y is not None else None,
        "centroidsY": [
            round(value, 6) if value is not None else None
            for value in series.centroids_y
        ],
        "signalPixels": series.signal_pixels,
    }


def classify_delta(windows: dict[str, Any], yakkai: dict[str, Any]) -> dict[str, Any]:
    windows_class = windows["classification"]
    yakkai_class = yakkai["classification"]
    if "low-confidence" in {windows_class, yakkai_class}:
        classification = "low-confidence"
    elif windows_class == yakkai_class:
        classification = "motion-matches-reference"
    elif windows_class == "in-sync" and yakkai_class in {"tip-only", "main-only", "out-of-sync"}:
        classification = "yakkai-motion-diverges-from-reference"
    else:
        classification = "motion-differs-from-reference"
    return {
        "classification": classification,
        "windowsClassification": windows_class,
        "yakkaiClassification": yakkai_class,
    }


def sequence_rois(
    frames: list[Path],
    tip_roi: tuple[int, int, int, int],
    main_roi: tuple[int, int, int, int],
    *,
    roi_base_size: tuple[int, int],
) -> tuple[tuple[int, int, int, int], tuple[int, int, int, int]]:
    size = image_size(frames[0])
    return (
        clamp_roi(scale_roi(tip_roi, from_size=roi_base_size, to_size=size), size),
        clamp_roi(scale_roi(main_roi, from_size=roi_base_size, to_size=size), size),
    )


def build_sequence_report(
    frames: list[Path],
    tip_roi: tuple[int, int, int, int],
    main_roi: tuple[int, int, int, int],
) -> dict[str, Any]:
    tip = motion_series(frames, tip_roi)
    main = motion_series(frames, main_roi)
    report = classify_motion_pair(tip, main)
    report["frameCount"] = len(frames)
    report["tipRoi"] = list(tip_roi)
    report["mainRoi"] = list(main_roi)
    return report


def build_report(
    windows_frames_dir: Path,
    yakkai_frames_dir: Path,
    *,
    tip_roi: tuple[int, int, int, int],
    main_roi: tuple[int, int, int, int],
    label: str,
    output_dir: Path | None = None,
    roi_base_size: tuple[int, int] = DEFAULT_ROI_BASE_SIZE,
) -> dict[str, Any]:
    windows_frames = load_sequence(windows_frames_dir)
    yakkai_frames = load_sequence(yakkai_frames_dir)
    windows_tip_roi, windows_main_roi = sequence_rois(
        windows_frames,
        tip_roi,
        main_roi,
        roi_base_size=roi_base_size,
    )
    yakkai_tip_roi, yakkai_main_roi = sequence_rois(
        yakkai_frames,
        tip_roi,
        main_roi,
        roi_base_size=roi_base_size,
    )
    windows = build_sequence_report(windows_frames, windows_tip_roi, windows_main_roi)
    yakkai = build_sequence_report(yakkai_frames, yakkai_tip_roi, yakkai_main_roi)
    report = {
        "label": label,
        "roiBaseSize": list(roi_base_size),
        "tipRoi": list(tip_roi),
        "mainRoi": list(main_roi),
        "windowsFrames": str(windows_frames_dir),
        "yakkaiFrames": str(yakkai_frames_dir),
        "windows": windows,
        "yakkai": yakkai,
        "delta": classify_delta(windows, yakkai),
    }
    if output_dir is not None:
        write_artifacts(
            report,
            output_dir,
            windows_frames,
            yakkai_frames,
            windows_tip_roi,
            windows_main_roi,
            yakkai_tip_roi,
            yakkai_main_roi,
        )
    return report


def crop_image(path: Path, roi: tuple[int, int, int, int]) -> Image.Image:
    with Image.open(path).convert("RGB") as image:
        clamped = clamp_roi(roi, image.size)
        x, y, width, height = clamped
        return image.crop((x, y, x + width, y + height))


def save_sample_crops(
    frames: list[Path],
    roi: tuple[int, int, int, int],
    output_dir: Path,
    *,
    prefix: str,
) -> list[Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    saved: list[Path] = []
    for sample in SAMPLE_INDICES:
        if sample >= len(frames):
            continue
        crop = crop_image(frames[sample], roi)
        out = output_dir / f"{prefix}-{sample:04d}.png"
        crop.save(out)
        saved.append(out)
    return saved


def make_contact_sheet(
    samples: list[tuple[str, Path]],
    output_path: Path,
    *,
    thumb_width: int = 180,
) -> None:
    thumbs: list[tuple[str, Image.Image]] = []
    for label, path in samples:
        with Image.open(path).convert("RGB") as image:
            height = max(1, round(image.height * (thumb_width / image.width)))
            thumbs.append((label, image.resize((thumb_width, height), Image.Resampling.BILINEAR)))
    if not thumbs:
        return
    label_height = 16
    columns = min(5, len(thumbs))
    rows = (len(thumbs) + columns - 1) // columns
    cell_width = thumb_width
    cell_height = max(image.height for _, image in thumbs) + label_height
    sheet = Image.new("RGB", (columns * cell_width, rows * cell_height), (20, 20, 24))
    draw = ImageDraw.Draw(sheet)
    for index, (label, image) in enumerate(thumbs):
        col = index % columns
        row = index // columns
        x = col * cell_width
        y = row * cell_height
        draw.text((x + 4, y + 2), label, fill=(230, 230, 235))
        sheet.paste(image, (x, y + label_height))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output_path)


def write_artifacts(
    report: dict[str, Any],
    output_dir: Path,
    windows_frames: list[Path],
    yakkai_frames: list[Path],
    windows_tip_roi: tuple[int, int, int, int],
    windows_main_roi: tuple[int, int, int, int],
    yakkai_tip_roi: tuple[int, int, int, int],
    yakkai_main_roi: tuple[int, int, int, int],
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "motion-report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True),
        encoding="utf-8",
    )
    (output_dir / "motion-report.md").write_text(render_markdown(report), encoding="utf-8")

    crop_specs = [
        ("windows-tip", windows_frames, windows_tip_roi, output_dir / "tip-crops"),
        ("windows-main", windows_frames, windows_main_roi, output_dir / "main-crops"),
        ("yakkai-tip", yakkai_frames, yakkai_tip_roi, output_dir / "tip-crops"),
        ("yakkai-main", yakkai_frames, yakkai_main_roi, output_dir / "main-crops"),
    ]
    samples: list[tuple[str, Path]] = []
    for prefix, frames, roi, crop_dir in crop_specs:
        for path in save_sample_crops(frames, roi, crop_dir, prefix=prefix):
            samples.append((path.stem, path))
    make_contact_sheet(samples, output_dir / "motion-contact-sheet.png")


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        f"# Ribbon Motion Report: {report['label']}",
        "",
        f"- Delta: `{report['delta']['classification']}`",
        f"- Windows: `{report['windows']['classification']}` ({report['windows']['reason']})",
        f"- Yakkai: `{report['yakkai']['classification']}` ({report['yakkai']['reason']})",
        f"- Tip ROI base: `{report['tipRoi']}`",
        f"- Main ROI base: `{report['mainRoi']}`",
        "",
        "## Metrics",
        "",
        "| Sequence | Region | Frames | Amplitude | Mean Y |",
        "|---|---:|---:|---:|---:|",
    ]
    for name in ("windows", "yakkai"):
        item = report[name]
        for region in ("tip", "main"):
            series = item[region]
            lines.append(
                f"| {name} | {region} | {series['confidentFrames']} | "
                f"{series['amplitude']} | {series['meanY']} |"
            )
        lines.append(
            f"| {name} | best lag |  | {item['bestLagFrames']} | corr={item['correlation']} |"
        )
    lines.append("")
    lines.append("## Interpretation")
    lines.append("")
    if report["delta"]["classification"] == "yakkai-motion-diverges-from-reference":
        lines.append(
            "Windows reference motion and Yakkai motion differ in the selected ribbon regions. "
            "Continue with puppet slot/effect boundary evidence before changing renderer code."
        )
    elif report["delta"]["classification"] == "motion-matches-reference":
        lines.append(
            "The selected ribbon regions move with the same classification in both sequences. "
            "If visual mismatch remains, the next target is likely color/flare/composition rather than motion sync."
        )
    else:
        lines.append(
            "The selected regions did not produce a decisive motion-sync comparison. "
            "Adjust ROIs or gather more focused reference evidence before changing renderer code."
        )
    return "\n".join(lines) + "\n"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compare ribbon tip/main motion between two frame sequences.")
    parser.add_argument("--windows-frames", type=Path, required=True)
    parser.add_argument("--yakkai-frames", type=Path, required=True)
    parser.add_argument("--tip-roi", type=parse_roi, required=True)
    parser.add_argument("--main-roi", type=parse_roi, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument(
        "--roi-base-size",
        type=parse_size,
        default=DEFAULT_ROI_BASE_SIZE,
        help="Coordinate space for ROI arguments, default 1600x900.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    report = build_report(
        args.windows_frames,
        args.yakkai_frames,
        tip_roi=args.tip_roi,
        main_roi=args.main_roi,
        label=args.label,
        output_dir=args.output_dir,
        roi_base_size=args.roi_base_size,
    )
    print(render_markdown(report), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
