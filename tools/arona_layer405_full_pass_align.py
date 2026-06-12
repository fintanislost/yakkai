#!/usr/bin/env python3
from __future__ import annotations

import argparse
import io
import json
import math
import zipfile
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image


REQUIRED_VARIANTS = ("day", "sunset", "night")
ROOT_NAME = "layer405_full_pass_export"


class FullPassAlignmentError(RuntimeError):
    pass


def zip_root(zip_file: zipfile.ZipFile) -> str:
    roots = sorted({name.split("/", 1)[0] for name in zip_file.namelist() if "/" in name})
    if ROOT_NAME not in roots:
        raise FullPassAlignmentError(f"archive root {ROOT_NAME} not found")
    return ROOT_NAME


def load_manifest(zip_file: zipfile.ZipFile, root: str) -> dict[str, Any]:
    try:
        manifest = json.loads(zip_file.read(f"{root}/source_manifest.json").decode("utf-8"))
    except KeyError as error:
        raise FullPassAlignmentError("source_manifest.json missing from full pass archive") from error
    status = str(manifest.get("status", ""))
    if not status.startswith("complete_full_layer405_pass_export"):
        raise FullPassAlignmentError(f"unexpected full pass manifest status: {status}")
    captures = {capture.get("variant"): capture for capture in manifest.get("captures", [])}
    missing = [variant for variant in REQUIRED_VARIANTS if variant not in captures]
    if missing:
        raise FullPassAlignmentError(f"missing required variants: {', '.join(missing)}")
    return manifest


def discover_variant_capture_dir(yakkai_root: Path, variant: str) -> Path:
    exact = yakkai_root / variant / "effect-captures" / "3228578419" / "405_ARONA_CROP_SHEET"
    if exact.is_dir():
        return exact
    candidates = sorted(
        path / "effect-captures" / "3228578419" / "405_ARONA_CROP_SHEET"
        for path in yakkai_root.iterdir()
        if path.is_dir() and path.name.startswith(variant)
    )
    for candidate in candidates:
        if candidate.is_dir():
            return candidate
    raise FullPassAlignmentError(f"{variant} Yakkai effect-capture directory not found under {yakkai_root}")


def rgba_array_from_zip(zip_file: zipfile.ZipFile, name: str, size: tuple[int, int]) -> np.ndarray:
    with Image.open(io.BytesIO(zip_file.read(name))) as image:
        rgba_image = image.convert("RGBA").resize(size, Image.Resampling.BILINEAR)
        return np.asarray(rgba_image, dtype=np.float32) / 255.0


def rgba_array(path: Path, size: tuple[int, int]) -> np.ndarray:
    with Image.open(path) as image:
        rgba_image = image.convert("RGBA").resize(size, Image.Resampling.BILINEAR)
        return np.asarray(rgba_image, dtype=np.float32) / 255.0


def rmse(left: np.ndarray, right: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(left - right))))


def alpha_weighted_rmse(left: np.ndarray, right: np.ndarray) -> float:
    alpha = np.maximum(left[..., 3:4], right[..., 3:4])
    denominator = max(float(np.sum(alpha)) * 3.0, 1.0)
    return float(np.sqrt(np.sum(np.square(left[..., :3] - right[..., :3]) * alpha) / denominator))


def delta_metrics(windows_before: np.ndarray, windows_after: np.ndarray, yakkai_before: np.ndarray, yakkai_after: np.ndarray) -> dict[str, float]:
    windows_delta = windows_after - windows_before
    yakkai_delta = yakkai_after - yakkai_before
    windows_rgb = windows_delta[..., :3].reshape(-1)
    yakkai_rgb = yakkai_delta[..., :3].reshape(-1)
    windows_norm = float(np.linalg.norm(windows_rgb))
    yakkai_norm = float(np.linalg.norm(yakkai_rgb))
    if windows_norm <= 1e-8 and yakkai_norm <= 1e-8:
        delta_cosine = 1.0
    elif windows_norm <= 1e-8 or yakkai_norm <= 1e-8:
        delta_cosine = 0.0
    else:
        delta_cosine = float(np.dot(windows_rgb, yakkai_rgb) / (windows_norm * yakkai_norm))
        delta_cosine = min(1.0, max(-1.0, delta_cosine))
    return {
        "deltaRgbRmse": rmse(windows_delta[..., :3], yakkai_delta[..., :3]),
        "deltaAlphaRmse": rmse(windows_delta[..., 3], yakkai_delta[..., 3]),
        "deltaCosine": delta_cosine,
        "windowsDeltaNorm": windows_norm / math.sqrt(windows_rgb.size),
        "yakkaiDeltaNorm": yakkai_norm / math.sqrt(yakkai_rgb.size),
    }


def yakkai_stage_for_pass(capture_dir: Path, pass_order: int) -> tuple[str, Path]:
    if pass_order == 1:
        return "effect-input", capture_dir / "effect_input.tga"
    effect_index = pass_order - 1
    return f"material-output-{effect_index}-0", capture_dir / f"material_output_{effect_index}_0.tga"


def classify_row(row: dict[str, Any]) -> str:
    if row.get("missingPath"):
        return "missing-yakkai-capture"
    if row["rmse"] <= 0.025 or row["alphaWeightedRmse"] <= 0.005:
        return "close"
    if row["rmse"] <= 0.075 or row["alphaWeightedRmse"] <= 0.025:
        return "drift"
    return "mismatch"


def align_full_pass_export(
    windows_archive: Path,
    yakkai_root: Path,
    output_dir: Path | None = None,
    max_size: tuple[int, int] = (512, 360),
) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    variant_dirs: dict[str, str] = {}
    with zipfile.ZipFile(windows_archive) as zip_file:
        root = zip_root(zip_file)
        manifest = load_manifest(zip_file, root)
        captures = {capture["variant"]: capture for capture in manifest["captures"]}
        for variant in REQUIRED_VARIANTS:
            capture = captures[variant]
            capture_dir = discover_variant_capture_dir(yakkai_root, variant)
            variant_dirs[variant] = str(capture_dir)
            previous_windows = None
            previous_yakkai = None
            for pass_entry in capture["passes"]:
                order = int(pass_entry["passOrder"])
                stage, yakkai_path = yakkai_stage_for_pass(capture_dir, order)
                row: dict[str, Any] = {
                    "variant": variant,
                    "passOrder": order,
                    "replayEventId": int(pass_entry["replayEventId"]),
                    "drawId": int(pass_entry["drawId"]),
                    "role": pass_entry.get("role"),
                    "pixelShaderResourceId": int(pass_entry["pixelShaderResourceId"]),
                    "windowsRtvAfterFile": pass_entry["rtvAfterFile"],
                    "yakkaiStage": stage,
                    "yakkaiPath": str(yakkai_path),
                }
                windows_image = rgba_array_from_zip(zip_file, f"{root}/{pass_entry['rtvAfterFile']}", max_size)
                if not yakkai_path.is_file():
                    row["missingPath"] = str(yakkai_path)
                    row["classification"] = classify_row(row)
                    rows.append(row)
                    previous_windows = windows_image
                    previous_yakkai = None
                    continue
                yakkai_image = rgba_array(yakkai_path, max_size)
                row.update(
                    {
                        "rmse": rmse(windows_image, yakkai_image),
                        "rgbRmse": rmse(windows_image[..., :3], yakkai_image[..., :3]),
                        "alphaRmse": rmse(windows_image[..., 3], yakkai_image[..., 3]),
                        "alphaWeightedRmse": alpha_weighted_rmse(windows_image, yakkai_image),
                    }
                )
                if previous_windows is not None and previous_yakkai is not None:
                    row.update(delta_metrics(previous_windows, windows_image, previous_yakkai, yakkai_image))
                row["classification"] = classify_row(row)
                rows.append(row)
                previous_windows = windows_image
                previous_yakkai = yakkai_image
    report = {
        "status": "complete",
        "windowsArchive": str(windows_archive),
        "yakkaiRoot": str(yakkai_root),
        "comparisonDimensions": list(max_size),
        "variantCaptureDirs": variant_dirs,
        "rows": rows,
    }
    if output_dir is not None:
        write_report(report, output_dir)
    return report


def fmt(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, float):
        return f"{value:.6f}"
    return str(value)


def write_report(report: dict[str, Any], output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "alignment.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    lines = [
        "# Arona Layer 405 Full Pass Alignment",
        "",
        f"Windows archive: `{report['windowsArchive']}`",
        f"Yakkai root: `{report['yakkaiRoot']}`",
        f"Comparison dimensions: `{report['comparisonDimensions'][0]}x{report['comparisonDimensions'][1]}`",
        "",
    ]
    for variant in REQUIRED_VARIANTS:
        lines.append(f"## {variant}")
        lines.append("| order | event | shaderRes | Yakkai stage | class | RMSE | alpha-weighted | alpha RMSE | delta RMSE | delta cosine |")
        lines.append("| ---: | ---: | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: |")
        for row in [entry for entry in report["rows"] if entry["variant"] == variant]:
            lines.append(
                "| {order} | {event} | {shader} | {stage} | {classification} | {rmse} | {alpha_weighted} | {alpha_rmse} | {delta_rmse} | {delta_cosine} |".format(
                    order=row["passOrder"],
                    event=row["replayEventId"],
                    shader=row["pixelShaderResourceId"],
                    stage=row["yakkaiStage"],
                    classification=row["classification"],
                    rmse=fmt(row.get("rmse")),
                    alpha_weighted=fmt(row.get("alphaWeightedRmse")),
                    alpha_rmse=fmt(row.get("alphaRmse")),
                    delta_rmse=fmt(row.get("deltaRgbRmse")),
                    delta_cosine=fmt(row.get("deltaCosine")),
                )
            )
        lines.append("")
    (output_dir / "alignment.md").write_text("\n".join(lines), encoding="utf-8")


def parse_size(value: str) -> tuple[int, int]:
    if "x" not in value:
        raise argparse.ArgumentTypeError("expected WIDTHxHEIGHT")
    width, height = value.split("x", 1)
    try:
        parsed = (int(width), int(height))
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected integer WIDTHxHEIGHT") from error
    if parsed[0] <= 0 or parsed[1] <= 0:
        raise argparse.ArgumentTypeError("width and height must be positive")
    return parsed


def main() -> int:
    parser = argparse.ArgumentParser(description="Align Windows Layer 405 full-pass exports to Yakkai effect captures.")
    parser.add_argument("--windows", required=True, type=Path, help="Path to layer405_full_pass_export.zip")
    parser.add_argument("--yakkai-root", required=True, type=Path, help="Root containing per-variant Yakkai effect-captures")
    parser.add_argument("--output", required=True, type=Path, help="Directory for alignment.json and alignment.md")
    parser.add_argument("--max-size", default=(512, 360), type=parse_size, help="Comparison resize, default 512x360")
    args = parser.parse_args()
    align_full_pass_export(args.windows, args.yakkai_root, args.output, args.max_size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
