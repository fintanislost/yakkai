#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import io
import json
import math
import re
import shutil
import zipfile
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image


REQUIRED_VARIANTS = {
    "day": "1",
    "sunset": "2",
    "night": "3",
}

DELTA_MAGNITUDE_THRESHOLD = 1.0 / 255.0

REQUIRED_VARIANT_FILES = [
    "effect-input-before-visible-effects.png",
    "prefix-3-after-first-lut-pair.png",
    "prefix-7-after-visible-effect-7.png",
    "final-publish-input.png",
    "default-before-final-publish.png",
    "default-after-final-publish.png",
    "metadata.json",
    "final_publish_state.csv",
    "pixel-history-lower-ribbon.csv",
    "pixel-history-transparent-edge.csv",
]


class FreshPublishError(RuntimeError):
    pass


def zip_root(zip_file: zipfile.ZipFile) -> str:
    roots = sorted({name.split("/", 1)[0] for name in zip_file.namelist() if "/" in name})
    if "layer405_final_publish_composite_fresh" not in roots:
        raise FreshPublishError("archive root layer405_final_publish_composite_fresh not found")
    return "layer405_final_publish_composite_fresh"


def read_json(zip_file: zipfile.ZipFile, name: str) -> dict[str, Any]:
    return json.loads(zip_file.read(name).decode("utf-8"))


def read_csv(zip_file: zipfile.ZipFile, name: str) -> list[dict[str, str]]:
    return list(csv.DictReader(io.StringIO(zip_file.read(name).decode("utf-8-sig"))))


def image_size(zip_file: zipfile.ZipFile, name: str) -> list[int]:
    with Image.open(io.BytesIO(zip_file.read(name))) as image:
        return [image.width, image.height]


def parse_write_mask(blend_state: str) -> int:
    match = re.search(r"writeMask=(\d+)", blend_state)
    if not match:
        raise FreshPublishError(f"writeMask missing from blend state: {blend_state}")
    return int(match.group(1))


def write_mask_channels(mask: int) -> str:
    channels = ""
    if mask & 1:
        channels += "R"
    if mask & 2:
        channels += "G"
    if mask & 4:
        channels += "B"
    if mask & 8:
        channels += "A"
    return channels or "none"


def parse_rgba(value: str) -> list[float]:
    parts = [float(part) for part in value.split()]
    if len(parts) != 4:
        raise FreshPublishError(f"expected four RGBA values, got {value!r}")
    return parts


def final_pixel_history_row(rows: list[dict[str, str]], replay_event: int) -> dict[str, Any]:
    for row in reversed(rows):
        if int(row["event"]) == replay_event:
            return {
                "finalEvent": replay_event,
                "passed": int(row["passed"]),
                "shaderOutRgba": parse_rgba(row["shader_out_rgba"]),
                "preModRgba": parse_rgba(row["pre_mod_rgba"]),
                "postModRgba": parse_rgba(row["post_mod_rgba"]),
                "flags": row.get("flags", ""),
            }
    raise FreshPublishError(f"pixel history missing final event {replay_event}")


def validate_manifest(manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    if manifest.get("status") != "complete_fresh_live_rdc_labeled_variants":
        raise FreshPublishError(f"unexpected source manifest status: {manifest.get('status')}")
    captures = {capture.get("variant"): capture for capture in manifest.get("captures", [])}
    missing = [variant for variant in REQUIRED_VARIANTS if variant not in captures]
    if missing:
        raise FreshPublishError(f"missing required variants: {', '.join(missing)}")
    for variant, timeofday in REQUIRED_VARIANTS.items():
        capture = captures[variant]
        if capture.get("sourceType") != "fresh-live-rdc":
            raise FreshPublishError(f"{variant} sourceType expected fresh-live-rdc")
        if capture.get("variantLabelSource") != "capture-session-label":
            raise FreshPublishError(f"{variant} variantLabelSource expected capture-session-label")
        if capture.get("variantMappingStatus") != "labeled-by-capture-session":
            raise FreshPublishError(f"{variant} variantMappingStatus expected labeled-by-capture-session")
        if str(capture.get("timeofday")) != timeofday:
            raise FreshPublishError(f"{variant} timeofday expected {timeofday}")
    return captures


def validate_capture_final_publish(variant: str, capture: dict[str, Any], final_publish: dict[str, Any]) -> None:
    checks = [
        ("finalPublishReplayEventId", int(final_publish["replayEventId"])),
        ("finalPublishDrawId", int(final_publish["drawId"])),
        ("finalPublishInputResourceId", int(final_publish["srvs"][0]["resourceId"])),
    ]
    for field, metadata_value in checks:
        if field not in capture:
            raise FreshPublishError(f"{variant} source manifest missing {field}")
        manifest_value = int(capture[field])
        if manifest_value != metadata_value:
            raise FreshPublishError(
                f"{variant} {field} mismatch: source manifest has {manifest_value}, metadata has {metadata_value}"
            )


def load_variant(zip_file: zipfile.ZipFile, root: str, capture: dict[str, Any]) -> dict[str, Any]:
    variant = capture["variant"]
    variant_root = f"{root}/{variant}"
    names = set(zip_file.namelist())
    for filename in REQUIRED_VARIANT_FILES:
        path = f"{variant_root}/{filename}"
        if path not in names:
            raise FreshPublishError(f"{variant} missing {filename}")

    metadata = read_json(zip_file, f"{variant_root}/metadata.json")
    final_publish = metadata["finalPublish"]
    validate_capture_final_publish(variant, capture, final_publish)
    write_mask = parse_write_mask(final_publish["blendState"])
    if write_mask != 7:
        raise FreshPublishError(f"{variant} final publish writeMask expected 7, got {write_mask}")

    replay_event = int(final_publish["replayEventId"])
    srv0 = final_publish["srvs"][0]
    if srv0.get("slot") != 0:
        raise FreshPublishError(f"{variant} final publish first SRV is not slot 0")

    state_rows = read_csv(zip_file, f"{variant_root}/final_publish_state.csv")
    lower_history = read_csv(zip_file, f"{variant_root}/pixel-history-lower-ribbon.csv")
    transparent_history = read_csv(zip_file, f"{variant_root}/pixel-history-transparent-edge.csv")
    return {
        "variant": variant,
        "timeofday": str(capture["timeofday"]),
        "sourceType": capture["sourceType"],
        "variantLabelSource": capture["variantLabelSource"],
        "selectedFrame": capture.get("selectedFrame"),
        "images": {
            filename: {
                "path": f"{variant_root}/{filename}",
                "dimensions": image_size(zip_file, f"{variant_root}/{filename}"),
            }
            for filename in REQUIRED_VARIANT_FILES
            if filename.endswith(".png")
        },
        "finalPublish": {
            "replayEventId": replay_event,
            "drawId": int(final_publish["drawId"]),
            "blendState": final_publish["blendState"],
            "writeMask": write_mask,
            "writeMaskChannels": write_mask_channels(write_mask),
            "renderTargetResourceId": int(final_publish["renderTargetResourceId"]),
            "renderTargetFormat": final_publish["renderTargetFormat"],
            "renderTargetDimensions": list(final_publish["renderTargetDimensions"]),
            "srv0ResourceId": int(srv0["resourceId"]),
            "srv0Format": srv0["format"],
            "srv0Dimensions": list(srv0["dimensions"]),
            "stateCsvRows": state_rows,
        },
        "pixelHistory": {
            "lowerRibbon": final_pixel_history_row(lower_history, replay_event),
            "transparentEdge": final_pixel_history_row(transparent_history, replay_event),
        },
    }


def load_windows_package(path: Path) -> dict[str, Any]:
    with zipfile.ZipFile(path) as zip_file:
        root = zip_root(zip_file)
        manifest = read_json(zip_file, f"{root}/source_manifest.json")
        captures = validate_manifest(manifest)
        variants = [load_variant(zip_file, root, captures[name]) for name in REQUIRED_VARIANTS]
    return {
        "status": "complete",
        "sourceArchive": str(path),
        "variants": variants,
    }


def extract_windows_package(path: Path, output_dir: Path) -> Path:
    extract_dir = output_dir / "windows_extract"
    if extract_dir.exists():
        shutil.rmtree(extract_dir)
    extract_dir.mkdir(parents=True, exist_ok=True)
    extract_root = extract_dir.resolve()

    with zipfile.ZipFile(path) as zip_file:
        root = zip_root(zip_file)
        for member in zip_file.infolist():
            member_path = Path(member.filename)
            if member_path.is_absolute() or ".." in member_path.parts:
                raise FreshPublishError(f"refuses to extract outside output: {member.filename}")
            destination = (extract_dir / member.filename).resolve()
            try:
                destination.relative_to(extract_root)
            except ValueError as error:
                raise FreshPublishError(f"refuses to extract outside output: {member.filename}") from error
            if member.is_dir():
                destination.mkdir(parents=True, exist_ok=True)
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            with zip_file.open(member) as source, destination.open("wb") as target:
                shutil.copyfileobj(source, target)

    return extract_dir / root


def rgba_array(path: Path, size: tuple[int, int] | None = None) -> np.ndarray:
    with Image.open(path) as image:
        rgba_image = image.convert("RGBA")
        if size is not None:
            rgba_image = rgba_image.resize(size, Image.Resampling.BILINEAR)
        return np.asarray(rgba_image, dtype=np.float32) / 255.0


def sample_rgba_at_default_coordinate(
    image_path: Path,
    coordinate: list[int],
    coordinate_dimensions: list[int],
) -> dict[str, Any]:
    if len(coordinate) != 2:
        raise FreshPublishError(f"default coordinate must contain two values, got {coordinate!r}")
    if len(coordinate_dimensions) != 2:
        raise FreshPublishError(f"default coordinate dimensions must contain two values, got {coordinate_dimensions!r}")
    source_width, source_height = coordinate_dimensions
    if source_width <= 0 or source_height <= 0:
        raise FreshPublishError(f"default coordinate dimensions must be positive, got {coordinate_dimensions!r}")
    with Image.open(image_path) as image:
        rgba_image = image.convert("RGBA")
        image_width, image_height = rgba_image.size
        x = min(image_width - 1, max(0, round(coordinate[0] * image_width / source_width)))
        y = min(image_height - 1, max(0, round(coordinate[1] * image_height / source_height)))
        pixel = rgba_image.getpixel((x, y))
    return {
        "imagePath": str(image_path),
        "imageDimensions": [image_width, image_height],
        "coordinateDimensions": coordinate_dimensions,
        "sourceCoordinate": coordinate,
        "samplePixel": [x, y],
        "rgba": [channel / 255.0 for channel in pixel],
    }


def vector_rmse(left: list[float], right: list[float]) -> float:
    return float(np.sqrt(np.mean(np.square(np.asarray(left, dtype=np.float32) - np.asarray(right, dtype=np.float32)))))


def rgb_delta_metrics(
    windows_pre: list[float],
    windows_post: list[float],
    yakkai_pre: list[float],
    yakkai_post: list[float],
) -> dict[str, Any]:
    windows_delta = [windows_post[index] - windows_pre[index] for index in range(3)]
    yakkai_delta = [yakkai_post[index] - yakkai_pre[index] for index in range(3)]
    windows_norm = float(np.linalg.norm(np.asarray(windows_delta, dtype=np.float32)))
    yakkai_norm = float(np.linalg.norm(np.asarray(yakkai_delta, dtype=np.float32)))
    if windows_norm <= 1e-8 or yakkai_norm <= 1e-8:
        delta_cosine = 1.0 if windows_norm <= 1e-8 and yakkai_norm <= 1e-8 else 0.0
    else:
        delta_cosine = float(np.dot(windows_delta, yakkai_delta) / (windows_norm * yakkai_norm))
        delta_cosine = min(1.0, max(-1.0, delta_cosine))
    magnitude_ratio = yakkai_norm / windows_norm if windows_norm > 1e-8 else None
    delta_rmse = vector_rmse(windows_delta, yakkai_delta)
    post_rmse = vector_rmse(windows_post[:3], yakkai_post[:3])
    pre_rmse = vector_rmse(windows_pre[:3], yakkai_pre[:3])
    if delta_rmse <= 0.03 and delta_cosine >= 0.95:
        classification = "default-delta-close"
    elif delta_cosine >= 0.80 and delta_rmse <= 0.12:
        classification = "default-delta-directional-match"
    else:
        classification = "default-delta-mismatch"
    return {
        "classification": classification,
        "windowsDeltaRgb": windows_delta,
        "yakkaiDeltaRgb": yakkai_delta,
        "preRmse": round(pre_rmse, 8),
        "postRmse": round(post_rmse, 8),
        "deltaRmse": round(delta_rmse, 8),
        "deltaCosine": round(delta_cosine, 8),
        "magnitudeRatio": None if magnitude_ratio is None else round(magnitude_ratio, 8),
    }


def validate_delta_sample_pixel(pixel: list[int]) -> list[int]:
    if not isinstance(pixel, (list, tuple)) or len(pixel) != 2:
        raise FreshPublishError(f"delta sample pixel must contain two integer values, got {pixel!r}")
    sample_pixel = []
    for value in pixel:
        if isinstance(value, bool) or not isinstance(value, (int, np.integer)):
            raise FreshPublishError(f"delta sample pixel must contain integer values, got {pixel!r}")
        sample_pixel.append(int(value))
    return sample_pixel


def rgb_delta_map(before_path: Path, after_path: Path) -> dict[str, Any]:
    with Image.open(before_path) as before_image, Image.open(after_path) as after_image:
        before = np.asarray(before_image.convert("RGB"), dtype=np.int16)
        after = np.asarray(after_image.convert("RGB"), dtype=np.int16)
    if before.shape != after.shape:
        raise FreshPublishError(f"delta map image shape mismatch: {before_path} {before.shape} vs {after_path} {after.shape}")
    rgb_delta = after.astype(np.float32)
    rgb_delta -= before
    np.square(rgb_delta, out=rgb_delta)
    magnitude = np.sqrt(np.sum(rgb_delta, axis=2, dtype=np.float32)) / 255.0
    peak_index = np.unravel_index(int(np.argmax(magnitude)), magnitude.shape)
    peak_y, peak_x = int(peak_index[0]), int(peak_index[1])

    def sample_magnitude(pixel: list[int]) -> float:
        sample_x, sample_y = validate_delta_sample_pixel(pixel)
        x = min(magnitude.shape[1] - 1, max(0, sample_x))
        y = min(magnitude.shape[0] - 1, max(0, sample_y))
        return round(float(magnitude[y, x]), 8)

    return {
        "beforePath": str(before_path),
        "afterPath": str(after_path),
        "dimensions": [int(magnitude.shape[1]), int(magnitude.shape[0])],
        "magnitude": magnitude,
        "peakPixel": [peak_x, peak_y],
        "peakMagnitude": round(float(magnitude[peak_y, peak_x]), 8),
        "nonzeroPixelCount": int(np.count_nonzero(magnitude >= DELTA_MAGNITUDE_THRESHOLD)),
        "sampleMagnitude": sample_magnitude,
    }


def find_nearest_delta_pixel(
    delta: dict[str, Any],
    sample_pixel: list[int],
    min_magnitude: float = DELTA_MAGNITUDE_THRESHOLD,
) -> dict[str, Any]:
    magnitude = delta["magnitude"]
    sample_x, sample_y = validate_delta_sample_pixel(sample_pixel)
    nearest_pixel = None
    nearest_distance_squared = math.inf
    nearest_magnitude = 0.0
    for y in range(magnitude.shape[0]):
        xs = np.flatnonzero(magnitude[y] >= min_magnitude)
        if len(xs) == 0:
            continue
        distances_squared = np.square(xs - sample_x) + (y - sample_y) ** 2
        index = int(np.argmin(distances_squared))
        distance_squared = float(distances_squared[index])
        if distance_squared < nearest_distance_squared:
            x = int(xs[index])
            nearest_pixel = [x, y]
            nearest_distance_squared = distance_squared
            nearest_magnitude = float(magnitude[y, x])
    if nearest_pixel is None:
        return {
            "classification": "no-meaningful-delta",
            "samplePixel": [sample_x, sample_y],
            "sampleMagnitude": delta["sampleMagnitude"]([sample_x, sample_y]),
            "nearestPixel": None,
            "nearestMagnitude": 0.0,
            "nearestDistancePixels": None,
            "peakPixel": delta["peakPixel"],
            "peakMagnitude": delta["peakMagnitude"],
            "nonzeroPixelCount": delta["nonzeroPixelCount"],
        }
    return {
        "classification": "nearest-delta-found",
        "samplePixel": [sample_x, sample_y],
        "sampleMagnitude": delta["sampleMagnitude"]([sample_x, sample_y]),
        "nearestPixel": nearest_pixel,
        "nearestMagnitude": round(nearest_magnitude, 8),
        "nearestDistancePixels": round(math.sqrt(nearest_distance_squared), 4),
        "peakPixel": delta["peakPixel"],
        "peakMagnitude": delta["peakMagnitude"],
        "nonzeroPixelCount": delta["nonzeroPixelCount"],
    }


def classify_default_delta_location(search: dict[str, Any], near_radius: float) -> str:
    sample_magnitude = float(search["sampleMagnitude"])
    if sample_magnitude >= DELTA_MAGNITUDE_THRESHOLD:
        return "delta-at-windows-sample"
    if search["nearestPixel"] is None:
        return "missing-default-delta"
    if float(search["nearestDistancePixels"]) <= near_radius:
        return "delta-nearby"
    return "delta-elsewhere"


def build_default_delta_locator_sample(
    oracle_sample: dict[str, Any],
    before_path: Path,
    after_path: Path,
    near_radius: float = 96.0,
) -> dict[str, Any]:
    sample_pixel = oracle_sample["beforeSample"]["samplePixel"]
    delta = rgb_delta_map(before_path, after_path)
    search = find_nearest_delta_pixel(delta, sample_pixel)
    classification = classify_default_delta_location(search, near_radius)
    return {
        "sampleName": oracle_sample["sampleName"],
        "classification": classification,
        "samplePixel": sample_pixel,
        "sampleMagnitude": search["sampleMagnitude"],
        "nearestPixel": search["nearestPixel"],
        "nearestMagnitude": search["nearestMagnitude"],
        "nearestDistancePixels": search["nearestDistancePixels"],
        "peakPixel": search["peakPixel"],
        "peakMagnitude": search["peakMagnitude"],
        "nonzeroPixelCount": search["nonzeroPixelCount"],
        "coordinate": oracle_sample["coordinate"],
        "coordinateDimensions": oracle_sample["coordinateDimensions"],
    }


def combine_locator_classifications(samples: dict[str, dict[str, Any]]) -> str:
    classifications = [sample["classification"] for sample in samples.values()]
    if any(classification == "delta-at-windows-sample" for classification in classifications):
        return "delta-at-windows-sample"
    if any(classification == "delta-nearby" for classification in classifications):
        return "delta-nearby"
    if any(classification == "delta-elsewhere" for classification in classifications):
        return "delta-elsewhere"
    return "missing-default-delta"


def build_boundary_stage_locator_sample(
    oracle_sample: dict[str, Any],
    before_path: Path,
    after_path: Path,
    near_radius: float = 96.0,
) -> dict[str, Any]:
    return build_default_delta_locator_sample(oracle_sample, before_path, after_path, near_radius=near_radius)


def build_yakkai_default_delta_locator(
    report: dict[str, Any],
    yakkai_variants: dict[str, dict[str, Any]],
) -> dict[str, dict[str, Any]]:
    if "yakkaiDefaultDeltaOracle" not in report:
        raise FreshPublishError("yakkaiDefaultDeltaOracle must be built before yakkaiDefaultDeltaLocator")
    locator: dict[str, dict[str, Any]] = {}
    for variant_name in REQUIRED_VARIANTS:
        variant_oracle = report["yakkaiDefaultDeltaOracle"][variant_name]
        captures = yakkai_variants[variant_name]["captures"]
        samples = {
            sample_name: build_default_delta_locator_sample(
                oracle_sample,
                Path(captures["default-before-effect"]),
                Path(captures["default-after-effect"]),
            )
            for sample_name, oracle_sample in variant_oracle["samples"].items()
        }
        boundary_samples = {}
        if "final-publish" in captures:
            boundary_samples = {
                sample_name: build_boundary_stage_locator_sample(
                    oracle_sample,
                    Path(captures["default-after-effect"]),
                    Path(captures["final-publish"]),
                )
                for sample_name, oracle_sample in variant_oracle["samples"].items()
            }
        locator[variant_name] = {
            "classification": combine_locator_classifications(samples),
            "samples": samples,
            "boundaryAfterToFinalPublish": {
                "classification": (
                    combine_locator_classifications(boundary_samples)
                    if boundary_samples
                    else "missing-final-publish-capture"
                ),
                "samples": boundary_samples,
            },
        }
    return locator


def rmse(left: np.ndarray, right: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(left - right))))


def alpha_weighted_rmse(left: np.ndarray, right: np.ndarray) -> float:
    alpha = np.maximum(left[..., 3:4], right[..., 3:4])
    if float(np.max(alpha)) <= 0.0:
        return rmse(left, right)
    rgb_delta = np.square(left[..., :3] - right[..., :3]) * alpha
    return float(np.sqrt(np.sum(rgb_delta) / max(float(np.sum(alpha)) * 3.0, 1.0)))


def exact_ratio_crop_candidates(
    local_width: int,
    local_height: int,
    target_width: int,
    target_height: int,
) -> list[dict[str, Any]]:
    target_ratio = target_width / target_height
    candidates: list[dict[str, Any]] = []
    for crop_height in range(max(4, local_height // 8), local_height + 1):
        crop_width = round(crop_height * target_ratio)
        if crop_width < 4 or crop_width > local_width:
            continue
        for y in range(0, local_height - crop_height + 1):
            for x in range(0, local_width - crop_width + 1):
                candidates.append({"cropPixels": [x, y, crop_width, crop_height]})
    return candidates


def bounded_ratio_crop_candidates(
    local_width: int,
    local_height: int,
    target_width: int,
    target_height: int,
) -> list[dict[str, Any]]:
    if local_width <= 96 and local_height <= 96:
        return exact_ratio_crop_candidates(local_width, local_height, target_width, target_height)

    target_ratio = target_width / target_height
    scales = [0.50, 0.55, 0.60, 0.615, 0.65, 0.70, 0.75, 0.80, 0.90, 1.00]
    offsets = [0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0]
    seen: set[tuple[int, int, int, int]] = set()
    candidates = []
    for scale in scales:
        crop_width = max(4, min(local_width, round(local_width * scale)))
        crop_height = max(4, min(local_height, round(crop_width / target_ratio)))
        if crop_height > local_height:
            crop_height = local_height
            crop_width = max(4, min(local_width, round(crop_height * target_ratio)))
        margin_x = local_width - crop_width
        margin_y = local_height - crop_height
        for oy in offsets:
            for ox in offsets:
                x = round(margin_x * ox)
                y = round(margin_y * oy)
                key = (x, y, crop_width, crop_height)
                if key in seen:
                    continue
                seen.add(key)
                candidates.append({"cropPixels": [x, y, crop_width, crop_height]})
    return candidates


def score_registration_candidate(
    search_image: Image.Image,
    target_pixels: np.ndarray,
    compare_dimensions: tuple[int, int],
    crop_pixels: list[int],
) -> float:
    x, y, width, height = crop_pixels
    cropped = search_image.crop((x, y, x + width, y + height)).resize(
        compare_dimensions,
        Image.Resampling.BILINEAR,
    )
    candidate_pixels = np.asarray(cropped, dtype=np.float32) / 255.0
    return alpha_weighted_rmse(candidate_pixels, target_pixels)


def refined_ratio_crop_candidates(
    base_candidates: list[list[int]],
    local_width: int,
    local_height: int,
    target_width: int,
    target_height: int,
    radius: int = 4,
) -> list[list[int]]:
    target_ratio = target_width / target_height
    seen: set[tuple[int, int, int, int]] = set()
    candidates: list[list[int]] = []
    for base_x, base_y, base_width, _base_height in base_candidates:
        min_width = max(4, base_width - radius)
        max_width = min(local_width, base_width + radius)
        for width in range(min_width, max_width + 1):
            height = round(width / target_ratio)
            if height < 4 or height > local_height:
                continue
            min_x = max(0, base_x - radius)
            max_x = min(local_width - width, base_x + radius)
            min_y = max(0, base_y - radius)
            max_y = min(local_height - height, base_y + radius)
            for y in range(min_y, max_y + 1):
                for x in range(min_x, max_x + 1):
                    key = (x, y, width, height)
                    if key in seen:
                        continue
                    seen.add(key)
                    candidates.append([x, y, width, height])
    return candidates


def register_layer_local_to_default(local_path: Path, target_path: Path, max_size: int) -> dict[str, Any]:
    with Image.open(local_path) as local_image:
        local_image = local_image.convert("RGBA")
        original_local_image = local_image.copy()
        local_width, local_height = local_image.size
        with Image.open(target_path) as target_image:
            target_image = target_image.convert("RGBA")
            target_width, target_height = target_image.size
            compare_width = min(target_width, max_size)
            compare_height = max(1, round(target_height * (compare_width / target_width)))
            target_pixels = np.asarray(
                target_image.resize((compare_width, compare_height), Image.Resampling.BILINEAR),
                dtype=np.float32,
            ) / 255.0

        search_scale = min(1.0, max_size / max(local_width, local_height))
        search_width = max(1, round(local_width * search_scale))
        search_height = max(1, round(local_height * search_scale))
        search_image = local_image.resize((search_width, search_height), Image.Resampling.BILINEAR)

    candidates = bounded_ratio_crop_candidates(search_width, search_height, compare_width, compare_height)
    best: dict[str, Any] | None = None
    best_score = math.inf
    scored_candidates: list[tuple[float, list[int]]] = []

    def apply_score(crop_pixels: list[int], candidate_count: int) -> None:
        nonlocal best, best_score
        score = score_registration_candidate(
            search_image,
            target_pixels,
            (compare_width, compare_height),
            crop_pixels,
        )
        if score < best_score:
            x, y, width, height = crop_pixels
            sx = local_width / search_width
            sy = local_height / search_height
            best_score = score
            best = {
                "classification": (
                    "registered-layer-local-to-default" if score < 0.05 else "weak-or-unregistered-match"
                ),
                "metric": "alpha-weighted-rmse",
                "rmse": round(score, 8),
                "cropPixels": [
                    round(x * sx),
                    round(y * sy),
                    round(width * sx),
                    round(height * sy),
                ],
                "localDimensions": [local_width, local_height],
                "targetDimensions": [target_width, target_height],
                "compareDimensions": [compare_width, compare_height],
                "candidateCount": candidate_count,
            }

    for candidate in candidates:
        crop_pixels = candidate["cropPixels"]
        score = score_registration_candidate(
            search_image,
            target_pixels,
            (compare_width, compare_height),
            crop_pixels,
        )
        scored_candidates.append((score, crop_pixels))
        apply_score(crop_pixels, len(candidates))
    if best is None:
        raise FreshPublishError("no registration candidates generated")

    top_crops = [crop for _score, crop in sorted(scored_candidates, key=lambda item: item[0])[:12]]
    refined_candidates = refined_ratio_crop_candidates(
        top_crops,
        search_width,
        search_height,
        compare_width,
        compare_height,
    )
    total_candidate_count = len(candidates) + len(refined_candidates)
    for crop_pixels in refined_candidates:
        apply_score(crop_pixels, total_candidate_count)
    if best is not None:
        original_candidates = refined_ratio_crop_candidates(
            [best["cropPixels"]],
            local_width,
            local_height,
            compare_width,
            compare_height,
            radius=2,
        )
        for crop_pixels in original_candidates:
            score = score_registration_candidate(
                original_local_image,
                target_pixels,
                (compare_width, compare_height),
                crop_pixels,
            )
            if score < best_score:
                best_score = score
                best = {
                    "classification": (
                        "registered-layer-local-to-default" if score < 0.05 else "weak-or-unregistered-match"
                    ),
                    "metric": "alpha-weighted-rmse",
                    "rmse": round(score, 8),
                    "cropPixels": crop_pixels,
                    "localDimensions": [local_width, local_height],
                    "targetDimensions": [target_width, target_height],
                    "compareDimensions": [compare_width, compare_height],
                    "candidateCount": total_candidate_count,
                }
        best["candidateCount"] = total_candidate_count + len(original_candidates)
    return best


def yakkai_pass_states(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    states = manifest.get("debugEffectPassStates")
    if states is None:
        states = manifest.get("passStates", [])
    if not isinstance(states, list):
        return []
    return [state for state in states if isinstance(state, dict)]


def correlate_yakkai_final_publish_state(manifest: dict[str, Any]) -> dict[str, Any]:
    candidates = [
        state
        for state in yakkai_pass_states(manifest)
        if state.get("output") == "_rt_default"
        and str(state.get("blendMode")) == "1"
        and state.get("blendEnabled") is True
    ]
    if not candidates:
        return {
            "classification": "missing-yakkai-final-publish-pass-state",
            "selectedPass": None,
            "candidateCount": 0,
        }

    selected = candidates[-1]
    try:
        color_mask_bits = int(selected.get("colorMaskBits", -1))
    except (TypeError, ValueError):
        color_mask_bits = -1
    if color_mask_bits == 7:
        classification = "yakkai-final-publish-rgb-mask"
    elif color_mask_bits == 15:
        classification = "yakkai-final-publish-rgba-mask"
    else:
        classification = "yakkai-final-publish-unexpected-mask"
    return {
        "classification": classification,
        "selectedPass": selected,
        "candidateCount": len(candidates),
    }


def find_yakkai_manifest(root: Path) -> Path:
    matches: list[Path] = []
    for path in sorted(root.rglob("manifest.json")):
        try:
            manifest = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            continue
        captures = manifest.get("captures", [])
        if not isinstance(captures, list):
            continue
        has_layer_405 = any(
            isinstance(capture, dict)
            and isinstance(capture.get("layer"), dict)
            and capture["layer"].get("layerId") == 405
            for capture in captures
        )
        if has_layer_405 and yakkai_pass_states(manifest):
            matches.append(path)
    if not matches:
        raise FreshPublishError(f"no layer 405 effect manifest with pass states under {root}")
    return matches[-1]


def layer_capture_stage_paths(manifest_path: Path, variant: str, layer_id: int = 405) -> dict[str, str]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    stages: dict[str, str] = {}
    for capture in manifest.get("captures", []):
        if not isinstance(capture, dict):
            continue
        layer = capture.get("layer", {})
        if not isinstance(layer, dict) or layer.get("layerId") != layer_id:
            continue
        if capture.get("completed") is False:
            continue
        stage = capture.get("stage")
        path = capture.get("path")
        if isinstance(stage, str) and isinstance(path, str):
            stages[stage] = path
    for required_stage in ["default-before-effect", "default-after-effect"]:
        if required_stage not in stages:
            raise FreshPublishError(f"{variant} missing layer {layer_id} capture stage {required_stage}")
        if not Path(stages[required_stage]).exists():
            raise FreshPublishError(f"{variant} capture path does not exist for {required_stage}: {stages[required_stage]}")
    return stages


def load_yakkai_variant_manifests(root: Path) -> dict[str, dict[str, Any]]:
    summary_path = root / "summary.json"
    if not summary_path.exists():
        raise FreshPublishError(f"missing Yakkai summary: {summary_path}")
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    variants: dict[str, dict[str, Any]] = {}
    for variant in summary.get("variants", []):
        if not isinstance(variant, dict):
            continue
        name = variant.get("name")
        manifest_path = variant.get("effectManifest")
        if not isinstance(name, str) or not isinstance(manifest_path, str):
            continue
        if name not in REQUIRED_VARIANTS:
            continue
        manifest = Path(manifest_path)
        variants[name] = {
            "manifestPath": str(manifest),
            "captures": layer_capture_stage_paths(manifest, name),
            "summary": variant,
        }
    missing = [variant for variant in REQUIRED_VARIANTS if variant not in variants]
    if missing:
        raise FreshPublishError(f"missing Yakkai variants in summary: {', '.join(missing)}")
    return variants


DEFAULT_DELTA_SAMPLE_COLUMNS = {
    "lowerRibbon": ("lower_ribbon_x", "lower_ribbon_y"),
    "transparentEdge": ("transparent_edge_x", "transparent_edge_y"),
}


def state_row_int(row: dict[str, str], field: str) -> int:
    try:
        value = float(row[field])
    except (KeyError, TypeError, ValueError) as error:
        raise FreshPublishError(f"final publish state row missing numeric {field}") from error
    if not math.isfinite(value) or not value.is_integer():
        raise FreshPublishError(f"final publish state row has non-integer {field}: {row[field]!r}")
    return int(value)


def sample_definition_from_state_rows(
    state_rows: list[dict[str, str]],
    variant: str | None = None,
    replay_event: int | None = None,
    draw_id: int | None = None,
) -> dict[str, dict[str, Any]]:
    required_fields = ["rt_width", "rt_height"]
    for x_field, y_field in DEFAULT_DELTA_SAMPLE_COLUMNS.values():
        required_fields.extend([x_field, y_field])
    require_exact_match = variant is not None or replay_event is not None or draw_id is not None
    complete_rows = []
    for row in state_rows:
        if not isinstance(row, dict):
            continue
        if variant is not None and row.get("variant") != variant:
            continue
        if replay_event is not None:
            try:
                if state_row_int(row, "replay_event") != int(replay_event):
                    continue
            except FreshPublishError:
                continue
        if draw_id is not None:
            try:
                if state_row_int(row, "draw_id") != int(draw_id):
                    continue
            except FreshPublishError:
                continue
        if all(row.get(field) not in (None, "") for field in required_fields):
            complete_rows.append(row)
    if not complete_rows:
        if require_exact_match:
            raise FreshPublishError(
                "final publish state rows missing default-delta sample coordinates "
                f"for variant={variant!r} replay_event={replay_event!r} draw_id={draw_id!r}"
            )
        raise FreshPublishError("final publish state rows missing default-delta sample coordinates")
    if require_exact_match and len(complete_rows) > 1:
        raise FreshPublishError(
            "ambiguous final publish state rows for "
            f"variant={variant!r} replay_event={replay_event!r} draw_id={draw_id!r}"
        )
    row = complete_rows[0]
    coordinate_dimensions = [
        state_row_int(row, "rt_width"),
        state_row_int(row, "rt_height"),
    ]
    return {
        sample_name: {
            "coordinate": [
                state_row_int(row, x_field),
                state_row_int(row, y_field),
            ],
            "coordinateDimensions": coordinate_dimensions,
            "stateFields": [x_field, y_field],
        }
        for sample_name, (x_field, y_field) in DEFAULT_DELTA_SAMPLE_COLUMNS.items()
    }


def build_yakkai_default_delta_sample(
    windows_variant: dict[str, Any],
    yakkai_variant: dict[str, Any],
    sample_name: str,
    sample_definition: dict[str, Any],
) -> dict[str, Any]:
    pixel_history = windows_variant.get("pixelHistory", {}).get(sample_name)
    if not isinstance(pixel_history, dict):
        raise FreshPublishError(f"{windows_variant.get('variant')} missing pixel history for {sample_name}")
    before_sample = sample_rgba_at_default_coordinate(
        Path(yakkai_variant["captures"]["default-before-effect"]),
        sample_definition["coordinate"],
        sample_definition["coordinateDimensions"],
    )
    after_sample = sample_rgba_at_default_coordinate(
        Path(yakkai_variant["captures"]["default-after-effect"]),
        sample_definition["coordinate"],
        sample_definition["coordinateDimensions"],
    )
    if (
        before_sample["imageDimensions"] != after_sample["imageDimensions"]
        or before_sample["samplePixel"] != after_sample["samplePixel"]
    ):
        raise FreshPublishError(
            f"{windows_variant.get('variant')} {sample_name} before/after sample pixel mismatch: "
            f"before pixel {before_sample['samplePixel']} in {before_sample['imageDimensions']}, "
            f"after pixel {after_sample['samplePixel']} in {after_sample['imageDimensions']}"
        )
    metrics = rgb_delta_metrics(
        pixel_history["preModRgba"],
        pixel_history["postModRgba"],
        before_sample["rgba"],
        after_sample["rgba"],
    )
    return {
        "sampleName": sample_name,
        "coordinate": sample_definition["coordinate"],
        "coordinateDimensions": sample_definition["coordinateDimensions"],
        "windowsPreRgba": pixel_history["preModRgba"],
        "windowsPostRgba": pixel_history["postModRgba"],
        "beforeSample": before_sample,
        "afterSample": after_sample,
        "metrics": metrics,
    }


def combine_delta_classifications(samples: dict[str, dict[str, Any]]) -> str:
    classifications = [sample["metrics"]["classification"] for sample in samples.values()]
    if not classifications:
        raise FreshPublishError("no default-delta samples were produced")
    if all(classification == "default-delta-close" for classification in classifications):
        return "default-delta-close"
    if all(
        classification in {"default-delta-close", "default-delta-directional-match"}
        for classification in classifications
    ):
        return "default-delta-directional-match"
    return "default-delta-mismatch"


def build_yakkai_default_delta_oracle(
    report: dict[str, Any],
    yakkai_variants: dict[str, dict[str, Any]],
) -> dict[str, dict[str, Any]]:
    windows_variants = {
        variant["variant"]: variant
        for variant in report.get("variants", [])
        if isinstance(variant, dict) and isinstance(variant.get("variant"), str)
    }
    oracle: dict[str, dict[str, Any]] = {}
    for variant_name in REQUIRED_VARIANTS:
        if variant_name not in windows_variants:
            raise FreshPublishError(f"missing Windows variant in report: {variant_name}")
        if variant_name not in yakkai_variants:
            raise FreshPublishError(f"missing Yakkai variant manifest: {variant_name}")
        windows_variant = windows_variants[variant_name]
        sample_definitions = sample_definition_from_state_rows(
            windows_variant["finalPublish"]["stateCsvRows"],
            variant_name,
            windows_variant["finalPublish"]["replayEventId"],
            windows_variant["finalPublish"]["drawId"],
        )
        samples = {
            sample_name: build_yakkai_default_delta_sample(
                windows_variant,
                yakkai_variants[variant_name],
                sample_name,
                sample_definition,
            )
            for sample_name, sample_definition in sample_definitions.items()
        }
        oracle[variant_name] = {
            "classification": combine_delta_classifications(samples),
            "manifestPath": yakkai_variants[variant_name]["manifestPath"],
            "captures": yakkai_variants[variant_name]["captures"],
            "samples": samples,
        }
    return oracle


def write_locator_crop(
    before_path: Path,
    after_path: Path,
    sample_pixel: list[int],
    output_path: Path,
    crop_radius: int = 48,
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(before_path) as before_image, Image.open(after_path) as after_image:
        before_rgba = before_image.convert("RGBA")
        after_rgba = after_image.convert("RGBA")
        if before_rgba.size != after_rgba.size:
            raise FreshPublishError(f"locator crop image size mismatch: {before_path} vs {after_path}")
        x, y = validate_delta_sample_pixel(sample_pixel)
        left = max(0, x - crop_radius)
        top = max(0, y - crop_radius)
        right = min(before_rgba.width, x + crop_radius + 1)
        bottom = min(before_rgba.height, y + crop_radius + 1)
        before_crop = before_rgba.crop((left, top, right, bottom))
        after_crop = after_rgba.crop((left, top, right, bottom))
        delta = Image.fromarray(
            np.clip(
                np.abs(
                    np.asarray(after_crop, dtype=np.int16)[:, :, :3]
                    - np.asarray(before_crop, dtype=np.int16)[:, :, :3]
                )
                * 4,
                0,
                255,
            ).astype(np.uint8),
            mode="RGB",
        ).convert("RGBA")
        sheet = Image.new("RGBA", (before_crop.width * 3, before_crop.height), (0, 0, 0, 255))
        sheet.paste(before_crop, (0, 0))
        sheet.paste(after_crop, (before_crop.width, 0))
        sheet.paste(delta, (before_crop.width * 2, 0))
        sheet.save(output_path)


def write_summary(report: dict[str, Any], windows: Path, output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    (output / "summary.json").write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
    lines = [
        "# Arona Layer 405 Fresh Final-Publish Diagnostics",
        "",
        f"Source archive: `{windows}`",
        "",
        "| Variant | Time | Event | Draw | Write Mask | SRV0 | RT | Lower Ribbon Pre -> Post |",
        "| --- | ---: | ---: | ---: | --- | --- | --- | --- |",
    ]
    for variant in report["variants"]:
        lower = variant["pixelHistory"]["lowerRibbon"]
        lines.append(
            "| {variant} | {timeofday} | {event} | {draw} | {mask} | {srv} | {rt} | {pre} -> {post} |".format(
                variant=variant["variant"],
                timeofday=variant["timeofday"],
                event=variant["finalPublish"]["replayEventId"],
                draw=variant["finalPublish"]["drawId"],
                mask=variant["finalPublish"]["writeMaskChannels"],
                srv="x".join(str(value) for value in variant["finalPublish"]["srv0Dimensions"]),
                rt="x".join(str(value) for value in variant["finalPublish"]["renderTargetDimensions"]),
                pre=" ".join(f"{value:.6f}" for value in lower["preModRgba"]),
                post=" ".join(f"{value:.6f}" for value in lower["postModRgba"]),
            )
        )
    if "yakkaiFinalPublishState" in report:
        yakkai_state = report["yakkaiFinalPublishState"]
        lines.extend(
            [
                "",
                "## Yakkai Final-Publish State",
                "",
                f"Classification: `{yakkai_state['classification']}`",
            ]
        )
    if "windowsRegistration" in report:
        lines.extend(["", "## Windows Registration", ""])
        lines.append("| Variant | Input -> Default After | RMSE | Crop |")
        lines.append("| --- | --- | ---: | --- |")
        for variant, registration in report["windowsRegistration"].items():
            after = registration["finalInputToDefaultAfter"]
            lines.append(
                f"| {variant} | `{after['classification']}` | {after['rmse']:.6f} | `{after['cropPixels']}` |"
            )
    if "yakkaiDefaultDeltaOracle" in report:
        lines.extend(["", "## Yakkai Default-Delta Oracle", ""])
        lines.append("| Variant | Sample | Classification | Delta RMSE | Delta Cosine | Yakkai Pixel |")
        lines.append("| --- | --- | --- | ---: | ---: | --- |")
        for variant, result in report["yakkaiDefaultDeltaOracle"].items():
            for sample_name, sample in result["samples"].items():
                metrics = sample["metrics"]
                lines.append(
                    "| {variant} | {sample} | `{classification}` | {delta_rmse:.6f} | "
                    "{delta_cosine:.6f} | `{pixel}` |".format(
                        variant=variant,
                        sample=sample_name,
                        classification=metrics["classification"],
                        delta_rmse=metrics["deltaRmse"],
                        delta_cosine=metrics["deltaCosine"],
                        pixel=sample["beforeSample"]["samplePixel"],
                    )
                )
    if "yakkaiDefaultDeltaLocator" in report:
        lines.extend(["", "## Yakkai Default-Delta Locator", ""])
        lines.append("| Variant | Sample | Classification | Sample Mag | Nearest Pixel | Distance | Peak Pixel | Peak Mag |")
        lines.append("| --- | --- | --- | ---: | --- | ---: | --- | ---: |")
        for variant, result in report["yakkaiDefaultDeltaLocator"].items():
            for sample_name, sample in result["samples"].items():
                distance = sample["nearestDistancePixels"]
                distance_text = "" if distance is None else f"{distance:.4f}"
                lines.append(
                    "| {variant} | {sample_name} | `{classification}` | {sample_mag:.6f} | "
                    "`{nearest}` | {distance} | `{peak}` | {peak_mag:.6f} |".format(
                        variant=variant,
                        sample_name=sample_name,
                        classification=sample["classification"],
                        sample_mag=sample["sampleMagnitude"],
                        nearest=sample["nearestPixel"],
                        distance=distance_text,
                        peak=sample["peakPixel"],
                        peak_mag=sample["peakMagnitude"],
                    )
                )
    (output / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Compare Arona Layer 405 fresh Windows final-publish evidence.")
    parser.add_argument("--windows", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--yakkai-manifest", type=Path)
    parser.add_argument("--yakkai-root", type=Path)
    args = parser.parse_args(argv)
    if args.yakkai_manifest is not None and args.yakkai_root is not None:
        parser.error("--yakkai-manifest and --yakkai-root are mutually exclusive")

    report = load_windows_package(args.windows)
    args.output.mkdir(parents=True, exist_ok=True)
    extracted_root = extract_windows_package(args.windows, args.output)
    registrations = {}
    for variant in REQUIRED_VARIANTS:
        variant_root = extracted_root / variant
        registrations[variant] = {
            "finalInputToDefaultAfter": register_layer_local_to_default(
                variant_root / "final-publish-input.png",
                variant_root / "default-after-final-publish.png",
                max_size=512,
            ),
            "finalInputToDefaultBefore": register_layer_local_to_default(
                variant_root / "final-publish-input.png",
                variant_root / "default-before-final-publish.png",
                max_size=512,
            ),
        }
    report["windowsRegistration"] = registrations
    yakkai_variants = None
    if args.yakkai_root is not None:
        yakkai_variants = load_yakkai_variant_manifests(args.yakkai_root)
        args.yakkai_manifest = Path(yakkai_variants["day"]["manifestPath"])
    if args.yakkai_manifest is not None:
        yakkai_manifest = json.loads(args.yakkai_manifest.read_text(encoding="utf-8"))
        report["yakkaiFinalPublishState"] = correlate_yakkai_final_publish_state(yakkai_manifest)
    if yakkai_variants is not None:
        report["yakkaiDefaultDeltaOracle"] = build_yakkai_default_delta_oracle(report, yakkai_variants)
        report["yakkaiDefaultDeltaLocator"] = build_yakkai_default_delta_locator(report, yakkai_variants)
        for variant_name, locator_result in report["yakkaiDefaultDeltaLocator"].items():
            captures = yakkai_variants[variant_name]["captures"]
            for sample_name, sample in locator_result["samples"].items():
                crop_path = args.output / "locator-crops" / f"{variant_name}-{sample_name}-sample-delta.png"
                write_locator_crop(
                    Path(captures["default-before-effect"]),
                    Path(captures["default-after-effect"]),
                    sample["samplePixel"],
                    crop_path,
                )
                sample["cropPath"] = str(crop_path)
                if sample["nearestPixel"] is not None:
                    nearest_crop_path = (
                        args.output / "locator-crops" / f"{variant_name}-{sample_name}-nearest-delta.png"
                    )
                    write_locator_crop(
                        Path(captures["default-before-effect"]),
                        Path(captures["default-after-effect"]),
                        sample["nearestPixel"],
                        nearest_crop_path,
                    )
                    sample["nearestCropPath"] = str(nearest_crop_path)
    write_summary(report, args.windows, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
