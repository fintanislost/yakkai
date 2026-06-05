#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Iterable

import numpy as np
from PIL import Image

from arona_ribbon_tip_boundary import clamp_roi, frame_paths, open_indexed_frame, scale_roi


LOW_CONFIDENCE_VALID_RATIO = 0.70
FLUTTER_RESIDUAL_THRESHOLD_PX = 1.5
PHASE_CORRELATION_THRESHOLD = 0.70
PHASE_LAG_THRESHOLD_FRAMES = 2
AMPLITUDE_RATIO_THRESHOLD = 0.35


def _rgba_crop(path: Path, roi: Iterable[int]) -> np.ndarray:
    with Image.open(path).convert("RGBA") as image:
        x, y, width, height = clamp_roi(roi, image.size)
        crop = image.crop((x, y, x + width, y + height))
        return np.asarray(crop, dtype=np.float32) / np.float32(255.0)


def _luminance(crop: np.ndarray) -> np.ndarray:
    rgb = crop[..., :3]
    return (
        np.float32(0.2126) * rgb[..., 0]
        + np.float32(0.7152) * rgb[..., 1]
        + np.float32(0.0722) * rgb[..., 2]
    )


def _vertical_gradient(values: np.ndarray) -> np.ndarray:
    gradient = np.zeros_like(values, dtype=np.float32)
    gradient[1:-1, :] = np.abs(values[2:, :] - values[:-2, :])
    return gradient


def _sample_columns(width: int, sample_columns: int) -> np.ndarray:
    count = max(4, min(width, sample_columns))
    return np.linspace(0, width - 1, count).round().astype(np.int32)


def _column_edge_y(gradient: np.ndarray, column: int) -> tuple[float | None, float]:
    values = gradient[:, column]
    peak = float(np.max(values))
    if peak < 0.03:
        return (None, peak)
    threshold = max(peak * 0.60, float(np.percentile(values, 92.0)))
    weights = np.where(values >= threshold, values, 0.0)
    total = float(np.sum(weights))
    if total <= 1.0e-6:
        return (None, peak)
    ys = np.arange(values.shape[0], dtype=np.float32)
    return (float(np.sum(ys * weights) / total), peak)


def extract_edge_trace(directory: Path | str, roi: Iterable[int], *, sample_columns: int = 96) -> dict[str, Any]:
    paths = frame_paths(Path(directory))
    first = _rgba_crop(paths[0], roi)
    columns = _sample_columns(first.shape[1], sample_columns)
    trace = np.full((len(paths), len(columns)), np.nan, dtype=np.float32)
    signal = np.zeros((len(paths), len(columns)), dtype=np.float32)

    for frame_index, path in enumerate(paths):
        crop = _rgba_crop(path, roi)
        gradient = _vertical_gradient(_luminance(crop))
        for column_index, column in enumerate(columns):
            edge_y, strength = _column_edge_y(gradient, int(column))
            signal[frame_index, column_index] = strength
            if edge_y is not None:
                trace[frame_index, column_index] = edge_y

    valid_ratio = float(np.count_nonzero(~np.isnan(trace)) / trace.size)
    confidence = "high" if valid_ratio >= LOW_CONFIDENCE_VALID_RATIO else "low"
    return {
        "directory": str(directory),
        "roi": [int(value) for value in roi],
        "frameCount": len(paths),
        "sampleColumns": [int(value) for value in columns.tolist()],
        "confidence": confidence,
        "validRatio": round(valid_ratio, 6),
        "edgeY": [[None if np.isnan(value) else round(float(value), 6) for value in row] for row in trace],
        "signal": [[round(float(value), 6) for value in row] for row in signal],
    }


def _dense_trace(edge_y: list[list[float | None]]) -> np.ndarray:
    values = np.asarray([[np.nan if value is None else float(value) for value in row] for row in edge_y], dtype=np.float32)
    valid = ~np.isnan(values)
    if not np.any(valid):
        return np.zeros(values.shape, dtype=np.float32)
    global_mean = float(np.mean(values[valid]))
    column_counts = np.count_nonzero(valid, axis=0)
    column_sums = np.nansum(values, axis=0)
    column_means = np.where(column_counts > 0, column_sums / np.maximum(column_counts, 1), global_mean)
    inds = np.where(np.isnan(values))
    values[inds] = np.take(column_means, inds[1])
    return values


def _best_lag_and_correlation(left: np.ndarray, right: np.ndarray, max_lag: int = 8) -> tuple[int, float]:
    left = left.astype(np.float32) - float(np.mean(left))
    right = right.astype(np.float32) - float(np.mean(right))
    best_lag = 0
    best_corr = 0.0
    for lag in range(-max_lag, max_lag + 1):
        if lag < 0:
            l_values = left[-lag:]
            r_values = right[: left.size + lag]
        elif lag > 0:
            l_values = left[: left.size - lag]
            r_values = right[lag:]
        else:
            l_values = left
            r_values = right
        if l_values.size < 4:
            continue
        denom = float(np.linalg.norm(l_values) * np.linalg.norm(r_values))
        if denom <= 1.0e-6:
            continue
        corr = float(np.dot(l_values, r_values) / denom)
        if corr > best_corr:
            best_corr = corr
            best_lag = lag
    return (best_lag, best_corr)


def _residual_matrix(flutter: dict[str, Any]) -> np.ndarray:
    values = _dense_trace(flutter["trace"]["edgeY"])
    if values.size == 0:
        return np.zeros((0, 0), dtype=np.float32)
    rigid = np.median(values, axis=1)
    residual = values - rigid[:, None]
    return residual - np.mean(residual, axis=0, keepdims=True)


def _best_residual_lag_and_correlation(left: dict[str, Any], right: dict[str, Any], max_lag: int = 8) -> tuple[int, float]:
    left_values = _residual_matrix(left)
    right_values = _residual_matrix(right)
    frame_count = min(left_values.shape[0], right_values.shape[0])
    column_count = min(left_values.shape[1], right_values.shape[1])
    if frame_count < 4 or column_count < 4:
        return (0, 0.0)
    left_values = left_values[:frame_count, :column_count]
    right_values = right_values[:frame_count, :column_count]
    best_lag = 0
    best_corr = 0.0
    for lag in range(-max_lag, max_lag + 1):
        if lag < 0:
            l_values = left_values[-lag:, :]
            r_values = right_values[: frame_count + lag, :]
        elif lag > 0:
            l_values = left_values[: frame_count - lag, :]
            r_values = right_values[lag:, :]
        else:
            l_values = left_values
            r_values = right_values
        if l_values.shape[0] < 4:
            continue
        left_flat = l_values.reshape(-1).astype(np.float32)
        right_flat = r_values.reshape(-1).astype(np.float32)
        left_flat = left_flat - float(np.mean(left_flat))
        right_flat = right_flat - float(np.mean(right_flat))
        denom = float(np.linalg.norm(left_flat) * np.linalg.norm(right_flat))
        if denom <= 1.0e-6:
            continue
        corr = float(np.dot(left_flat, right_flat) / denom)
        if corr > best_corr:
            best_corr = corr
            best_lag = lag
    return (best_lag, best_corr)


def measure_flutter_from_trace(trace: dict[str, Any]) -> dict[str, Any]:
    if trace["confidence"] != "high":
        return {
            "confidence": trace["confidence"],
            "motionClass": "low-confidence",
            "rigidMotionAmplitudePx": 0.0,
            "flutterResidualRmsPx": 0.0,
            "flutterTemporalP95Px": 0.0,
            "travelLagFrames": 0,
            "travelCorrelation": 0.0,
        }

    values = _dense_trace(trace["edgeY"])
    rigid = np.median(values, axis=1)
    residual = values - rigid[:, None]
    temporal_residual = residual - np.mean(residual, axis=0, keepdims=True)
    flutter_rms = float(np.sqrt(np.mean(temporal_residual * temporal_residual)))
    temporal_std = np.std(temporal_residual, axis=0)
    flutter_p95 = float(np.percentile(temporal_std, 95.0))
    rigid_amp = float(np.percentile(rigid, 95.0) - np.percentile(rigid, 5.0))
    left_column = temporal_residual[:, max(0, temporal_residual.shape[1] // 4)]
    right_column = temporal_residual[:, min(temporal_residual.shape[1] - 1, temporal_residual.shape[1] * 3 // 4)]
    travel_lag, travel_corr = _best_lag_and_correlation(left_column, right_column)
    is_flutter = flutter_rms >= FLUTTER_RESIDUAL_THRESHOLD_PX
    return {
        "confidence": trace["confidence"],
        "motionClass": "flutter-wave" if is_flutter else "rigid-motion",
        "rigidMotionAmplitudePx": round(rigid_amp, 6),
        "flutterResidualRmsPx": round(flutter_rms, 6),
        "flutterTemporalP95Px": round(flutter_p95, 6),
        "travelLagFrames": int(travel_lag),
        "travelCorrelation": round(float(travel_corr), 6),
    }


def measure_flutter_sequence(directory: Path | str, roi: Iterable[int], *, sample_columns: int = 96) -> dict[str, Any]:
    trace = extract_edge_trace(directory, roi, sample_columns=sample_columns)
    result = measure_flutter_from_trace(trace)
    result["trace"] = trace
    return result


def classify_flutter_report(report: dict[str, Any]) -> str:
    windows = report["sequences"]["windows"]["flutter"]
    yakkai = report["sequences"]["normal-yakkai"]["flutter"]
    if windows["confidence"] != "high":
        return "reference-alignment-insufficient"
    if yakkai["confidence"] != "high":
        return "roi-or-edge-trace-ambiguous"

    windows_flutter = windows["motionClass"] == "flutter-wave"
    yakkai_flutter = yakkai["motionClass"] == "flutter-wave"
    if windows_flutter and not yakkai_flutter:
        return "windows-flutter-yakkai-rigid"
    if not windows_flutter and yakkai_flutter:
        return "roi-or-edge-trace-ambiguous"
    if not windows_flutter and not yakkai_flutter:
        return "no-flutter-mismatch-reproduced"

    windows_amp = float(windows["flutterResidualRmsPx"])
    yakkai_amp = float(yakkai["flutterResidualRmsPx"])
    amp_delta = abs(windows_amp - yakkai_amp) / max(windows_amp, yakkai_amp, 1.0e-6)
    if amp_delta >= AMPLITUDE_RATIO_THRESHOLD:
        return "windows-and-yakkai-flutter-different-amplitude"

    residual_lag, residual_corr = _best_residual_lag_and_correlation(windows, yakkai)
    if abs(residual_lag) >= PHASE_LAG_THRESHOLD_FRAMES or residual_corr < PHASE_CORRELATION_THRESHOLD:
        return "windows-and-yakkai-flutter-different-phase"

    lag_delta = abs(int(windows["travelLagFrames"]) - int(yakkai["travelLagFrames"]))
    corr_min = min(float(windows["travelCorrelation"]), float(yakkai["travelCorrelation"]))
    if lag_delta >= PHASE_LAG_THRESHOLD_FRAMES or corr_min < PHASE_CORRELATION_THRESHOLD:
        return "windows-and-yakkai-flutter-different-phase"
    return "no-flutter-mismatch-reproduced"


def _load_flutter_config(path: Path) -> tuple[dict[str, Any], tuple[int, int], int]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    regions = data.get("regions", {})
    if "lower_ribbon_edge" not in regions:
        raise ValueError("region config must contain regions.lower_ribbon_edge")
    base_size_data = data.get("baseSize", [1600, 900])
    base_size = (int(base_size_data[0]), int(base_size_data[1]))
    sample_columns = int(data.get("sampleColumns", 128))
    return regions, base_size, sample_columns


def write_kymograph(flutter: dict[str, Any], output: Path) -> None:
    trace = _dense_trace(flutter["trace"]["edgeY"])
    if trace.size == 0:
        image = np.zeros((1, 1), dtype=np.uint8)
    else:
        rigid = np.median(trace, axis=1)
        residual = trace - rigid[:, None]
        residual = residual - np.mean(residual, axis=0, keepdims=True)
        max_abs = max(float(np.max(np.abs(residual))), 1.0)
        normalized = np.clip((residual / max_abs) * 0.5 + 0.5, 0.0, 1.0)
        image = np.round(normalized * 255.0).astype(np.uint8)
    output.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(image, "L").resize((image.shape[1] * 4, image.shape[0] * 4), Image.Resampling.NEAREST).save(output)


def write_report_markdown(report: dict[str, Any], output: Path) -> None:
    lines = [
        "# Arona Lower Ribbon Flutter Parity Report",
        "",
        f"Classification: `{report['classification']}`",
        "",
        "| Sequence | Motion class | Rigid amp px | Flutter RMS px | Flutter p95 px | Travel lag | Travel corr |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for label, sequence in report["sequences"].items():
        flutter = sequence["flutter"]
        lines.append(
            "| "
            f"{label} | {flutter['motionClass']} | "
            f"{flutter['rigidMotionAmplitudePx']:.3f} | "
            f"{flutter['flutterResidualRmsPx']:.3f} | "
            f"{flutter['flutterTemporalP95Px']:.3f} | "
            f"{flutter['travelLagFrames']} | "
            f"{flutter['travelCorrelation']:.3f} |"
        )
    lines.extend([
        "",
        "## Artifacts",
        "",
        "- `flutter-report.json`",
        "- `flutter-report.md`",
        "- `kymographs/`",
        "",
    ])
    output.write_text("\n".join(lines), encoding="utf-8")


def command_flutter_report(args: argparse.Namespace) -> int:
    regions, base_size, sample_columns = _load_flutter_config(Path(args.regions))
    edge_region = regions["lower_ribbon_edge"]
    sequences: dict[str, Path] = {
        "windows": Path(args.windows_dir),
        "normal-yakkai": Path(args.normal_yakkai_dir),
    }
    for item in args.prefix:
        if "=" not in item:
            raise ValueError(f"--prefix expects LABEL=DIR, got {item}")
        label, directory = item.split("=", 1)
        sequences[label] = Path(directory)

    measured: dict[str, Any] = {}
    for label, directory in sequences.items():
        first = open_indexed_frame(frame_paths(directory), 0)
        roi = scale_roi(edge_region["roi"], from_size=base_size, to_size=first.size)
        flutter = measure_flutter_sequence(directory, roi, sample_columns=sample_columns)
        measured[label] = {"directory": str(directory), "roi": roi, "flutter": flutter}

    report = {
        "classification": classify_flutter_report({"sequences": measured}),
        "regions": regions,
        "roiBaseSize": list(base_size),
        "sampleColumns": sample_columns,
        "sequences": measured,
    }
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "flutter-report.json").write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
    write_report_markdown(report, output_dir / "flutter-report.md")
    kymograph_dir = output_dir / "kymographs"
    for label, sequence in measured.items():
        write_kymograph(sequence["flutter"], kymograph_dir / f"{label}.png")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Compare lower-ribbon flutter edge deformation across frame sequences.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    report = subparsers.add_parser("flutter-report")
    report.add_argument("--regions", required=True)
    report.add_argument("--windows-dir", required=True)
    report.add_argument("--normal-yakkai-dir", required=True)
    report.add_argument("--prefix", action="append", default=[])
    report.add_argument("--output-dir", required=True)
    report.set_defaults(func=command_flutter_report)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
