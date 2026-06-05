#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Iterable

import numpy as np
from PIL import Image, ImageDraw, ImageFont


MOVING_THRESHOLD_PX = 2.0
LOW_CONFIDENCE_MIN_FRAME_RATIO = 0.70
LOW_CONFIDENCE_MIN_SIGNAL = 2.0


def frame_paths(directory: Path) -> list[Path]:
    paths = sorted(
        path
        for path in directory.iterdir()
        if path.is_file() and path.suffix.lower() in {".png", ".jpg", ".jpeg", ".tga"}
    )
    if not paths:
        raise ValueError(f"no image frames found in {directory}")
    return paths


def load_frames(directory: Path | str) -> list[Image.Image]:
    return [Image.open(path).convert("RGBA").copy() for path in frame_paths(Path(directory))]


def parse_int_list(value: str) -> list[int]:
    result: list[int] = []
    for part in value.split(","):
        part = part.strip()
        if not part:
            continue
        result.append(int(part))
    if not result:
        raise ValueError("expected at least one frame index")
    return result


def parse_roi(value: str) -> list[int]:
    parts = [int(part.strip()) for part in value.split(",")]
    if len(parts) != 4:
        raise ValueError("ROI must contain x,y,width,height")
    if parts[0] < 0 or parts[1] < 0 or parts[2] <= 0 or parts[3] <= 0:
        raise ValueError("ROI must have non-negative origin and positive size")
    return parts


def clamp_roi(roi: Iterable[int], size: tuple[int, int]) -> tuple[int, int, int, int]:
    x, y, width, height = [int(value) for value in roi]
    image_width, image_height = size
    x = min(max(0, x), max(0, image_width - 1))
    y = min(max(0, y), max(0, image_height - 1))
    width = max(1, min(width, image_width - x))
    height = max(1, min(height, image_height - y))
    return (x, y, width, height)


def scale_roi(
    roi: Iterable[int],
    *,
    from_size: tuple[int, int],
    to_size: tuple[int, int],
) -> list[int]:
    x, y, width, height = [int(value) for value in roi]
    if from_size == to_size:
        return [x, y, width, height]
    scale_x = to_size[0] / from_size[0]
    scale_y = to_size[1] / from_size[1]
    return [
        max(0, round(x * scale_x)),
        max(0, round(y * scale_y)),
        max(1, round(width * scale_x)),
        max(1, round(height * scale_y)),
    ]


def crop_array(frame: Image.Image, roi: Iterable[int]) -> np.ndarray:
    image = frame.convert("RGBA")
    x, y, width, height = clamp_roi(roi, image.size)
    crop = image.crop((x, y, x + width, y + height))
    return np.asarray(crop, dtype=np.float32) / np.float32(255.0)


def luminance_array(crop: np.ndarray) -> np.ndarray:
    rgb = crop[..., :3]
    return (
        np.float32(0.2126) * rgb[..., 0]
        + np.float32(0.7152) * rgb[..., 1]
        + np.float32(0.0722) * rgb[..., 2]
    )


def gradient_magnitude(values: np.ndarray) -> np.ndarray:
    gx = np.zeros_like(values, dtype=np.float32)
    gy = np.zeros_like(values, dtype=np.float32)
    gx[:, 1:-1] = values[:, 2:] - values[:, :-2]
    gy[1:-1, :] = values[2:, :] - values[:-2, :]
    return np.sqrt(gx * gx + gy * gy)


def _positive_percentile(values: np.ndarray, percentile: float) -> float:
    positive = values[values > 1.0e-6]
    if positive.size == 0:
        return 0.0
    return float(np.percentile(positive, percentile))


def measure_composition_roi(frames: list[Image.Image], roi: Iterable[int]) -> dict[str, Any]:
    edge_means: list[float] = []
    edge_p95s: list[float] = []
    glow_scores: list[float] = []
    cyan_crispness: list[float] = []
    mean_luma: list[float] = []

    for frame in frames:
        crop = crop_array(frame, roi)
        luma = luminance_array(crop)
        gradient = gradient_magnitude(luma)
        rgb = crop[..., :3]
        cyan = np.clip(((rgb[..., 1] + rgb[..., 2]) * np.float32(0.5)) - rgb[..., 0], 0.0, 1.0)
        cyan_signal = np.clip(cyan - float(np.percentile(cyan, 75.0)), 0.0, 1.0)
        cyan_gradient = gradient_magnitude(cyan_signal)

        edge_mean = float(np.mean(gradient))
        edge_p95 = _positive_percentile(gradient, 95.0)
        luma_mean = float(np.mean(luma))
        glow_score = float(luma_mean / (1.0 + edge_p95 * 8.0))
        cyan_coverage = float(np.sum(cyan_signal))
        cyan_edge_density = float(np.sum(cyan_gradient)) / max(cyan_coverage, 1.0e-6)
        cyan_score = cyan_edge_density * _positive_percentile(cyan_signal, 95.0)

        edge_means.append(edge_mean)
        edge_p95s.append(edge_p95)
        glow_scores.append(glow_score)
        cyan_crispness.append(cyan_score)
        mean_luma.append(luma_mean)

    return {
        "frameCount": len(frames),
        "edgeHardnessMean": round(float(np.mean(edge_means)), 6),
        "edgeHardnessP95": round(float(np.mean(edge_p95s)), 6),
        "glowWashScore": round(float(np.mean(glow_scores)), 6),
        "cyanCrispness": round(float(np.mean(cyan_crispness)), 6),
        "meanLuma": round(float(np.mean(mean_luma)), 6),
    }


def _alpha_weights(crop: np.ndarray) -> np.ndarray | None:
    alpha = crop[..., 3]
    spread = float(np.percentile(alpha, 95.0) - np.percentile(alpha, 5.0))
    if spread < 0.08:
        return None
    visible = np.clip(alpha - float(np.percentile(alpha, 25.0)), 0.0, 1.0)
    if float(np.sum(visible)) < LOW_CONFIDENCE_MIN_SIGNAL:
        return None
    return visible.astype(np.float32)


def _edge_weights(crop: np.ndarray) -> np.ndarray | None:
    rgb = crop[..., :3]
    alpha = crop[..., 3]
    luminance = (
        np.float32(0.2126) * rgb[..., 0]
        + np.float32(0.7152) * rgb[..., 1]
        + np.float32(0.0722) * rgb[..., 2]
    )

    gx = np.zeros_like(luminance, dtype=np.float32)
    gy = np.zeros_like(luminance, dtype=np.float32)
    gx[:, 1:-1] = luminance[:, 2:] - luminance[:, :-2]
    gy[1:-1, :] = luminance[2:, :] - luminance[:-2, :]
    gradient = np.sqrt(gx * gx + gy * gy)
    if float(np.max(gradient)) < 0.01:
        return None

    visible = alpha > 0.03
    values = gradient[visible] if np.any(visible) else gradient.reshape(-1)
    if values.size == 0:
        return None
    threshold = max(float(np.percentile(values, 88.0)), 0.012)
    weights = np.where(visible & (gradient >= threshold), gradient, 0.0).astype(np.float32)
    if float(np.sum(weights)) < LOW_CONFIDENCE_MIN_SIGNAL:
        return None
    return weights


def salient_weights(crop: np.ndarray) -> np.ndarray | None:
    alpha = _alpha_weights(crop)
    if alpha is not None:
        return alpha
    return _edge_weights(crop)


def weighted_vertical_centroid(weights: np.ndarray) -> float | None:
    total = float(np.sum(weights))
    if total < LOW_CONFIDENCE_MIN_SIGNAL:
        return None
    ys = np.indices(weights.shape, dtype=np.float32)[0]
    return float(np.sum(ys * weights) / total)


def measure_roi_motion(frames: list[Image.Image], roi: Iterable[int]) -> dict[str, Any]:
    centroids: list[float | None] = []
    signal_sums: list[float] = []
    for frame in frames:
        crop = crop_array(frame, roi)
        weights = salient_weights(crop)
        if weights is None:
            centroids.append(None)
            signal_sums.append(0.0)
            continue
        centroid = weighted_vertical_centroid(weights)
        centroids.append(centroid)
        signal_sums.append(float(np.sum(weights)))

    valid = [value for value in centroids if value is not None]
    if valid:
        low = float(np.percentile(valid, 5.0))
        high = float(np.percentile(valid, 95.0))
        amplitude = high - low
        mean_y = float(np.mean(valid))
    else:
        amplitude = 0.0
        mean_y = None

    confidence_ratio = len(valid) / max(1, len(frames))
    confidence = "high" if confidence_ratio >= LOW_CONFIDENCE_MIN_FRAME_RATIO else "low"
    return {
        "frameCount": len(frames),
        "validFrameCount": len(valid),
        "confidence": confidence,
        "confidenceRatio": round(confidence_ratio, 6),
        "verticalAmplitudePx": round(float(amplitude), 6),
        "meanYPx": None if mean_y is None else round(mean_y, 6),
        "centroidsY": [None if value is None else round(float(value), 6) for value in centroids],
        "signalSums": [round(value, 6) for value in signal_sums],
    }


def classify_motion(result: dict[str, Any], moving_threshold_px: float = MOVING_THRESHOLD_PX) -> str:
    if result.get("confidence") != "high":
        return "low-confidence"
    amplitude = float(result.get("verticalAmplitudePx", 0.0))
    if amplitude >= moving_threshold_px:
        return "moving"
    return "static"


def measure_sequence(
    directory: Path,
    regions: dict[str, dict[str, Any]],
    *,
    roi_base_size: tuple[int, int],
    moving_threshold_px: float = MOVING_THRESHOLD_PX,
) -> dict[str, Any]:
    frames = load_frames(directory)
    frame_size = frames[0].size
    measured: dict[str, Any] = {}
    for name, config in regions.items():
        roi = scale_roi(config["roi"], from_size=roi_base_size, to_size=frame_size)
        result = measure_roi_motion(frames, roi)
        result["classification"] = classify_motion(result, moving_threshold_px)
        result["roi"] = roi
        result["sourceRoi"] = config["roi"]
        result["description"] = config.get("description", "")
        measured[name] = result
    return {
        "directory": str(directory),
        "frameCount": len(frames),
        "frameSize": list(frame_size),
        "regions": measured,
    }


def measure_lower_tip_composition_sequence(
    directory: Path,
    regions: dict[str, dict[str, Any]],
    *,
    roi_base_size: tuple[int, int],
) -> dict[str, Any]:
    frames = load_frames(directory)
    frame_size = frames[0].size
    measured: dict[str, Any] = {}
    for name, config in regions.items():
        roi = scale_roi(config["roi"], from_size=roi_base_size, to_size=frame_size)
        motion = measure_roi_motion(frames, roi)
        composition = measure_composition_roi(frames, roi)
        measured[name] = {
            "description": config.get("description", ""),
            "sourceRoi": config["roi"],
            "roi": roi,
            "motion": {
                **motion,
                "classification": classify_motion(motion),
            },
            "composition": composition,
        }
    return {
        "directory": str(directory),
        "frameCount": len(frames),
        "frameSize": list(frame_size),
        "regions": measured,
    }


def classify_lower_tip_composition(report: dict[str, Any]) -> str:
    windows = report["sequences"]["windows"]["regions"]
    yakkai = report["sequences"]["normal-yakkai"]["regions"]

    win_tip_motion = windows["lower_tip"]["motion"]["classification"]
    yak_tip_motion = yakkai["lower_tip"]["motion"]["classification"]
    if win_tip_motion == "low-confidence":
        return "reference-alignment-insufficient"
    if yak_tip_motion == "low-confidence":
        return "roi-or-slot-ambiguous"

    motion_mismatch = win_tip_motion == "moving" and yak_tip_motion == "static"

    win_edge = float(windows["lower_tip"]["composition"]["edgeHardnessP95"])
    yak_edge = float(yakkai["lower_tip"]["composition"]["edgeHardnessP95"])
    win_glow = float(windows["wall_glow"]["composition"]["glowWashScore"])
    yak_glow = float(yakkai["wall_glow"]["composition"]["glowWashScore"])
    win_cyan = float(windows["cyan_strand"]["composition"]["cyanCrispness"])
    yak_cyan = float(yakkai["cyan_strand"]["composition"]["cyanCrispness"])

    hard_edge = yak_edge > win_edge * 1.20
    glow_missing = yak_glow < win_glow * 0.88
    cyan_too_crisp = yak_cyan > win_cyan * 1.20

    if motion_mismatch and (hard_edge or glow_missing or cyan_too_crisp):
        return "motion-and-composition-mismatch"
    if motion_mismatch:
        return "lower-tip-motion-mismatch"
    if glow_missing:
        return "glow-or-blur-contribution-missing"
    if hard_edge or cyan_too_crisp:
        return "composition-hard-edge-mismatch"
    return "no-lower-tip-mismatch-reproduced"


def classify_boundary(sequences: dict[str, dict[str, Any]]) -> str:
    windows_tip = sequences["windows"]["regions"]["tip"]["classification"]
    yakkai_tip = sequences["normal-yakkai"]["regions"]["tip"]["classification"]

    if windows_tip == "low-confidence":
        return "reference-alignment-insufficient"
    if yakkai_tip == "low-confidence":
        return "roi-or-slot-ambiguous"
    if windows_tip == "static":
        return "reference-alignment-insufficient"
    if yakkai_tip == "moving":
        return "no-static-tip-reproduced"

    ordered = ["N0", "N2", "N7", "N10", "N11"]
    prefix_states: dict[str, str] = {}
    for label in ordered:
        sequence = sequences.get(label)
        if sequence is None:
            continue
        state = sequence["regions"]["tip"]["classification"]
        prefix_states[label] = state
        if state == "low-confidence":
            return "roi-or-slot-ambiguous"

    if prefix_states.get("N0") == "static":
        return "static-before-effects"
    if prefix_states.get("N0") == "moving" and prefix_states.get("N2") == "static":
        return "motion-lost-after-pulse"
    if prefix_states.get("N2") == "moving" and prefix_states.get("N7") == "static":
        return "motion-lost-after-waterwaves"
    if prefix_states.get("N7") == "moving" and prefix_states.get("N10") == "static":
        return "motion-lost-after-early-shake"
    if prefix_states.get("N10") == "moving" and prefix_states.get("N11") == "static":
        return "motion-lost-after-final-shake"
    if all(prefix_states.get(label) == "moving" for label in ordered):
        return "motion-lost-at-final-composition"
    return "roi-or-slot-ambiguous"


def target_for_classification(classification: str) -> str:
    return {
        "static-before-effects": "puppet slot/cutout composition",
        "motion-lost-after-pulse": "pulse routing",
        "motion-lost-after-waterwaves": "waterwaves routing",
        "motion-lost-after-early-shake": "shake routing",
        "motion-lost-after-final-shake": "shake routing",
        "motion-lost-at-final-composition": "final-display composition",
        "reference-alignment-insufficient": "Windows per-effect capture",
        "roi-or-slot-ambiguous": "stronger slot-local capture/ROI instrumentation",
        "no-static-tip-reproduced": "human visual confirmation",
    }.get(classification, "stronger slot-local capture/ROI instrumentation")


def load_region_config(
    path: Path,
    *,
    required_regions: tuple[str, ...] = ("tip", "main"),
) -> tuple[dict[str, dict[str, Any]], list[int], float, tuple[int, int]]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    regions = data.get("regions")
    if not isinstance(regions, dict):
        raise ValueError("region config must contain a regions object")
    missing = [name for name in required_regions if name not in regions]
    if missing:
        joined = ", ".join(f"regions.{name}" for name in missing)
        raise ValueError(f"region config missing required regions: {joined}")
    frames = [int(value) for value in data.get("frames", [0, 10, 20, 30, 39, 50])]
    moving_threshold = float(data.get("movingThresholdPx", MOVING_THRESHOLD_PX))
    base_size_data = data.get("baseSize", [1600, 900])
    if (
        not isinstance(base_size_data, list)
        or len(base_size_data) != 2
        or int(base_size_data[0]) <= 0
        or int(base_size_data[1]) <= 0
    ):
        raise ValueError("baseSize must be [width, height]")
    return regions, frames, moving_threshold, (int(base_size_data[0]), int(base_size_data[1]))


def open_indexed_frame(paths: list[Path], index: int) -> Image.Image:
    clamped = min(max(0, index), len(paths) - 1)
    return Image.open(paths[clamped]).convert("RGBA")


def union_roi(regions: dict[str, dict[str, Any]], size: tuple[int, int], padding: int = 48) -> tuple[int, int, int, int]:
    boxes = []
    for config in regions.values():
        x, y, width, height = clamp_roi(config["roi"], size)
        boxes.append((x, y, x + width, y + height))
    x0 = max(0, min(box[0] for box in boxes) - padding)
    y0 = max(0, min(box[1] for box in boxes) - padding)
    x1 = min(size[0], max(box[2] for box in boxes) + padding)
    y1 = min(size[1], max(box[3] for box in boxes) + padding)
    return (x0, y0, x1 - x0, y1 - y0)


def draw_regions(
    image: Image.Image,
    regions: dict[str, dict[str, Any]],
    *,
    origin: tuple[int, int] = (0, 0),
) -> Image.Image:
    out = image.convert("RGBA").copy()
    draw = ImageDraw.Draw(out)
    colors = {
        "tip": (255, 64, 64, 255),
        "main": (64, 220, 255, 255),
        "lower_tip": (255, 64, 64, 255),
        "wall_glow": (255, 230, 64, 255),
        "cyan_strand": (64, 240, 255, 255),
        "context": (170, 120, 255, 255),
    }
    origin_x, origin_y = origin
    for name, config in regions.items():
        x, y, width, height = [int(value) for value in config["roi"]]
        box = (x - origin_x, y - origin_y, x - origin_x + width, y - origin_y + height)
        color = colors.get(name, (255, 255, 0, 255))
        draw.rectangle(box, outline=color, width=3)
        draw.text((box[0] + 4, box[1] + 4), name, fill=color)
    return out


def label_image(image: Image.Image, label: str, width: int | None = None) -> Image.Image:
    image = image.convert("RGB")
    label_height = 24
    target_width = width or image.width
    out = Image.new("RGB", (target_width, image.height + label_height), (16, 18, 22))
    draw = ImageDraw.Draw(out)
    out.paste(image, (0, label_height))
    draw.text((6, 5), label, fill=(235, 235, 235), font=ImageFont.load_default())
    return out


def make_grid(cells: list[list[Image.Image]], output: Path, gutter: int = 6) -> None:
    if not cells or not cells[0]:
        raise ValueError("cannot build empty contact sheet")
    widths = [max(row[col].width for row in cells if col < len(row)) for col in range(len(cells[0]))]
    heights = [max(cell.height for cell in row) for row in cells]
    total_width = sum(widths) + gutter * (len(widths) - 1)
    total_height = sum(heights) + gutter * (len(heights) - 1)
    sheet = Image.new("RGB", (total_width, total_height), (10, 12, 16))
    y = 0
    for row_index, row in enumerate(cells):
        x = 0
        for col_index, cell in enumerate(row):
            sheet.paste(cell.convert("RGB"), (x, y))
            x += widths[col_index] + gutter
        y += heights[row_index] + gutter
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)


def contact_sheet(
    sequences: dict[str, Path],
    frames: list[int],
    output: Path,
    *,
    regions: dict[str, dict[str, Any]] | None = None,
    roi_base_size: tuple[int, int] = (1600, 900),
) -> None:
    rows: list[list[Image.Image]] = []
    for label, directory in sequences.items():
        paths = frame_paths(directory)
        row: list[Image.Image] = []
        first = open_indexed_frame(paths, 0)
        scaled_regions = None
        if regions:
            scaled_regions = {
                name: {
                    **config,
                    "roi": scale_roi(config["roi"], from_size=roi_base_size, to_size=first.size),
                }
                for name, config in regions.items()
            }
        crop_roi = union_roi(scaled_regions, first.size) if scaled_regions else None
        for index in frames:
            frame = open_indexed_frame(paths, index)
            if crop_roi:
                x, y, width, height = crop_roi
                crop = frame.crop((x, y, x + width, y + height))
                crop = draw_regions(crop, scaled_regions, origin=(x, y))
                thumb = crop
            else:
                thumb = frame.copy()
                thumb.thumbnail((360, 210), Image.Resampling.LANCZOS)
            row.append(label_image(thumb, f"{label} f{index}"))
        rows.append(row)
    make_grid(rows, output)


def write_boundary_report_markdown(report: dict[str, Any], output: Path) -> None:
    lines = [
        "# Arona Ribbon Tip Motion Boundary Report",
        "",
        f"Classification: `{report['classification']}`",
        "",
        f"Recommended next target: `{report['recommendedNextTarget']}`",
        "",
        "## Region Motion",
        "",
        "| Sequence | Tip | Tip amp px | Main | Main amp px | Tip confidence | Main confidence |",
        "| --- | --- | ---: | --- | ---: | --- | --- |",
    ]
    for label, sequence in report["sequences"].items():
        tip = sequence["regions"]["tip"]
        main = sequence["regions"]["main"]
        lines.append(
            "| "
            f"{label} | {tip['classification']} | {tip['verticalAmplitudePx']:.3f} | "
            f"{main['classification']} | {main['verticalAmplitudePx']:.3f} | "
            f"{tip['confidence']} | {main['confidence']} |"
        )
    lines.extend([
        "",
        "## Artifacts",
        "",
        "- `boundary-contact-sheet.png`",
        "- `claim-freeze-contact-sheet.png`",
        "- `boundary-report.json`",
        "",
    ])
    output.write_text("\n".join(lines), encoding="utf-8")


def write_lower_composition_markdown(report: dict[str, Any], output: Path) -> None:
    lines = [
        "# Arona Lower Ribbon Tip Composition Report",
        "",
        f"Classification: `{report['classification']}`",
        "",
        "| Sequence | Region | Motion | Amp px | Edge p95 | Glow wash | Cyan crisp | Mean luma |",
        "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for label, sequence in report["sequences"].items():
        for region_name, region in sequence["regions"].items():
            motion = region["motion"]
            composition = region["composition"]
            lines.append(
                "| "
                f"{label} | {region_name} | {motion['classification']} | "
                f"{motion['verticalAmplitudePx']:.3f} | "
                f"{composition['edgeHardnessP95']:.6f} | "
                f"{composition['glowWashScore']:.6f} | "
                f"{composition['cyanCrispness']:.6f} | "
                f"{composition['meanLuma']:.6f} |"
            )
    lines.extend([
        "",
        "## Artifacts",
        "",
        "- `lower-composition-contact-sheet.png`",
        "- `lower-composition-report.json`",
        "- `region-crops/`",
        "",
    ])
    output.write_text("\n".join(lines), encoding="utf-8")


def write_region_crops(
    sequences: dict[str, Path],
    regions: dict[str, dict[str, Any]],
    frames: list[int],
    output_dir: Path,
    *,
    roi_base_size: tuple[int, int],
) -> None:
    crop_root = output_dir / "region-crops"
    for label, directory in sequences.items():
        paths = frame_paths(directory)
        first = open_indexed_frame(paths, 0)
        scaled_regions = {
            name: {
                **config,
                "roi": scale_roi(config["roi"], from_size=roi_base_size, to_size=first.size),
            }
            for name, config in regions.items()
        }
        for region_name, config in scaled_regions.items():
            region_dir = crop_root / label / region_name
            region_dir.mkdir(parents=True, exist_ok=True)
            for index in frames:
                frame = open_indexed_frame(paths, index)
                x, y, width, height = clamp_roi(config["roi"], frame.size)
                crop = frame.crop((x, y, x + width, y + height))
                crop.save(region_dir / f"frame-{index:04d}.png")


def command_contact_sheet(args: argparse.Namespace) -> int:
    sequences = {
        "windows": Path(args.windows_dir),
        "normal-yakkai": Path(args.yakkai_dir),
    }
    contact_sheet(sequences, parse_int_list(args.frames), Path(args.output))
    return 0


def command_measure(args: argparse.Namespace) -> int:
    frames = load_frames(args.sequence)
    result = measure_roi_motion(frames, parse_roi(args.roi))
    result["classification"] = classify_motion(result, args.moving_threshold_px)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


def command_boundary_report(args: argparse.Namespace) -> int:
    regions, frames, moving_threshold, roi_base_size = load_region_config(
        Path(args.regions),
        required_regions=("tip", "main"),
    )
    sequences: dict[str, Path] = {
        "windows": Path(args.windows_dir),
        "normal-yakkai": Path(args.normal_yakkai_dir),
    }
    for item in args.prefix:
        if "=" not in item:
            raise ValueError(f"--prefix expects LABEL=DIR, got {item}")
        label, directory = item.split("=", 1)
        sequences[label] = Path(directory)

    measured = {
        label: measure_sequence(
            directory,
            regions,
            roi_base_size=roi_base_size,
            moving_threshold_px=moving_threshold,
        )
        for label, directory in sequences.items()
    }
    classification = classify_boundary(measured)
    report = {
        "classification": classification,
        "recommendedNextTarget": target_for_classification(classification),
        "movingThresholdPx": moving_threshold,
        "roiBaseSize": list(roi_base_size),
        "frames": frames,
        "regions": regions,
        "sequences": measured,
    }

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "boundary-report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True),
        encoding="utf-8",
    )
    write_boundary_report_markdown(report, output_dir / "boundary-report.md")
    contact_sheet(
        sequences,
        frames,
        output_dir / "boundary-contact-sheet.png",
        regions=regions,
        roi_base_size=roi_base_size,
    )
    write_region_crops(sequences, regions, frames, output_dir, roi_base_size=roi_base_size)
    return 0


def command_lower_composition_report(args: argparse.Namespace) -> int:
    regions, frames, moving_threshold, roi_base_size = load_region_config(
        Path(args.regions),
        required_regions=("lower_tip", "wall_glow", "cyan_strand", "context"),
    )
    sequences: dict[str, Path] = {
        "windows": Path(args.windows_dir),
        "normal-yakkai": Path(args.normal_yakkai_dir),
    }
    for item in args.prefix:
        if "=" not in item:
            raise ValueError(f"--prefix expects LABEL=DIR, got {item}")
        label, directory = item.split("=", 1)
        sequences[label] = Path(directory)

    measured = {
        label: measure_lower_tip_composition_sequence(
            directory,
            regions,
            roi_base_size=roi_base_size,
        )
        for label, directory in sequences.items()
    }
    report = {
        "classification": classify_lower_tip_composition({
            "sequences": measured,
            "movingThresholdPx": moving_threshold,
        }),
        "movingThresholdPx": moving_threshold,
        "roiBaseSize": list(roi_base_size),
        "frames": frames,
        "regions": regions,
        "sequences": measured,
    }

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "lower-composition-report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True),
        encoding="utf-8",
    )
    write_lower_composition_markdown(report, output_dir / "lower-composition-report.md")
    contact_sheet(
        sequences,
        frames,
        output_dir / "lower-composition-contact-sheet.png",
        regions=regions,
        roi_base_size=roi_base_size,
    )
    write_region_crops(sequences, regions, frames, output_dir, roi_base_size=roi_base_size)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Compare ribbon-tip motion across frame sequences.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    contact = subparsers.add_parser("contact-sheet")
    contact.add_argument("--windows-dir", required=True)
    contact.add_argument("--yakkai-dir", required=True)
    contact.add_argument("--frames", required=True)
    contact.add_argument("--output", required=True)
    contact.set_defaults(func=command_contact_sheet)

    measure = subparsers.add_parser("measure")
    measure.add_argument("--sequence", type=Path, required=True)
    measure.add_argument("--roi", required=True)
    measure.add_argument("--moving-threshold-px", type=float, default=MOVING_THRESHOLD_PX)
    measure.set_defaults(func=command_measure)

    boundary = subparsers.add_parser("boundary-report")
    boundary.add_argument("--regions", required=True)
    boundary.add_argument("--windows-dir", required=True)
    boundary.add_argument("--normal-yakkai-dir", required=True)
    boundary.add_argument("--prefix", action="append", default=[])
    boundary.add_argument("--output-dir", required=True)
    boundary.set_defaults(func=command_boundary_report)

    lower = subparsers.add_parser("lower-composition-report")
    lower.add_argument("--regions", required=True)
    lower.add_argument("--windows-dir", required=True)
    lower.add_argument("--normal-yakkai-dir", required=True)
    lower.add_argument("--prefix", action="append", default=[])
    lower.add_argument("--output-dir", required=True)
    lower.set_defaults(func=command_lower_composition_report)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
