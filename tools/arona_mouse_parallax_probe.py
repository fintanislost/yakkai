#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import shlex
import subprocess
from typing import Any

from PIL import Image, ImageChops, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_ROOT = ROOT / "smoke-tests" / "artifacts" / "tmp" / "arona-mouse-parallax-probe"
ARONA_REVIEW_REGIONS = {
    "wall": (0.08, 0.08, 0.42, 0.38),
    "desk": (0.05, 0.68, 0.95, 0.90),
    "character": (0.35, 0.12, 0.88, 0.74),
    "ribbon_tip": (0.70, 0.05, 0.98, 0.42),
}
ARONA_TRANSFORM_ANCHOR_LAYER_IDS = (195, 217, 325, 328)

# Local diagnostic threshold for still-frame registration. Values above this
# mean the best shifted crops still differ too much to trust motion direction.
MOTION_REGION_RMSE_UNCERTAIN_THRESHOLD = 0.18
MOTION_REGION_MIN_SHIFT_PX = 2
MOTION_REGION_MAX_SEARCH_DIMENSION = 360
GATE_EXIT_CODE = 3
GATE_OFFSET_EPSILON = 0.01
GATE_MIN_ARONA_LAYER_COUNT = 17
GATE_MIN_VISIBLE_EXPECTED_OFFSET_PX = 20.0
GATE_MIN_VISIBLE_TRANSLATION_RATIO = 0.10
GATE_ALLOWED_ANCHOR_CLASSIFICATIONS = {
    "anchor-propagation-evidence-present",
    "anchor-child-map-missing",
}
GATE_ALLOWED_MOTION_CLASSIFICATIONS = {
    "opposite-sign-motion",
    "weak-motion",
    "one-sided-motion",
    "same-direction-motion",
    "registration-uncertain",
}


@dataclass(frozen=True)
class ProbeConfig:
    repo_root: Path
    harness: Path
    source: Path
    assets: Path
    output_root: Path
    scene_id: str = "3228578419"
    capture_delay_ms: int = 10000
    window_size: str = "1600x900"
    capture_layers: str = "405"
    scene_properties_json: str = ""


VARIANTS: tuple[tuple[str, str], ...] = (
    ("left", "0,0.5"),
    ("center", "0.5,0.5"),
    ("right", "1,0.5"),
)


def parse_window_sizes(raw: str) -> list[str]:
    sizes: list[str] = []
    for part in raw.split(","):
        value = part.strip().lower()
        if not value:
            raise ValueError("window size entries must not be empty")
        pieces = value.split("x")
        if len(pieces) != 2:
            raise ValueError(f"invalid window size: {value}")
        width, height = pieces
        if not width.isdigit() or not height.isdigit():
            raise ValueError(f"invalid window size: {value}")
        if int(width) <= 0 or int(height) <= 0:
            raise ValueError(f"invalid window size: {value}")
        sizes.append(f"{int(width)}x{int(height)}")
    return sizes


def safe_size_dir(window_size: str) -> str:
    return window_size.lower()


def config_for_window_size(config: ProbeConfig, window_size: str) -> ProbeConfig:
    return ProbeConfig(
        repo_root=config.repo_root,
        harness=config.harness,
        source=config.source,
        assets=config.assets,
        output_root=config.output_root / safe_size_dir(window_size),
        scene_id=config.scene_id,
        capture_delay_ms=config.capture_delay_ms,
        window_size=window_size,
        capture_layers=config.capture_layers,
        scene_properties_json=config.scene_properties_json,
    )


def build_harness_command(config: ProbeConfig, variant_name: str, mouse_position: str) -> list[str]:
    variant_dir = config.output_root / variant_name
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
        "--capture",
        str(variant_dir / "still.png"),
        "--capture-delay-ms",
        str(config.capture_delay_ms),
        "--debug-effect-captures",
        str(variant_dir / "effect-captures"),
        "--debug-effect-capture-delay-ms",
        str(config.capture_delay_ms),
        "--debug-effect-capture-layers",
        config.capture_layers,
        "--debug-mouse-position",
        mouse_position,
    ]
    if config.scene_properties_json:
        command.extend(["--scene-properties-json", config.scene_properties_json])
    return command


def load_mouse_parallax_manifest(path: Path) -> dict[str, Any]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    mouse = manifest.get("mouseParallax")
    if not isinstance(mouse, dict):
        raise ValueError(f"manifest does not contain mouseParallax: {path}")
    return manifest


def _layers_by_id(manifest: dict[str, Any]) -> dict[int, dict[str, Any]]:
    layers = manifest.get("mouseParallax", {}).get("parallaxLayers", [])
    result: dict[int, dict[str, Any]] = {}
    if not isinstance(layers, list):
        return result
    for layer in layers:
        if not isinstance(layer, dict):
            continue
        layer_id = layer.get("layerId")
        if isinstance(layer_id, int):
            result[layer_id] = layer
    return result


def _coerce_layer_id(value: Any) -> int | None:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value)
        except ValueError:
            return None
    return None


def _coerce_layer_id_list(value: Any) -> list[int]:
    if not isinstance(value, list):
        return []
    ids: list[int] = []
    for item in value:
        layer_id = _coerce_layer_id(item.get("layerId") if isinstance(item, dict) else item)
        if layer_id is not None:
            ids.append(layer_id)
    return sorted(dict.fromkeys(ids))


def _mouse_parallax_layers(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    mouse = manifest.get("mouseParallax", {})
    if not isinstance(mouse, dict):
        return []
    layers = mouse.get("parallaxLayers", [])
    if not isinstance(layers, list):
        return []
    return [layer for layer in layers if isinstance(layer, dict)]


def _authored_transform_anchor_layer_ids(manifest: dict[str, Any]) -> list[int]:
    mouse = manifest.get("mouseParallax", {})
    if not isinstance(mouse, dict):
        return list(ARONA_TRANSFORM_ANCHOR_LAYER_IDS)

    if "authoredTransformAnchorLayerIds" in mouse:
        return _coerce_layer_id_list(mouse.get("authoredTransformAnchorLayerIds"))
    if "authoredTransformAnchors" in mouse:
        return _coerce_layer_id_list(mouse.get("authoredTransformAnchors"))
    return list(ARONA_TRANSFORM_ANCHOR_LAYER_IDS)


def classify_anchor_propagation(manifest: dict[str, Any]) -> dict[str, Any]:
    authored_ids = _authored_transform_anchor_layer_ids(manifest)
    anchor_layers = [
        layer
        for layer in _mouse_parallax_layers(manifest)
        if layer.get("layerKind") == "transform-anchor"
    ]
    present_ids = sorted(
        layer_id
        for layer_id in (_coerce_layer_id(layer.get("layerId")) for layer in anchor_layers)
        if layer_id is not None
    )
    missing_ids = [layer_id for layer_id in authored_ids if layer_id not in set(present_ids)]

    anchor_rows: list[dict[str, Any]] = []
    child_map_missing = False
    has_child_edges = False
    for layer in anchor_layers:
        child_ids_raw = layer.get("childLayerIds")
        child_ids = _coerce_layer_id_list(child_ids_raw)
        propagation_expectation = str(layer.get("propagationExpectation", "unknown-parent-graph"))
        child_map_known = isinstance(child_ids_raw, list) and propagation_expectation != "unknown-parent-graph"
        if not child_map_known:
            child_map_missing = True
        if child_ids:
            has_child_edges = True
        anchor_rows.append(
            {
                "layerId": _coerce_layer_id(layer.get("layerId")),
                "layerName": layer.get("layerName", ""),
                "parentLayerId": _coerce_layer_id(layer.get("parentLayerId")),
                "parentLayerName": layer.get("parentLayerName", ""),
                "hasChildren": bool(layer.get("hasChildren", False)),
                "childLayerIds": child_ids,
                "propagationExpectation": propagation_expectation,
            }
        )

    if missing_ids:
        classification = "anchor-inventory-missing"
    elif child_map_missing or not has_child_edges:
        classification = "anchor-child-map-missing"
    else:
        classification = "anchor-propagation-evidence-present"

    return {
        "classification": classification,
        "authoredAnchorLayerIds": authored_ids,
        "presentAnchorLayerIds": present_ids,
        "missingAnchorLayerIds": missing_ids,
        "anchors": anchor_rows,
    }


def _offset(layer: dict[str, Any]) -> tuple[float, float]:
    value = layer.get("expectedOffset", [0.0, 0.0])
    if not isinstance(value, list) or len(value) < 2:
        return (0.0, 0.0)
    try:
        return (float(value[0]), float(value[1]))
    except (TypeError, ValueError):
        return (0.0, 0.0)


def _max_abs_offset(manifest: dict[str, Any]) -> float:
    max_value = 0.0
    for layer in _layers_by_id(manifest).values():
        x, y = _offset(layer)
        max_value = max(max_value, abs(x), abs(y))
    return max_value


def _has_opposite_sign_movement(
    left_manifest: dict[str, Any],
    right_manifest: dict[str, Any],
    *,
    epsilon: float,
) -> bool:
    left_layers = _layers_by_id(left_manifest)
    right_layers = _layers_by_id(right_manifest)
    for layer_id, left_layer in left_layers.items():
        right_layer = right_layers.get(layer_id)
        if right_layer is None:
            continue
        left_x, left_y = _offset(left_layer)
        right_x, right_y = _offset(right_layer)
        if abs(left_x) > epsilon and abs(right_x) > epsilon and left_x * right_x < 0:
            return True
        if abs(left_y) > epsilon and abs(right_y) > epsilon and left_y * right_y < 0:
            return True
    return False


def _max_expected_anchor_offset(left_manifest: dict[str, Any], right_manifest: dict[str, Any]) -> float:
    left_layers = _layers_by_id(left_manifest)
    right_layers = _layers_by_id(right_manifest)
    anchor_ids = set(_authored_transform_anchor_layer_ids(left_manifest))
    anchor_ids.update(_authored_transform_anchor_layer_ids(right_manifest))
    max_value = 0.0
    for layer_id in anchor_ids:
        for layer in (left_layers.get(layer_id), right_layers.get(layer_id)):
            if layer is None:
                continue
            x, y = _offset(layer)
            max_value = max(max_value, abs(x), abs(y))
    return max_value


def _visible_opposite_sign_translation_px(motion_regions: dict[str, Any]) -> int:
    max_value = 0
    for region in motion_regions.values():
        if not isinstance(region, dict):
            continue
        left = region.get("leftVsCenter")
        right = region.get("rightVsCenter")
        if not isinstance(left, dict) or not isinstance(right, dict):
            continue
        left_dx = int(left.get("dx", 0))
        left_dy = int(left.get("dy", 0))
        right_dx = int(right.get("dx", 0))
        right_dy = int(right.get("dy", 0))
        if left_dx * right_dx < 0:
            max_value = max(max_value, min(abs(left_dx), abs(right_dx)))
        if left_dy * right_dy < 0:
            max_value = max(max_value, min(abs(left_dy), abs(right_dy)))
    return max_value


def classify_probe(
    left_manifest: dict[str, Any],
    center_manifest: dict[str, Any],
    right_manifest: dict[str, Any],
    image_metrics: dict[str, float],
    *,
    offset_epsilon: float = 0.01,
    image_rmse_threshold: float = 0.002,
) -> str:
    if _max_abs_offset(center_manifest) > offset_epsilon:
        return "center-not-neutral"

    has_manifest_movement = _has_opposite_sign_movement(
        left_manifest,
        right_manifest,
        epsilon=offset_epsilon,
    )
    max_image_rmse = max(
        float(image_metrics.get("leftCenterRmse", 0.0)),
        float(image_metrics.get("rightCenterRmse", 0.0)),
    )

    if not has_manifest_movement and max_image_rmse <= image_rmse_threshold:
        return "no-local-movement"
    if has_manifest_movement and max_image_rmse > image_rmse_threshold:
        return "movement-detected"
    return "review-needed"


def image_object_rmse(left_image: Image.Image, right_image: Image.Image) -> float:
    left_rgba = left_image.convert("RGBA")
    right_rgba = right_image.convert("RGBA")
    if right_rgba.size != left_rgba.size:
        right_rgba = right_rgba.resize(left_rgba.size, Image.Resampling.BILINEAR)
    diff = ImageChops.difference(left_rgba, right_rgba)
    histogram = diff.histogram()
    square_sum = 0
    for value, count in enumerate(histogram):
        channel_value = value % 256
        square_sum += channel_value * channel_value * count
    divisor = left_rgba.width * left_rgba.height * 4
    if divisor == 0:
        return 0.0
    return math.sqrt(square_sum / divisor) / 255.0


def image_rmse(left: Path, right: Path) -> float:
    with Image.open(left) as left_image, Image.open(right) as right_image:
        return image_object_rmse(left_image, right_image)


def _crop_rmse_at_shift(
    base_gray: Image.Image,
    moved_gray: Image.Image,
    region: tuple[int, int, int, int],
    dx: int,
    dy: int,
) -> float:
    x0, y0, x1, y1 = region
    shifted_x0 = x0 + dx
    shifted_y0 = y0 + dy
    shifted_x1 = x1 + dx
    shifted_y1 = y1 + dy
    if (
        shifted_x0 < 0
        or shifted_y0 < 0
        or shifted_x1 > moved_gray.width
        or shifted_y1 > moved_gray.height
    ):
        return float("inf")
    base_crop = base_gray.crop((x0, y0, x1, y1))
    moved_crop = moved_gray.crop((shifted_x0, shifted_y0, shifted_x1, shifted_y1))
    return image_object_rmse(base_crop, moved_crop)


def _scaled_registration_inputs(
    base_gray: Image.Image,
    moved_gray: Image.Image,
    region: tuple[int, int, int, int],
    max_shift: int,
) -> tuple[Image.Image, Image.Image, tuple[int, int, int, int], int, float]:
    x0, y0, x1, y1 = region
    longest_region_edge = max(x1 - x0, y1 - y0)
    if longest_region_edge <= MOTION_REGION_MAX_SEARCH_DIMENSION:
        return base_gray, moved_gray, region, max_shift, 1.0

    scale = MOTION_REGION_MAX_SEARCH_DIMENSION / longest_region_edge
    scaled_size = (
        max(1, round(base_gray.width * scale)),
        max(1, round(base_gray.height * scale)),
    )
    scaled_region = (
        max(0, min(scaled_size[0], round(x0 * scale))),
        max(0, min(scaled_size[1], round(y0 * scale))),
        max(0, min(scaled_size[0], round(x1 * scale))),
        max(0, min(scaled_size[1], round(y1 * scale))),
    )
    scaled_max_shift = max(1, round(max_shift * scale))
    return (
        base_gray.resize(scaled_size, Image.Resampling.BILINEAR),
        moved_gray.resize(scaled_size, Image.Resampling.BILINEAR),
        scaled_region,
        scaled_max_shift,
        scale,
    )


def estimate_translation(
    base: Image.Image,
    moved: Image.Image,
    region: tuple[int, int, int, int],
    max_shift: int,
    step: int,
) -> dict[str, Any]:
    base_gray = base.convert("L")
    moved_gray = moved.convert("L")
    search_base, search_moved, search_region, search_max_shift, search_scale = _scaled_registration_inputs(
        base_gray,
        moved_gray,
        region,
        max_shift,
    )
    x0, y0, x1, y1 = search_region
    base_crop = search_base.crop((x0, y0, x1, y1))
    best = {"dx": 0, "dy": 0, "rmse": float("inf")}
    moved_width, moved_height = search_moved.size
    search_step = max(1, round(step * search_scale))
    for dy in range(-search_max_shift, search_max_shift + 1, search_step):
        for dx in range(-search_max_shift, search_max_shift + 1, search_step):
            shifted_x0 = x0 + dx
            shifted_y0 = y0 + dy
            shifted_x1 = x1 + dx
            shifted_y1 = y1 + dy
            if shifted_x0 < 0 or shifted_y0 < 0 or shifted_x1 > moved_width or shifted_y1 > moved_height:
                continue
            moved_crop = search_moved.crop((shifted_x0, shifted_y0, shifted_x1, shifted_y1))
            rmse = image_object_rmse(base_crop, moved_crop)
            current_shift = abs(dx) + abs(dy)
            best_shift = abs(int(best["dx"])) + abs(int(best["dy"]))
            if rmse < best["rmse"] or (rmse == best["rmse"] and current_shift < best_shift):
                best = {"dx": dx, "dy": dy, "rmse": rmse}

    original_dx = round(int(best["dx"]) / search_scale)
    original_dy = round(int(best["dy"]) / search_scale)
    candidate_rmse = _crop_rmse_at_shift(base_gray, moved_gray, region, original_dx, original_dy)
    zero_shift_rmse = _crop_rmse_at_shift(base_gray, moved_gray, region, 0, 0)
    if zero_shift_rmse <= candidate_rmse:
        original_dx = 0
        original_dy = 0
        candidate_rmse = zero_shift_rmse
    return {
        "dx": original_dx,
        "dy": original_dy,
        "rmse": candidate_rmse,
        "zeroShiftRmse": zero_shift_rmse,
        "searchScale": search_scale,
        "searchedMaxShift": search_max_shift,
    }


def classify_motion_region(left_vs_center: dict[str, Any], right_vs_center: dict[str, Any]) -> str:
    left_rmse = float(left_vs_center.get("rmse", float("inf")))
    right_rmse = float(right_vs_center.get("rmse", float("inf")))
    if max(left_rmse, right_rmse) > MOTION_REGION_RMSE_UNCERTAIN_THRESHOLD:
        return "registration-uncertain"

    left_dx = int(left_vs_center.get("dx", 0))
    left_dy = int(left_vs_center.get("dy", 0))
    right_dx = int(right_vs_center.get("dx", 0))
    right_dy = int(right_vs_center.get("dy", 0))
    left_shift = max(abs(left_dx), abs(left_dy))
    right_shift = max(abs(right_dx), abs(right_dy))
    max_shift = max(left_shift, right_shift)
    if max_shift <= MOTION_REGION_MIN_SHIFT_PX:
        return "weak-motion"
    if (left_dx * right_dx < 0) or (left_dy * right_dy < 0):
        return "opposite-sign-motion"
    if min(left_shift, right_shift) <= MOTION_REGION_MIN_SHIFT_PX:
        return "one-sided-motion"
    return "same-direction-motion"


def _pixel_region(size: tuple[int, int], normalized: tuple[float, float, float, float]) -> tuple[int, int, int, int]:
    width, height = size
    x0, y0, x1, y1 = normalized
    return (
        max(0, min(width, round(width * x0))),
        max(0, min(height, round(height * y0))),
        max(0, min(width, round(width * x1))),
        max(0, min(height, round(height * y1))),
    )


def write_diff_map(left: Image.Image, right: Image.Image, output: Path) -> None:
    left_rgb = left.convert("RGB")
    right_rgb = right.convert("RGB")
    if right_rgb.size != left_rgb.size:
        right_rgb = right_rgb.resize(left_rgb.size, Image.Resampling.BILINEAR)
    diff = ImageChops.difference(left_rgb, right_rgb)
    amplified = diff.point(lambda value: min(255, value * 4))
    amplified.save(output)


def build_motion_regions(output_root: Path) -> dict[str, dict[str, Any]]:
    with Image.open(output_root / "left" / "still.png") as left_image, Image.open(
        output_root / "center" / "still.png"
    ) as center_image, Image.open(output_root / "right" / "still.png") as right_image:
        left = left_image.copy()
        center = center_image.copy()
        right = right_image.copy()

    write_diff_map(left, center, output_root / "left-vs-center-diff.png")
    write_diff_map(right, center, output_root / "right-vs-center-diff.png")

    width, height = center.size
    max_shift = max(24, round(min(width, height) * 0.05))
    regions: dict[str, dict[str, Any]] = {}
    for name, normalized_region in ARONA_REVIEW_REGIONS.items():
        region = _pixel_region(center.size, normalized_region)
        left_vs_center = estimate_translation(center, left, region, max_shift=max_shift, step=1)
        right_vs_center = estimate_translation(center, right, region, max_shift=max_shift, step=1)
        regions[name] = {
            "region": name,
            "normalizedRegion": list(normalized_region),
            "pixelRegion": list(region),
            "leftVsCenter": left_vs_center,
            "rightVsCenter": right_vs_center,
            "classification": classify_motion_region(left_vs_center, right_vs_center),
        }
    return regions


def write_contact_sheet(output: Path, images: list[tuple[str, Path]]) -> None:
    thumbs: list[tuple[str, Image.Image]] = []
    for label, path in images:
        with Image.open(path) as image:
            thumb = image.convert("RGB")
            thumb.thumbnail((480, 270), Image.Resampling.LANCZOS)
            thumbs.append((label, thumb.copy()))

    label_height = 26
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

    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)


def write_reports(output_root: Path, classification: str, image_metrics: dict[str, float]) -> None:
    manifests = {
        name: load_mouse_parallax_manifest(output_root / name / "effect-captures" / "manifest.json")
        for name, _position in VARIANTS
    }
    motion_regions = build_motion_regions(output_root)
    anchor_propagation = classify_anchor_propagation(manifests["center"])
    report = {
        "classification": classification,
        "imageMetrics": image_metrics,
        "motionRegions": motion_regions,
        "anchorPropagation": anchor_propagation,
        "manifests": {
            name: {
                "mouseParallax": manifest.get("mouseParallax", {}),
            }
            for name, manifest in manifests.items()
        },
    }
    (output_root / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Arona Mouse Parallax Probe",
        "",
        f"- Classification: `{classification}`",
        f"- Left/center RMSE: `{image_metrics.get('leftCenterRmse', 0.0):.8f}`",
        f"- Right/center RMSE: `{image_metrics.get('rightCenterRmse', 0.0):.8f}`",
        f"- Contact sheet: `{output_root / 'contact-sheet.png'}`",
        f"- Anchor propagation: `{anchor_propagation['classification']}`",
        "",
        "| Variant | Input source | Requested | Effective | Parallax layers |",
        "| --- | --- | --- | --- | ---: |",
    ]
    for name, manifest in manifests.items():
        mouse = manifest.get("mouseParallax", {})
        requested = mouse.get("requestedPosition", "")
        effective = mouse.get("effectivePosition", "")
        layers = mouse.get("parallaxLayers", [])
        layer_count = len(layers) if isinstance(layers, list) else 0
        lines.append(
            f"| {name} | `{mouse.get('inputSource', '')}` | `{requested}` | `{effective}` | {layer_count} |"
        )
    lines.extend(
        [
            "",
            "## Motion Regions",
            "",
            "| Region | Left vs center | Right vs center | Classification |",
            "| --- | ---: | ---: | --- |",
        ]
    )
    for name, region in motion_regions.items():
        left = region["leftVsCenter"]
        right = region["rightVsCenter"]
        lines.append(
            "| "
            f"{name} | "
            f"`dx={left['dx']} dy={left['dy']} rmse={float(left['rmse']):.6f}` | "
            f"`dx={right['dx']} dy={right['dy']} rmse={float(right['rmse']):.6f}` | "
            f"`{region['classification']}` |"
        )
    (output_root / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def _parallax_layer_count(report: dict[str, Any]) -> int:
    center = report.get("manifests", {}).get("center", {})
    mouse = center.get("mouseParallax", {}) if isinstance(center, dict) else {}
    layers = mouse.get("parallaxLayers", []) if isinstance(mouse, dict) else []
    return len(layers) if isinstance(layers, list) else 0


def _summary_label(summary: dict[str, Any], index: int) -> str:
    window_size = summary.get("windowSize")
    if isinstance(window_size, str) and window_size:
        return window_size
    return f"result-{index + 1}"


def _summary_manifest(summary: dict[str, Any], variant: str) -> dict[str, Any]:
    manifests = summary.get("manifests", {})
    if not isinstance(manifests, dict):
        return {}
    manifest = manifests.get(variant, {})
    return manifest if isinstance(manifest, dict) else {}


def evaluate_gate_results(results: list[dict[str, Any]]) -> list[str]:
    failures: list[str] = []
    for index, summary in enumerate(results):
        label = _summary_label(summary, index)
        classification = summary.get("classification")
        if classification != "movement-detected":
            failures.append(f"{label}: classification is {classification!r}, expected 'movement-detected'")

        left_manifest = _summary_manifest(summary, "left")
        center_manifest = _summary_manifest(summary, "center")
        right_manifest = _summary_manifest(summary, "right")
        center_offset = _max_abs_offset(center_manifest)
        if center_offset > GATE_OFFSET_EPSILON:
            failures.append(
                f"{label}: center expected offset {center_offset:.6f} exceeds {GATE_OFFSET_EPSILON:.2f}"
            )
        if not _has_opposite_sign_movement(left_manifest, right_manifest, epsilon=GATE_OFFSET_EPSILON):
            failures.append(f"{label}: no left/right opposite-sign expected offset layer")

        layer_count = _parallax_layer_count(summary)
        if layer_count < GATE_MIN_ARONA_LAYER_COUNT:
            failures.append(f"{label}: parallax layer count {layer_count} below {GATE_MIN_ARONA_LAYER_COUNT}")

        anchor = summary.get("anchorPropagation")
        anchor_classification = anchor.get("classification") if isinstance(anchor, dict) else None
        if anchor_classification not in GATE_ALLOWED_ANCHOR_CLASSIFICATIONS:
            failures.append(
                f"{label}: anchor propagation classification {anchor_classification!r} is not accepted"
            )

        motion_regions = summary.get("motionRegions")
        if not isinstance(motion_regions, dict) or not motion_regions:
            failures.append(f"{label}: motion-region classifications are missing")
        else:
            missing_regions = [
                name
                for name in ARONA_REVIEW_REGIONS
                if not isinstance(motion_regions.get(name), dict)
            ]
            if missing_regions:
                failures.append(
                    f"{label}: motion-region rows missing or malformed: {', '.join(missing_regions)}"
                )
            unknown_regions = [
                name
                for name, region in motion_regions.items()
                if isinstance(region, dict)
                and region.get("classification") not in GATE_ALLOWED_MOTION_CLASSIFICATIONS
            ]
            if unknown_regions:
                failures.append(
                    f"{label}: motion-region rows have unknown classifications: {', '.join(map(str, unknown_regions))}"
                )
            same_direction_regions = [
                name
                for name, region in motion_regions.items()
                if isinstance(region, dict) and region.get("classification") == "same-direction-motion"
            ]
            if same_direction_regions:
                failures.append(
                    f"{label}: same-direction motion regions present: {', '.join(map(str, same_direction_regions))}"
                )

            expected_anchor_offset = _max_expected_anchor_offset(left_manifest, right_manifest)
            if expected_anchor_offset >= GATE_MIN_VISIBLE_EXPECTED_OFFSET_PX:
                visible_translation = _visible_opposite_sign_translation_px(motion_regions)
                min_visible_translation = expected_anchor_offset * GATE_MIN_VISIBLE_TRANSLATION_RATIO
                if visible_translation < min_visible_translation:
                    failures.append(
                        f"{label}: visible opposite-sign translation {visible_translation}px below "
                        f"{min_visible_translation:.1f}px minimum for expected anchor offset "
                        f"{expected_anchor_offset:.1f}px"
                    )
    return failures


def gate_probe_results(results: list[dict[str, Any]]) -> int:
    failures = evaluate_gate_results(results)
    if not failures:
        print("gate=pass")
        return 0
    print("gate=fail")
    for failure in failures:
        print(f"gateFailure={failure}")
    return GATE_EXIT_CODE


def write_sweep_report(output_root: Path, results: list[dict[str, Any]]) -> None:
    output_root.mkdir(parents=True, exist_ok=True)
    (output_root / "sweep-report.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Arona Mouse Parallax Probe Sweep",
        "",
        "| Window size | Classification | Left/center RMSE | Right/center RMSE | Parallax layers | Opposite-sign regions |",
        "| --- | --- | ---: | ---: | ---: | ---: |",
    ]
    contact_images: list[tuple[str, Path]] = []
    for result in results:
        window_size = str(result.get("windowSize", ""))
        metrics = result.get("imageMetrics", {})
        metrics = metrics if isinstance(metrics, dict) else {}
        motion_regions = result.get("motionRegions", {})
        motion_regions = motion_regions if isinstance(motion_regions, dict) else {}
        opposite_count = sum(
            1
            for region in motion_regions.values()
            if isinstance(region, dict) and region.get("classification") == "opposite-sign-motion"
        )
        lines.append(
            "| "
            f"`{window_size}` | "
            f"`{result.get('classification', '')}` | "
            f"`{float(metrics.get('leftCenterRmse', 0.0)):.8f}` | "
            f"`{float(metrics.get('rightCenterRmse', 0.0)):.8f}` | "
            f"{_parallax_layer_count(result)} | "
            f"{opposite_count} |"
        )
        output_path = Path(str(result.get("outputRoot", output_root / safe_size_dir(window_size))))
        for name, _position in VARIANTS:
            contact_images.append((f"{window_size} {name}", output_path / name / "still.png"))

    lines.extend(["", f"- Contact sheet: `{output_root / 'sweep-contact-sheet.png'}`"])
    (output_root / "sweep-report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    if contact_images:
        write_contact_sheet(output_root / "sweep-contact-sheet.png", contact_images)


def run_probe(config: ProbeConfig, *, gate: bool = False) -> int:
    config.output_root.mkdir(parents=True, exist_ok=True)
    for name, mouse_position in VARIANTS:
        variant_dir = config.output_root / name
        variant_dir.mkdir(parents=True, exist_ok=True)
        command = build_harness_command(config, name, mouse_position)
        completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        (variant_dir / "command.txt").write_text(shlex.join(command) + "\n", encoding="utf-8")
        (variant_dir / "stdout.log").write_text(completed.stdout, encoding="utf-8")
        (variant_dir / "stderr.log").write_text(completed.stderr, encoding="utf-8")
        if completed.returncode != 0:
            print(f"{name} harness run failed with exit code {completed.returncode}", flush=True)
            return completed.returncode

    image_metrics = {
        "leftCenterRmse": image_rmse(config.output_root / "left" / "still.png",
                                     config.output_root / "center" / "still.png"),
        "rightCenterRmse": image_rmse(config.output_root / "right" / "still.png",
                                      config.output_root / "center" / "still.png"),
    }
    manifests = {
        name: load_mouse_parallax_manifest(config.output_root / name / "effect-captures" / "manifest.json")
        for name, _position in VARIANTS
    }
    classification = classify_probe(
        manifests["left"],
        manifests["center"],
        manifests["right"],
        image_metrics,
    )
    write_contact_sheet(
        config.output_root / "contact-sheet.png",
        [(name, config.output_root / name / "still.png") for name, _position in VARIANTS],
    )
    write_reports(config.output_root, classification, image_metrics)
    print(f"classification={classification}")
    print(f"contactSheet={config.output_root / 'contact-sheet.png'}")
    print(f"report={config.output_root / 'report.md'}")
    if gate:
        report = json.loads((config.output_root / "report.json").read_text(encoding="utf-8"))
        return gate_probe_results([report])
    return 0


def run_probe_sweep(config: ProbeConfig, window_sizes: list[str], *, gate: bool = False) -> int:
    results: list[dict[str, Any]] = []
    for window_size in window_sizes:
        sized_config = config_for_window_size(config, window_size)
        code = run_probe(sized_config)
        if code != 0:
            return code
        report = json.loads((sized_config.output_root / "report.json").read_text(encoding="utf-8"))
        results.append({"windowSize": window_size, "outputRoot": str(sized_config.output_root), **report})
    write_sweep_report(config.output_root, results)
    if gate:
        return gate_probe_results(results)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run Arona left/center/right synthetic mouse parallax diagnostics.")
    parser.add_argument("--scene-id", default="3228578419")
    parser.add_argument("--source", required=True)
    parser.add_argument("--assets", required=True)
    parser.add_argument("--harness", default=str(ROOT / "build/native/scene_harness/yakkai_scene_harness"))
    parser.add_argument("--output-root", default=str(DEFAULT_OUTPUT_ROOT))
    parser.add_argument("--capture-delay-ms", type=int, default=10000)
    parser.add_argument("--window-size", default="1600x900")
    parser.add_argument("--window-sizes", default="")
    parser.add_argument("--capture-layers", default="405")
    parser.add_argument("--scene-properties-json", default="")
    parser.add_argument("--gate", action="store_true", help="fail with exit code 3 unless strict local gate passes")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    config = ProbeConfig(
        repo_root=ROOT,
        harness=Path(args.harness),
        source=Path(args.source),
        assets=Path(args.assets),
        output_root=Path(args.output_root),
        scene_id=args.scene_id,
        capture_delay_ms=max(0, args.capture_delay_ms),
        window_size=args.window_size,
        capture_layers=args.capture_layers,
        scene_properties_json=args.scene_properties_json,
    )
    try:
        window_sizes = parse_window_sizes(args.window_sizes) if args.window_sizes else [args.window_size]
    except ValueError as exc:
        parser.error(str(exc))
    if args.window_sizes:
        return run_probe_sweep(config, window_sizes, gate=args.gate)
    return run_probe(config, gate=args.gate)


if __name__ == "__main__":
    raise SystemExit(main())
