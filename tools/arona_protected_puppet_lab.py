#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
from typing import Any

import numpy as np
from PIL import Image


MAX_COMPARE_SIZE = 512
FINAL_DISPLAY_DELTA_THRESHOLDS_RGB255 = (1, 4, 8, 16)
FINAL_DISPLAY_MIN_VISIBLE_FRACTION = 0.0001


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{path} is not a JSON object")
    return data


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def layer_from_record(record: dict[str, Any]) -> dict[str, Any]:
    layer = record.get("layer")
    return layer if isinstance(layer, dict) else record


def layer_id(record: dict[str, Any]) -> int | None:
    value = layer_from_record(record).get("layerId", record.get("layerId"))
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def first_matching_layer(records: list[Any], target_layer_id: int) -> dict[str, Any] | None:
    for record in records:
        if isinstance(record, dict) and layer_id(record) == target_layer_id:
            return record
    return None


def rounded(value: float | None) -> float | None:
    if value is None:
        return None
    return round(float(value), 6)


def rounded_rgb(values: np.ndarray | list[float] | None, *, scale: float = 1.0) -> list[float] | None:
    if values is None:
        return None
    out: list[float] = []
    for value in values[:3]:
        scaled = float(value) * scale
        nearest = round(scaled)
        if abs(scaled - nearest) < 1.0e-4:
            out.append(float(nearest))
        else:
            out.append(round(scaled, 6))
    return out


def load_rgba(path: Path, size: tuple[int, int] | None = None) -> np.ndarray:
    with Image.open(path).convert("RGBA") as image:
        if size is not None and image.size != size:
            image = image.resize(size, Image.Resampling.BILINEAR)
        return np.asarray(image, dtype=np.float32) / 255.0


def image_size(path: Path) -> tuple[int, int]:
    with Image.open(path) as image:
        return image.size


def alpha_bounds_pixels(alpha: np.ndarray) -> list[int] | None:
    ys, xs = np.nonzero(alpha > 0.0)
    if len(xs) == 0 or len(ys) == 0:
        return None
    return [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]


def alpha_bounds_norm(bounds: list[int] | None, width: int, height: int) -> list[float] | None:
    if bounds is None or width <= 0 or height <= 0:
        return None
    x1, y1, x2, y2 = bounds
    return [
        rounded(x1 / width) or 0.0,
        rounded(y1 / height) or 0.0,
        rounded((x2 + 1) / width) or 0.0,
        rounded((y2 + 1) / height) or 0.0,
    ]


def alpha_centroid_norm(alpha: np.ndarray) -> list[float] | None:
    total = float(np.sum(alpha))
    if total <= 1.0e-8:
        return None
    ys, xs = np.indices(alpha.shape, dtype=np.float32)
    height, width = alpha.shape
    return [
        rounded(float(np.sum(xs * alpha) / total) / max(width - 1, 1)) or 0.0,
        rounded(float(np.sum(ys * alpha) / total) / max(height - 1, 1)) or 0.0,
    ]


def alpha_weighted_mean_rgb(pixels: np.ndarray, *, scale: float = 255.0) -> list[float]:
    rgb = pixels[..., :3]
    alpha = pixels[..., 3]
    total_alpha = float(np.sum(alpha))
    if total_alpha <= 0.0:
        mean = np.mean(rgb.reshape((-1, 3)), axis=0)
    else:
        mean = np.sum(rgb * alpha[..., None], axis=(0, 1)) / total_alpha
    return rounded_rgb(mean, scale=scale) or [0.0, 0.0, 0.0]


def image_stats(path: Path) -> dict[str, Any]:
    pixels = load_rgba(path)
    height, width, _ = pixels.shape
    alpha = pixels[..., 3]
    visible = alpha > 0.0
    opaque = alpha >= 0.95
    bounds = alpha_bounds_pixels(alpha)
    return {
        "dimensions": [width, height],
        "visibleFraction": rounded(float(np.count_nonzero(visible) / alpha.size)),
        "opaqueFraction": rounded(float(np.count_nonzero(opaque) / alpha.size)),
        "alphaBoundsPixels": bounds,
        "alphaBoundsNorm": alpha_bounds_norm(bounds, width, height),
        "alphaCentroidNorm": alpha_centroid_norm(alpha),
        "alphaWeightedMeanRgb": alpha_weighted_mean_rgb(pixels),
    }


def image_delta_stats(
    before_path: Path,
    after_path: Path,
    *,
    threshold_rgb255: int = 1,
) -> dict[str, Any]:
    size = fit_compare_size(before_path, after_path)
    before = load_rgba(before_path, size)
    after = load_rgba(after_path, size)
    delta = np.max(np.abs(after - before), axis=2)
    threshold = max(0, int(threshold_rgb255)) / 255.0
    mask = delta > threshold
    mask_alpha = mask.astype(np.float32)
    height, width = mask_alpha.shape
    bounds = alpha_bounds_pixels(mask_alpha)
    return {
        "dimensions": [width, height],
        "thresholdRgb255": int(threshold_rgb255),
        "threshold": rounded(threshold),
        "visibleFraction": rounded(float(np.count_nonzero(mask) / mask.size)),
        "alphaBoundsPixels": bounds,
        "alphaBoundsNorm": alpha_bounds_norm(bounds, width, height),
        "alphaCentroidNorm": alpha_centroid_norm(mask_alpha),
    }


def capture_record(record: dict[str, Any]) -> dict[str, Any]:
    info = record.get("renderTargetInfo")
    if not isinstance(info, dict):
        info = {}
    capture = {
        "stage": str(record.get("stage", "")),
        "path": str(record.get("path", "")),
        "renderTarget": str(record.get("renderTarget", "")),
        "width": int(info.get("width", 0) or 0),
        "height": int(info.get("height", 0) or 0),
    }
    path = Path(capture["path"])
    if capture["path"] and path.exists():
        try:
            capture["imageStats"] = image_stats(path)
        except Exception as exc:
            capture["imageStatsError"] = str(exc)
    return capture


def common_compare_size(left: Path, right: Path, max_size: int = MAX_COMPARE_SIZE) -> tuple[int, int]:
    left_width, left_height = image_size(left)
    right_width, right_height = image_size(right)
    width = max(1, min(max(left_width, right_width), max_size))
    height = max(1, min(max(left_height, right_height), max_size))
    return (width, height)


def fit_compare_size(left: Path, right: Path, max_size: int = MAX_COMPARE_SIZE) -> tuple[int, int]:
    left_width, left_height = image_size(left)
    right_width, right_height = image_size(right)
    width = max(left_width, right_width)
    height = max(left_height, right_height)
    if width <= 0 or height <= 0:
        return (1, 1)
    scale = min(max_size / width, max_size / height, 1.0)
    return (max(1, round(width * scale)), max(1, round(height * scale)))


def rmse(left: np.ndarray, right: np.ndarray) -> float:
    diff = left.astype(np.float32) - right.astype(np.float32)
    return float(np.sqrt(np.mean(diff * diff)))


def gradient_magnitude(gray: np.ndarray) -> np.ndarray:
    gx = np.zeros_like(gray, dtype=np.float32)
    gy = np.zeros_like(gray, dtype=np.float32)
    gx[:, 1:-1] = gray[:, 2:] - gray[:, :-2]
    gy[1:-1, :] = gray[2:, :] - gray[:-2, :]
    return np.sqrt(gx * gx + gy * gy)


def visual_saliency(pixels: np.ndarray) -> np.ndarray:
    rgb = pixels[..., :3]
    gray = np.mean(rgb, axis=2)
    chroma = np.std(rgb, axis=2)
    contrast = np.abs(gray - float(np.median(gray)))
    return gradient_magnitude(gray) + 0.25 * chroma + 0.10 * contrast


def weighted_centroid(weights: np.ndarray) -> list[float] | None:
    total = float(np.sum(weights))
    if total <= 1.0e-8:
        return None
    ys, xs = np.indices(weights.shape, dtype=np.float32)
    return [
        float(np.sum(xs * weights) / total),
        float(np.sum(ys * weights) / total),
    ]


def screen_space_drift(probe_path: Path, normal_path: Path, max_size: int = MAX_COMPARE_SIZE) -> dict[str, Any]:
    probe_width, probe_height = image_size(probe_path)
    size = fit_compare_size(probe_path, normal_path, max_size=max_size)
    probe = load_rgba(probe_path, size)
    normal = load_rgba(normal_path, size)
    diff = np.mean(np.abs(probe[..., :3] - normal[..., :3]), axis=2)
    threshold = max(0.08, float(np.percentile(diff, 80)))
    changed = diff >= threshold
    coverage = float(np.count_nonzero(changed) / changed.size)
    probe_weights = np.where(changed, visual_saliency(probe), 0.0)
    normal_weights = np.where(changed, visual_saliency(normal), 0.0)
    probe_centroid = weighted_centroid(probe_weights)
    normal_centroid = weighted_centroid(normal_weights)
    if probe_centroid is None or normal_centroid is None:
        return {
            "classification": "insufficient-signal",
            "compareSize": [size[0], size[1]],
            "differenceThreshold": rounded(threshold),
            "differenceCoverage": rounded(coverage),
        }
    scale_x = probe_width / size[0]
    scale_y = probe_height / size[1]
    delta = [
        (probe_centroid[0] - normal_centroid[0]) * scale_x,
        (probe_centroid[1] - normal_centroid[1]) * scale_y,
    ]
    magnitude = float(np.sqrt(delta[0] * delta[0] + delta[1] * delta[1]))
    classification = "screen-space-drift" if magnitude >= 24.0 and coverage >= 0.005 else "screen-space-stable"
    return {
        "classification": classification,
        "compareSize": [size[0], size[1]],
        "probeCentroid": [rounded(probe_centroid[0]), rounded(probe_centroid[1])],
        "normalCentroid": [rounded(normal_centroid[0]), rounded(normal_centroid[1])],
        "centroidDeltaPixels": [rounded(delta[0]), rounded(delta[1])],
        "driftMagnitudePixels": rounded(magnitude),
        "differenceThreshold": rounded(threshold),
        "differenceCoverage": rounded(coverage),
    }


def bounds_iou(left: list[float] | None, right: list[float] | None) -> float:
    if left is None and right is None:
        return 1.0
    if left is None or right is None:
        return 0.0
    left_x1, left_y1, left_x2, left_y2 = left
    right_x1, right_y1, right_x2, right_y2 = right
    intersection_width = max(0.0, min(left_x2, right_x2) - max(left_x1, right_x1))
    intersection_height = max(0.0, min(left_y2, right_y2) - max(left_y1, right_y1))
    intersection = intersection_width * intersection_height
    left_area = max(0.0, left_x2 - left_x1) * max(0.0, left_y2 - left_y1)
    right_area = max(0.0, right_x2 - right_x1) * max(0.0, right_y2 - right_y1)
    union = left_area + right_area - intersection
    if union <= 0.0:
        return 1.0
    return intersection / union


def compare_image_paths(left_path: Path, right_path: Path, max_size: int = MAX_COMPARE_SIZE) -> dict[str, Any]:
    left_dimensions = list(image_size(left_path))
    right_dimensions = list(image_size(right_path))
    size = common_compare_size(left_path, right_path, max_size=max_size)
    left = load_rgba(left_path, size)
    right = load_rgba(right_path, size)
    left_stats = image_stats(left_path)
    right_stats = image_stats(right_path)
    left_mean = np.array(alpha_weighted_mean_rgb(left, scale=1.0), dtype=np.float32)
    right_mean = np.array(alpha_weighted_mean_rgb(right, scale=1.0), dtype=np.float32)
    return {
        "rmse": rounded(rmse(left[..., :3], right[..., :3])),
        "alphaRmse": rounded(rmse(left[..., 3], right[..., 3])),
        "leftDimensions": left_dimensions,
        "rightDimensions": right_dimensions,
        "compareSize": [size[0], size[1]],
        "visibleFractionDelta": rounded(
            float(right_stats.get("visibleFraction") or 0.0)
            - float(left_stats.get("visibleFraction") or 0.0)
        ),
        "opaqueFractionDelta": rounded(
            float(right_stats.get("opaqueFraction") or 0.0)
            - float(left_stats.get("opaqueFraction") or 0.0)
        ),
        "alphaBoundsIou": rounded(
            bounds_iou(left_stats.get("alphaBoundsNorm"), right_stats.get("alphaBoundsNorm"))
        ),
        "meanRgbDelta": rounded_rgb(right_mean - left_mean, scale=1.0),
    }


def classify_boundary(comparison: dict[str, Any]) -> str:
    if comparison.get("boundary") == "final-display-before->final-display-after":
        if float(comparison.get("rmse") or 0.0) > 0.001 or float(comparison.get("alphaRmse") or 0.0) > 0.001:
            return "final-display-contribution"
        return "no-final-display-contribution"
    if (
        comparison.get("boundary") == "effect-output->final-publish"
        and comparison.get("leftDimensions") != comparison.get("rightDimensions")
    ):
        return "local-to-frame-composition"
    alpha_iou = float(comparison.get("alphaBoundsIou") or 0.0)
    visible_delta = abs(float(comparison.get("visibleFractionDelta") or 0.0))
    opaque_delta = abs(float(comparison.get("opaqueFractionDelta") or 0.0))
    mean_delta = comparison.get("meanRgbDelta")
    max_mean_delta = max(abs(float(value)) for value in mean_delta) if isinstance(mean_delta, list) else 0.0
    if alpha_iou < 0.85 or visible_delta > 0.05 or opaque_delta > 0.05:
        return "alpha-or-bounds-drift"
    if float(comparison.get("rmse") or 0.0) > 0.08 or max_mean_delta > 0.08:
        return "color-drift"
    return "stable"


def capture_by_stage(captures: list[dict[str, Any]], stage: str) -> dict[str, Any] | None:
    for capture in captures:
        if capture.get("stage") == stage and capture.get("path"):
            return capture
    return None


def ratio(numerator: float | None, denominator: float | None) -> float | None:
    if numerator is None or denominator is None or denominator <= 1.0e-8:
        return None
    return rounded(numerator / denominator)


def centroid_distance(left: list[Any] | None, right: list[Any] | None) -> float | None:
    if not isinstance(left, list) or not isinstance(right, list) or len(left) < 2 or len(right) < 2:
        return None
    dx = float(right[0]) - float(left[0])
    dy = float(right[1]) - float(left[1])
    return rounded(float(np.sqrt(dx * dx + dy * dy)))


def final_display_alignment(
    output_stats: dict[str, Any],
    delta_stats: dict[str, Any],
) -> dict[str, Any]:
    output_visible = float(output_stats.get("visibleFraction") or 0.0)
    delta_visible = float(delta_stats.get("visibleFraction") or 0.0)
    visible_ratio = ratio(delta_visible, output_visible)
    bounds_iou_value = rounded(
        bounds_iou(output_stats.get("alphaBoundsNorm"), delta_stats.get("alphaBoundsNorm"))
    )
    drift = centroid_distance(
        output_stats.get("alphaCentroidNorm"),
        delta_stats.get("alphaCentroidNorm"),
    )

    classification = "missing-final-display-alignment"
    if visible_ratio is not None and bounds_iou_value is not None and drift is not None:
        if visible_ratio < 0.75:
            classification = "final-display-coverage-loss"
        elif visible_ratio > 1.35 or bounds_iou_value < 0.75 or drift > 0.05:
            classification = "final-display-shape-drift"
        else:
            classification = "final-display-aligned"

    return {
        "classification": classification,
        "outputVisibleFraction": rounded(output_visible),
        "deltaVisibleFraction": rounded(delta_visible),
        "deltaToOutputVisibleRatio": visible_ratio,
        "outputBoundsNorm": output_stats.get("alphaBoundsNorm"),
        "deltaBoundsNorm": delta_stats.get("alphaBoundsNorm"),
        "boundsIou": bounds_iou_value,
        "outputCentroid": output_stats.get("alphaCentroidNorm"),
        "deltaCentroid": delta_stats.get("alphaCentroidNorm"),
        "centroidDrift": drift,
    }


def bounds_extent(bounds: list[Any] | None) -> tuple[float, float] | None:
    if not isinstance(bounds, list) or len(bounds) < 4:
        return None
    width = float(bounds[2]) - float(bounds[0])
    height = float(bounds[3]) - float(bounds[1])
    if width <= 1.0e-8 or height <= 1.0e-8:
        return None
    return (width, height)


def project_centroid_between_bounds(
    centroid: list[Any] | None,
    from_bounds: list[Any] | None,
    to_bounds: list[Any] | None,
) -> list[float] | None:
    from_extent = bounds_extent(from_bounds)
    to_extent = bounds_extent(to_bounds)
    if (
        not isinstance(centroid, list)
        or len(centroid) < 2
        or from_extent is None
        or to_extent is None
        or not isinstance(from_bounds, list)
        or not isinstance(to_bounds, list)
        or len(from_bounds) < 4
        or len(to_bounds) < 4
    ):
        return None

    local_x = (float(centroid[0]) - float(from_bounds[0])) / from_extent[0]
    local_y = (float(centroid[1]) - float(from_bounds[1])) / from_extent[1]
    return [
        rounded(float(to_bounds[0]) + local_x * to_extent[0]) or 0.0,
        rounded(float(to_bounds[1]) + local_y * to_extent[1]) or 0.0,
    ]


def final_display_screen_space_projection(
    output_stats: dict[str, Any],
    delta_stats: dict[str, Any],
) -> dict[str, Any]:
    projected_centroid = project_centroid_between_bounds(
        output_stats.get("alphaCentroidNorm"),
        output_stats.get("alphaBoundsNorm"),
        delta_stats.get("alphaBoundsNorm"),
    )
    delta_centroid = delta_stats.get("alphaCentroidNorm")
    drift = centroid_distance(projected_centroid, delta_centroid)

    classification = "missing-final-display-screen-projection"
    if projected_centroid is not None and isinstance(delta_centroid, list) and drift is not None:
        if drift <= 0.03:
            classification = "screen-space-affine-consistent"
        else:
            classification = "screen-space-affine-drift"

    return {
        "classification": classification,
        "comparison": "output-bounds-centroid-projected-to-final-display-delta-bounds",
        "projectedOutputCentroid": projected_centroid,
        "deltaCentroid": delta_centroid,
        "projectedCentroidDrift": drift,
        "outputBoundsNorm": output_stats.get("alphaBoundsNorm"),
        "deltaBoundsNorm": delta_stats.get("alphaBoundsNorm"),
    }


def final_display_delta_threshold_sweep(
    before_path: Path,
    after_path: Path,
    output_stats: dict[str, Any],
) -> list[dict[str, Any]]:
    sweep: list[dict[str, Any]] = []
    for threshold_rgb255 in FINAL_DISPLAY_DELTA_THRESHOLDS_RGB255:
        delta_stats = image_delta_stats(
            before_path,
            after_path,
            threshold_rgb255=threshold_rgb255,
        )
        sweep.append({
            "thresholdRgb255": threshold_rgb255,
            "threshold": delta_stats.get("threshold"),
            "deltaVisibleFraction": delta_stats.get("visibleFraction"),
            "deltaBoundsNorm": delta_stats.get("alphaBoundsNorm"),
            "deltaCentroid": delta_stats.get("alphaCentroidNorm"),
            "alignment": final_display_alignment(output_stats, delta_stats),
        })
    return sweep


def final_display_threshold_sensitivity(sweep: list[dict[str, Any]]) -> dict[str, Any]:
    aligned: list[int] = []
    shape_drift: list[int] = []
    coverage_loss: list[int] = []
    missing: list[int] = []
    visible: list[int] = []

    for entry in sweep:
        threshold = int(entry.get("thresholdRgb255") or 0)
        delta_visible = float(entry.get("deltaVisibleFraction") or 0.0)
        if delta_visible > FINAL_DISPLAY_MIN_VISIBLE_FRACTION:
            visible.append(threshold)
        alignment = entry.get("alignment")
        classification = ""
        if isinstance(alignment, dict):
            classification = str(alignment.get("classification") or "")
        if classification == "final-display-aligned":
            aligned.append(threshold)
        elif classification == "final-display-shape-drift":
            shape_drift.append(threshold)
        elif classification == "final-display-coverage-loss":
            coverage_loss.append(threshold)
        else:
            missing.append(threshold)

    if not sweep:
        classification = "missing-final-display-thresholds"
    elif not visible:
        classification = "weak-final-display-signal"
    elif shape_drift and aligned:
        classification = "threshold-sensitive-shape-drift"
    elif shape_drift:
        classification = "persistent-shape-drift"
    elif coverage_loss and aligned:
        classification = "threshold-sensitive-coverage-loss"
    elif coverage_loss:
        classification = "persistent-coverage-loss"
    elif aligned:
        classification = "threshold-stable-aligned"
    else:
        classification = "mixed-final-display-thresholds"

    return {
        "classification": classification,
        "visibleThresholdRgb255": visible,
        "alignedThresholdRgb255": aligned,
        "shapeDriftThresholdRgb255": shape_drift,
        "coverageLossThresholdRgb255": coverage_loss,
        "missingThresholdRgb255": missing,
    }


def active_slots_from_layer(layer: dict[str, Any], diagnostic: dict[str, Any] | None) -> list[str]:
    slots: list[str] = []

    def add_slot(value: Any) -> None:
        text = str(value)
        if text not in slots:
            slots.append(text)

    for source in (layer, diagnostic or {}):
        for animation in as_list(source.get("puppetAnimationLayers")):
            if not isinstance(animation, dict) or animation.get("visibleAndWeighted") is not True:
                continue
            for slot in as_list(animation.get("activeBoneSlots")):
                add_slot(slot)
        publish = publish_for(source, None)
        for slot_info in as_list(publish.get("puppetCutoutSlotCoverage")):
            if isinstance(slot_info, dict) and slot_info.get("active") is True:
                add_slot(slot_info.get("slot", "unknown"))
    return slots


def secondary_only_slots_from_layer(layer: dict[str, Any], diagnostic: dict[str, Any] | None) -> list[str]:
    slots: list[str] = []

    def add_slot(value: Any) -> None:
        text = str(value)
        if text not in slots:
            slots.append(text)

    for source in (layer, diagnostic or {}):
        publish = publish_for(source, None)
        for slot_info in as_list(publish.get("puppetCutoutSlotCoverage")):
            if not isinstance(slot_info, dict):
                continue
            primary_vertices = slot_info.get("primaryVertexCount", slot_info.get("vertexCount", 0))
            weighted_vertices = slot_info.get("weightedVertexCount", slot_info.get("vertexCount", 0))
            if slot_info.get("secondaryOnly") is True or (
                primary_vertices == 0 and weighted_vertices > 0
            ):
                add_slot(slot_info.get("slot", "unknown"))
    return slots


def final_coverage_diagnostics(
    captures: list[dict[str, Any]],
    layer: dict[str, Any],
    diagnostic: dict[str, Any] | None,
) -> dict[str, Any]:
    stages = {
        stage: capture_by_stage(captures, stage)
        for stage in (
            "effect-input",
            "effect-output",
            "final-display-before",
            "final-display-after",
            "final-publish",
        )
    }
    active_slots = active_slots_from_layer(layer, diagnostic)
    secondary_only_slots = secondary_only_slots_from_layer(layer, diagnostic)
    out: dict[str, Any] = {
        "classification": "missing-final-evidence",
        "source": "missing",
        "activeSlots": active_slots,
        "secondaryOnlySlots": secondary_only_slots,
    }

    output_capture = stages["effect-output"]
    before_capture = stages["final-display-before"]
    after_capture = stages["final-display-after"]
    if (
        output_capture is not None
        and isinstance(output_capture.get("imageStats"), dict)
        and before_capture is not None
        and after_capture is not None
    ):
        before_path = Path(str(before_capture.get("path", "")))
        after_path = Path(str(after_capture.get("path", "")))
        if before_path.exists() and after_path.exists():
            publish = publish_for(layer, diagnostic)
            output_stats = output_capture["imageStats"]
            delta_stats = image_delta_stats(before_path, after_path)
            delta_visible = float(delta_stats.get("visibleFraction") or 0.0)
            threshold_sweep = final_display_delta_threshold_sweep(
                before_path,
                after_path,
                output_stats,
            )
            classification = (
                "final-display-boundary-present"
                if delta_visible > FINAL_DISPLAY_MIN_VISIBLE_FRACTION
                else "final-display-no-visible-contribution"
            )
            out.update({
                "classification": classification,
                "source": "final-display-boundary",
                "outputVisibleFraction": output_stats.get("visibleFraction"),
                "outputCentroid": output_stats.get("alphaCentroidNorm"),
                "finalDisplayDeltaVisibleFraction": rounded(delta_visible),
                "finalDisplayDeltaBoundsNorm": delta_stats.get("alphaBoundsNorm"),
                "finalDisplayDeltaCentroid": delta_stats.get("alphaCentroidNorm"),
                "finalDisplayAlignment": final_display_alignment(output_stats, delta_stats),
                "finalDisplayScreenSpaceProjection": final_display_screen_space_projection(output_stats, delta_stats),
                "finalDisplayDeltaThresholdSweep": threshold_sweep,
                "finalDisplayThresholdSensitivity": final_display_threshold_sensitivity(threshold_sweep),
                "finalDisplayBoundaryTiming": publish.get("finalDisplayBoundaryCaptureTiming"),
                "finalDisplayBeforeRenderTarget": publish.get("finalDisplayBeforeRenderTarget"),
                "finalDisplayAfterRenderTarget": publish.get("finalDisplayAfterRenderTarget"),
            })
            return out

    publish_stages = {
        stage: stages[stage]
        for stage in ("effect-input", "effect-output", "final-publish")
    }
    if any(capture is None or not isinstance(capture.get("imageStats"), dict) for capture in publish_stages.values()):
        return out

    input_stats = stages["effect-input"]["imageStats"]
    output_stats = stages["effect-output"]["imageStats"]
    final_stats = stages["final-publish"]["imageStats"]
    input_visible = float(input_stats.get("visibleFraction") or 0.0)
    output_visible = float(output_stats.get("visibleFraction") or 0.0)
    final_visible = float(final_stats.get("visibleFraction") or 0.0)
    output_to_input = ratio(output_visible, input_visible)
    final_to_output = ratio(final_visible, output_visible)
    output_centroid = output_stats.get("alphaCentroidNorm")
    final_centroid = final_stats.get("alphaCentroidNorm")
    final_drift = centroid_distance(output_centroid, final_centroid)

    classification = "stable"
    if final_to_output is None:
        classification = "missing-final-evidence"
    elif final_to_output < 0.85:
        classification = "final-coverage-loss"
    elif final_drift is not None and final_drift > 0.03:
        classification = "final-screen-drift"

    out.update({
        "classification": classification,
        "source": "final-publish",
        "inputVisibleFraction": rounded(input_visible),
        "outputVisibleFraction": rounded(output_visible),
        "finalVisibleFraction": rounded(final_visible),
        "outputToInputAlphaRatio": output_to_input,
        "finalToOutputAlphaRatio": final_to_output,
        "inputCentroid": input_stats.get("alphaCentroidNorm"),
        "outputCentroid": output_centroid,
        "finalCentroid": final_centroid,
        "finalCentroidDrift": final_drift,
    })
    return out


def boundary_comparisons(captures: list[dict[str, Any]]) -> list[dict[str, Any]]:
    comparisons: list[dict[str, Any]] = []
    for left_stage, right_stage in (
        ("effect-input", "effect-output"),
        ("effect-output", "final-publish"),
        ("final-display-before", "final-display-after"),
        ("default-before-effect", "default-after-effect"),
    ):
        left = capture_by_stage(captures, left_stage)
        right = capture_by_stage(captures, right_stage)
        if left is None or right is None:
            continue
        left_path = Path(str(left.get("path", "")))
        right_path = Path(str(right.get("path", "")))
        if not left_path.exists() or not right_path.exists():
            continue
        comparison = compare_image_paths(left_path, right_path)
        comparison["boundary"] = f"{left_stage}->{right_stage}"
        comparison["leftStage"] = left_stage
        comparison["rightStage"] = right_stage
        comparison["classification"] = classify_boundary(comparison)
        comparisons.append(comparison)
    return comparisons


def policy_for(layer: dict[str, Any], diagnostic: dict[str, Any] | None) -> dict[str, Any]:
    policy = layer.get("policy")
    if isinstance(policy, dict):
        return policy
    if diagnostic is not None:
        policy = diagnostic.get("policy")
        if isinstance(policy, dict):
            return policy
    return {}


def debug_probe_for(layer: dict[str, Any], fallback: dict[str, Any] | None) -> dict[str, Any]:
    probe = layer.get("debugProbe")
    if isinstance(probe, dict):
        return probe
    if fallback is not None:
        probe = fallback.get("debugProbe")
        if isinstance(probe, dict):
            return probe
    return {}


def first_dict(*values: Any) -> dict[str, Any]:
    for value in values:
        if isinstance(value, dict):
            return value
    return {}


def publish_for(layer: dict[str, Any], diagnostic: dict[str, Any] | None) -> dict[str, Any]:
    return first_dict(layer.get("publish"), (diagnostic or {}).get("publish"))


def route_diagnostics(layer: dict[str, Any], diagnostic: dict[str, Any] | None) -> dict[str, Any]:
    publish = publish_for(layer, diagnostic)
    if not publish:
        return {"classification": "missing-route-metadata"}
    route_risk = str(publish.get("routeRisk", ""))
    final_mesh = str(publish.get("standaloneFinalMeshKind") or publish.get("effectFinalMeshKind") or "")
    puppet_layer = publish.get("puppetLayer") is True
    standalone = publish.get("standalonePuppetFinalDisplay") is True
    if route_risk == "puppet-effect-output-displayed-as-flat-card" or (
        puppet_layer and standalone and final_mesh == "flat-card"
    ):
        classification = "flat-card-puppet-effect-route"
    elif puppet_layer and standalone:
        classification = "standalone-puppet-effect-route"
    elif puppet_layer:
        classification = "puppet-effect-route"
    else:
        classification = "non-puppet-effect-route"
    return {
        "classification": classification,
        "puppetLayer": puppet_layer,
        "standalonePuppetFinalDisplay": standalone,
        "publishFinalOutput": publish.get("publishFinalOutput"),
        "effectInputMeshKind": str(publish.get("effectInputMeshKind", "")),
        "effectFinalMeshKind": str(publish.get("effectFinalMeshKind", "")),
        "standaloneFinalMeshKind": str(publish.get("standaloneFinalMeshKind", "")),
        "finalDisplayRoute": str(publish.get("finalDisplayRoute", "")),
        "routeRisk": route_risk,
    }


def mesh_bounds_summary(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        return {}
    return {
        "vertexArrayCount": value.get("vertexArrayCount"),
        "indexArrayCount": value.get("indexArrayCount"),
        "vertexCount": value.get("vertexCount"),
        "indexDataCount": value.get("indexDataCount"),
        "indexRenderDataCount": value.get("indexRenderDataCount"),
        "positionMin": as_list(value.get("positionMin")),
        "positionMax": as_list(value.get("positionMax")),
    }


def transform_origin(value: Any) -> list[Any]:
    if not isinstance(value, dict):
        return []
    return as_list(value.get("origin"))


def composition_diagnostics(
    layer: dict[str, Any],
    diagnostic: dict[str, Any] | None,
) -> dict[str, Any]:
    publish = publish_for(layer, diagnostic)
    if not publish:
        return {"classification": "missing-composition-metadata"}

    standalone = publish.get("standalonePuppetFinalDisplay") is True
    parent_id = publish.get("parentId")
    standalone_parent_id = publish.get("standaloneDisplayParentId")
    final_texture = str(publish.get("standaloneFinalTexture") or "")
    effect_input_mesh = mesh_bounds_summary(publish.get("effectInputMeshBounds"))
    effect_final_mesh = mesh_bounds_summary(publish.get("effectFinalMeshBounds"))
    standalone_final_mesh = mesh_bounds_summary(publish.get("standaloneFinalMeshBounds"))

    if standalone and not final_texture:
        classification = "missing-standalone-final-texture"
    elif standalone and parent_id != standalone_parent_id:
        classification = "standalone-parent-mismatch"
    elif standalone and not standalone_final_mesh:
        classification = "missing-standalone-final-mesh-bounds"
    else:
        classification = "composition-metadata-present"

    return {
        "classification": classification,
        "effectInputLocalOrigin": transform_origin(publish.get("effectInputLocalTransform")),
        "standaloneDisplayLocalOrigin": transform_origin(
            publish.get("standaloneDisplayLocalTransform")
        ),
        "standaloneDisplayParentId": standalone_parent_id,
        "standaloneDisplayHasParsedParentNode": publish.get(
            "standaloneDisplayHasParsedParentNode"
        ),
        "standaloneDisplayNodeOrdinal": publish.get("standaloneDisplayNodeOrdinal"),
        "standaloneFinalMaterialBlendMode": publish.get(
            "standaloneFinalMaterialBlendMode"
        ),
        "standaloneFinalTexture": final_texture,
        "effectInputMeshBounds": effect_input_mesh,
        "effectFinalMeshBounds": effect_final_mesh,
        "standaloneFinalMeshBounds": standalone_final_mesh,
    }


def effect_manifest_path_for_variant(
    variant: dict[str, Any],
    output_dir: Path | None,
) -> Path | None:
    manifest_path_text = variant.get("effectManifest")
    if manifest_path_text:
        return Path(str(manifest_path_text))

    if output_dir is None:
        return None
    variant_name = str(variant.get("name", ""))
    if not variant_name:
        return None
    fallback = output_dir / variant_name / "effect-captures" / "manifest.json"
    return fallback if fallback.exists() else None


def useful_class(*values: Any) -> str:
    for value in values:
        text = str(value or "")
        if text and text != "none":
            return text
    return str(values[0] or "") if values else ""


def text_blob(*sources: Any) -> str:
    values: list[str] = []
    for source in sources:
        if isinstance(source, dict):
            for key in ("candidateChainShape", "candidateEffectClass", "layerName"):
                value = source.get(key)
                if isinstance(value, str):
                    values.append(value)
            for key in ("effectNames", "effectOrder", "materialShaders", "candidateMixFamilies", "candidateFamilies"):
                values.extend(str(item) for item in as_list(source.get(key)))
    return " ".join(values).lower()


def infer_effect_class(layer: dict[str, Any], diagnostic: dict[str, Any] | None) -> str:
    explicit = useful_class(
        layer.get("candidateEffectClass"),
        (diagnostic or {}).get("candidateEffectClass"),
    )
    if explicit and explicit != "none":
        return explicit
    shape = str(layer.get("candidateChainShape") or (diagnostic or {}).get("candidateChainShape") or "")
    text = text_blob(layer, diagnostic)
    if shape == "protected-puppet-mixed" and ("lut" in text or "lut_loader" in text):
        return "protected-puppet-lut"
    if shape == "protected-puppet-mixed":
        return "protected-puppet-mixed"
    return explicit


def variant_report(
    variant: dict[str, Any],
    layer_id_value: int,
    normal_variant: dict[str, Any] | None = None,
    output_dir: Path | None = None,
) -> dict[str, Any]:
    manifest_path = effect_manifest_path_for_variant(variant, output_dir)
    normal_yakkai = ""
    if isinstance(normal_variant, dict):
        normal_yakkai = str(normal_variant.get("yakkai", ""))
    out: dict[str, Any] = {
        "name": str(variant.get("name", "")),
        "status": str(variant.get("status", "")),
        "reference": str(variant.get("reference", "")),
        "yakkai": str(variant.get("yakkai", "")),
        "normalYakkai": normal_yakkai,
        "registeredYakkai": str(variant.get("registeredYakkai", "")),
        "registeredDiff": str(variant.get("registeredDiff", "")),
        "metrics": variant.get("metrics") if isinstance(variant.get("metrics"), dict) else {},
        "effectManifest": str(manifest_path or ""),
        "manifestStatus": "missing",
        "protectedDiagnostic": None,
        "probeCaptures": [],
        "probeStages": [],
        "probeCaptureCount": 0,
        "boundaryComparisons": [],
        "probeFinalToNormal": None,
        "screenSpaceDrift": None,
        "routeDiagnostics": {"classification": "missing-route-metadata"},
        "compositionDiagnostics": {"classification": "missing-composition-metadata"},
        "finalCoverageDiagnostics": {"classification": "missing-final-evidence", "activeSlots": []},
        "debugProbeRequested": False,
        "debugProbeOverrodePolicy": False,
        "debugProbeReason": "",
    }
    if manifest_path is None:
        return out

    if not manifest_path.exists():
        return out

    manifest = load_json(manifest_path)
    out["manifestStatus"] = str(manifest.get("status", "unknown"))
    diagnostics = [
        item for item in as_list(manifest.get("protectedPuppetDiagnostics"))
        if isinstance(item, dict) and layer_id(item) == layer_id_value
    ]
    diagnostic = diagnostics[0] if diagnostics else None
    out["protectedDiagnostic"] = diagnostic

    captures = [
        item for item in as_list(manifest.get("captures"))
        if isinstance(item, dict) and layer_id(item) == layer_id_value
    ]
    out["probeCaptures"] = [capture_record(item) for item in captures]
    out["probeStages"] = [item["stage"] for item in out["probeCaptures"]]
    out["probeCaptureCount"] = len(out["probeCaptures"])
    out["boundaryComparisons"] = boundary_comparisons(out["probeCaptures"])
    final_publish = capture_by_stage(out["probeCaptures"], "final-publish")
    if final_publish is not None and normal_yakkai:
        final_path = Path(str(final_publish.get("path", "")))
        normal_path = Path(normal_yakkai)
        if final_path.exists() and normal_path.exists():
            comparison = compare_image_paths(final_path, normal_path)
            comparison["boundary"] = "probe-final-publish->normal-yakkai"
            comparison["classification"] = classify_boundary(comparison)
            out["probeFinalToNormal"] = comparison
            out["screenSpaceDrift"] = screen_space_drift(final_path, normal_path)

    first_capture_layer = layer_from_record(captures[0]) if captures else {}
    stripped = first_matching_layer(as_list(manifest.get("strippedCandidates")), layer_id_value)
    probe = debug_probe_for(first_capture_layer, stripped)
    out["debugProbeRequested"] = probe.get("requested") is True
    out["debugProbeOverrodePolicy"] = probe.get("overrodePolicy") is True
    out["debugProbeReason"] = str(probe.get("reason", ""))

    policy = policy_for(first_capture_layer, diagnostic)
    out["policyReason"] = str(policy.get("reason", ""))
    out["normalPolicyKeepsEffects"] = policy.get("keepEffects")
    out["normalPolicyStripsEffects"] = policy.get("strippedEffects")
    out["candidateChainShape"] = str(
        first_capture_layer.get("candidateChainShape")
        or (diagnostic or {}).get("candidateChainShape")
        or ""
    )
    out["candidateEffectClass"] = infer_effect_class(first_capture_layer, diagnostic)
    out["routeDiagnostics"] = route_diagnostics(first_capture_layer, diagnostic)
    out["compositionDiagnostics"] = composition_diagnostics(first_capture_layer, diagnostic)
    out["finalCoverageDiagnostics"] = final_coverage_diagnostics(
        out["probeCaptures"],
        first_capture_layer,
        diagnostic,
    )
    out["effectOrder"] = as_list((diagnostic or {}).get("effectOrder")) or as_list(first_capture_layer.get("effectNames"))
    alpha_evidence = (diagnostic or {}).get("alphaEvidence")
    out["alphaEvidence"] = alpha_evidence if isinstance(alpha_evidence, dict) else {}
    return out


def normal_variants_by_name(normal_summary_path: Path | None) -> dict[str, dict[str, Any]]:
    if normal_summary_path is None:
        return {}
    if not normal_summary_path.exists():
        return {}
    summary = load_json(normal_summary_path)
    variants: dict[str, dict[str, Any]] = {}
    for variant in as_list(summary.get("variants")):
        if not isinstance(variant, dict):
            continue
        name = str(variant.get("name", ""))
        if name:
            variants[name] = variant
    return variants


def build_report(
    summary_path: Path,
    layer_id: int = 405,
    normal_summary_path: Path | None = None,
) -> dict[str, Any]:
    summary = load_json(summary_path)
    normal_variants = normal_variants_by_name(normal_summary_path)
    output_dir_text = str(summary.get("outputDir") or "")
    output_dir = Path(output_dir_text) if output_dir_text else None
    variants = [
        variant_report(variant,
                       layer_id,
                       normal_variants.get(str(variant.get("name", ""))),
                       output_dir)
        for variant in as_list(summary.get("variants"))
        if isinstance(variant, dict)
    ]
    return {
        "summary": str(summary_path),
        "normalSummary": str(normal_summary_path or ""),
        "outputDir": str(summary.get("outputDir", "")),
        "layerId": layer_id,
        "variants": variants,
    }


def format_number(value: Any) -> str:
    if isinstance(value, (int, float)):
        return f"{value:.6g}"
    return "n/a"


def format_compact_list(value: Any) -> str:
    values = as_list(value)
    if not values:
        return "n/a"
    return "[" + ", ".join(
        format_number(item) if isinstance(item, (int, float)) else str(item)
        for item in values
    ) + "]"


def format_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# Arona Protected Puppet Lab",
        "",
        f"- summary: `{report['summary']}`",
        f"- layer: `{report['layerId']}`",
        "",
    ]
    for variant in report["variants"]:
        metrics = variant.get("metrics") if isinstance(variant.get("metrics"), dict) else {}
        registered_rmse = format_number(metrics.get("registeredRmse"))
        lines.append(f"## {variant['name']}")
        lines.append("")
        lines.append(
            f"- {variant['name']}: status={variant['status']} registeredRmse={registered_rmse}"
        )
        lines.append(
            f"- manifest: `{variant['manifestStatus']}` `{variant.get('effectManifest', '')}`"
        )
        lines.append(
            f"- probe: requested={variant['debugProbeRequested']} "
            f"overrodePolicy={variant['debugProbeOverrodePolicy']} "
            f"reason=`{variant['debugProbeReason']}`"
        )
        stages = ", ".join(variant["probeStages"]) if variant["probeStages"] else "none"
        lines.append(f"- probe captures: {variant['probeCaptureCount']} `{stages}`")
        diagnostic = variant.get("protectedDiagnostic")
        if isinstance(diagnostic, dict):
            lines.append(
                f"- protected diagnostic: {diagnostic.get('captureMode', 'unknown')}"
            )
        else:
            lines.append("- protected diagnostic: missing")
        lines.append(
            f"- class/shape: `{variant.get('candidateEffectClass', '')}` "
            f"`{variant.get('candidateChainShape', '')}`"
        )
        if variant.get("effectOrder"):
            lines.append(
                "- effect order: " + ", ".join(f"`{item}`" for item in variant["effectOrder"])
            )
        if variant.get("alphaEvidence"):
            lines.append(f"- alpha evidence: `{variant['alphaEvidence']}`")
        route = variant.get("routeDiagnostics")
        if isinstance(route, dict):
            lines.append(
                f"- route: class=`{route.get('classification')}` "
                f"inputMesh=`{route.get('effectInputMeshKind')}` "
                f"finalMesh=`{route.get('standaloneFinalMeshKind') or route.get('effectFinalMeshKind')}` "
                f"route=`{route.get('finalDisplayRoute')}` "
                f"risk=`{route.get('routeRisk')}`"
            )
        composition = variant.get("compositionDiagnostics")
        if isinstance(composition, dict):
            input_mesh = composition.get("effectInputMeshBounds")
            if not isinstance(input_mesh, dict):
                input_mesh = {}
            final_mesh = composition.get("standaloneFinalMeshBounds")
            if not isinstance(final_mesh, dict):
                final_mesh = {}
            lines.append(
                f"- composition: class=`{composition.get('classification')}` "
                f"finalTexture=`{composition.get('standaloneFinalTexture')}` "
                f"finalBlend=`{composition.get('standaloneFinalMaterialBlendMode')}` "
                f"ordinal=`{composition.get('standaloneDisplayNodeOrdinal')}` "
                f"parent=`{composition.get('standaloneDisplayParentId')}` "
                f"inputVerts=`{input_mesh.get('vertexCount')}` "
                f"finalVerts=`{final_mesh.get('vertexCount')}` "
                f"inputOrigin=`{format_compact_list(composition.get('effectInputLocalOrigin'))}` "
                f"displayOrigin=`{format_compact_list(composition.get('standaloneDisplayLocalOrigin'))}`"
            )
        coverage = variant.get("finalCoverageDiagnostics")
        if isinstance(coverage, dict):
            alignment = coverage.get("finalDisplayAlignment")
            if not isinstance(alignment, dict):
                alignment = {}
            sensitivity = coverage.get("finalDisplayThresholdSensitivity")
            if not isinstance(sensitivity, dict):
                sensitivity = {}
            projection = coverage.get("finalDisplayScreenSpaceProjection")
            if not isinstance(projection, dict):
                projection = {}
            lines.append(
                f"- final coverage: class=`{coverage.get('classification')}` "
                f"source=`{coverage.get('source')}` "
                f"activeSlots=`{format_compact_list(coverage.get('activeSlots'))}` "
                f"secondaryOnlySlots=`{format_compact_list(coverage.get('secondaryOnlySlots'))}` "
                f"inputVisible=`{coverage.get('inputVisibleFraction')}` "
                f"outputVisible=`{coverage.get('outputVisibleFraction')}` "
                f"finalVisible=`{coverage.get('finalVisibleFraction')}` "
                f"finalDisplayDelta=`{coverage.get('finalDisplayDeltaVisibleFraction')}` "
                f"alignment=`{alignment.get('classification')}` "
                f"alignRatio=`{alignment.get('deltaToOutputVisibleRatio')}` "
                f"alignIou=`{alignment.get('boundsIou')}` "
                f"alignDrift=`{alignment.get('centroidDrift')}` "
                f"thresholds=`{sensitivity.get('classification')}` "
                f"alignedThresholds=`{format_compact_list(sensitivity.get('alignedThresholdRgb255'))}` "
                f"driftThresholds=`{format_compact_list(sensitivity.get('shapeDriftThresholdRgb255'))}` "
                f"screenProjection=`{projection.get('classification')}` "
                f"projectedDrift=`{projection.get('projectedCentroidDrift')}` "
                f"finalToOutput=`{coverage.get('finalToOutputAlphaRatio')}` "
                f"centroidDrift=`{coverage.get('finalCentroidDrift')}`"
            )
        for comparison in variant.get("boundaryComparisons", []):
            lines.append(
                f"- boundary `{comparison.get('boundary')}`: "
                f"class=`{comparison.get('classification')}` "
                f"rmse=`{comparison.get('rmse')}` "
                f"alphaRmse=`{comparison.get('alphaRmse')}` "
                f"alphaIou=`{comparison.get('alphaBoundsIou')}` "
                f"meanRgbDelta=`{comparison.get('meanRgbDelta')}`"
            )
        if isinstance(variant.get("probeFinalToNormal"), dict):
            comparison = variant["probeFinalToNormal"]
            lines.append(
                f"- probe final vs normal: class=`{comparison.get('classification')}` "
                f"rmse=`{comparison.get('rmse')}` "
                f"alphaRmse=`{comparison.get('alphaRmse')}` "
                f"alphaIou=`{comparison.get('alphaBoundsIou')}` "
                f"meanRgbDelta=`{comparison.get('meanRgbDelta')}`"
            )
        if isinstance(variant.get("screenSpaceDrift"), dict):
            drift = variant["screenSpaceDrift"]
            lines.append(
                f"- screen-space drift: class=`{drift.get('classification')}` "
                f"delta=`{drift.get('centroidDeltaPixels')}` "
                f"magnitude=`{drift.get('driftMagnitudePixels')}` "
                f"coverage=`{drift.get('differenceCoverage')}`"
            )
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def contact_sheet_inputs(report: dict[str, Any]) -> list[tuple[str, Path]]:
    rows: list[tuple[str, Path]] = []
    preferred_stages = {
        "effect-input",
        "effect-output",
        "final-publish",
    }
    for variant in report["variants"]:
        name = variant["name"]
        for label_key, path_key in (
            ("reference", "reference"),
            ("yakkai", "yakkai"),
            ("registered", "registeredYakkai"),
        ):
            path_text = variant.get(path_key)
            if path_text and Path(path_text).exists():
                rows.append((f"{name} {label_key}", Path(path_text)))
        for capture in variant.get("probeCaptures", []):
            stage = capture.get("stage", "")
            path_text = capture.get("path", "")
            if (
                (stage in preferred_stages or str(stage).startswith("material-output"))
                and path_text
                and Path(path_text).exists()
            ):
                rows.append((f"{name} {stage}", Path(path_text)))
    return rows


def write_contact_sheet(report: dict[str, Any], output: Path) -> bool:
    inputs = contact_sheet_inputs(report)
    if not inputs:
        return False
    output.parent.mkdir(parents=True, exist_ok=True)
    command = ["montage"]
    for label, path in inputs:
        command += ["-label", label, str(path)]
    command += ["-thumbnail", "360x220", "-geometry", "+10+30", "-tile", "6x", str(output)]
    completed = subprocess.run(command, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return completed.returncode == 0 and output.exists()


def write_outputs(report: dict[str, Any], output_dir: Path, *, contact_sheet: bool = True) -> dict[str, str]:
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "protected-puppet-lab.json"
    markdown_path = output_dir / "protected-puppet-lab.md"
    json_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    markdown_path.write_text(format_markdown(report), encoding="utf-8")
    outputs = {
        "json": str(json_path),
        "markdown": str(markdown_path),
    }
    if contact_sheet:
        contact_sheet_path = output_dir / "protected-puppet-contact-sheet.png"
        if write_contact_sheet(report, contact_sheet_path):
            outputs["contactSheet"] = str(contact_sheet_path)
    return outputs


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize Arona protected puppet probe evidence.")
    parser.add_argument("summary", type=Path)
    parser.add_argument("--layer-id", type=int, default=405)
    parser.add_argument("--normal-summary", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--no-contact-sheet", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    report = build_report(args.summary, layer_id=args.layer_id, normal_summary_path=args.normal_summary)
    output_dir = args.output_dir or args.summary.parent / "protected-puppet-lab"
    outputs = write_outputs(report, output_dir, contact_sheet=not args.no_contact_sheet)
    print(f"Report: {outputs['markdown']}")
    print(f"JSON: {outputs['json']}")
    if "contactSheet" in outputs:
        print(f"Contact sheet: {outputs['contactSheet']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
