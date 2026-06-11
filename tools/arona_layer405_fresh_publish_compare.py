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

WINDOWS_CONTENT_STAGE_FILES = {
    "effect-input": "effect-input-before-visible-effects.png",
    "prefix-3": "prefix-3-after-first-lut-pair.png",
    "prefix-7": "prefix-7-after-visible-effect-7.png",
    "final-publish-input": "final-publish-input.png",
}

WINDOWS_CONTENT_STAGE_TARGET_ORDERS = {
    "effect-input": 0,
    "prefix-3": 3,
    "prefix-7": 7,
    "final-publish-input": 9999,
}

WINDOWS_CONTENT_TRANSITIONS = {
    "effect-input-to-prefix-3": ("effect-input", "prefix-3", 3),
    "prefix-3-to-prefix-7": ("prefix-3", "prefix-7", 7),
    "prefix-7-to-final-publish-input": ("prefix-7", "final-publish-input", 9999),
}

WINDOWS_CONTENT_RANGES = {
    "prefix-3-to-prefix-7": ("prefix-3", "prefix-7", 3, 7),
}


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


def layer_final_publish_boundary(captures: dict[str, str]) -> tuple[str, str]:
    if "final-display-before" in captures and "final-display-after" in captures:
        return ("final-display-before", "final-display-after")
    return ("default-before-effect", "default-after-effect")


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
        layer_from_stage, layer_to_stage = layer_final_publish_boundary(captures)
        layer_from_path = Path(captures[layer_from_stage])
        layer_to_path = Path(captures[layer_to_stage])
        for stage, capture_path in ((layer_from_stage, layer_from_path), (layer_to_stage, layer_to_path)):
            if not capture_path.is_file():
                raise FreshPublishError(f"{variant_name} missing {stage} capture file: {capture_path}")
        layer_boundary_samples = {
            sample_name: build_boundary_stage_locator_sample(
                oracle_sample,
                layer_from_path,
                layer_to_path,
            )
            for sample_name, oracle_sample in variant_oracle["samples"].items()
        }
        locator[variant_name] = {
            "classification": combine_locator_classifications(samples),
            "samples": samples,
            "layerFinalPublishBoundary": {
                "fromStage": layer_from_stage,
                "toStage": layer_to_stage,
                "classification": combine_locator_classifications(layer_boundary_samples),
                "samples": layer_boundary_samples,
            },
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


def image_dimensions(path: Path) -> list[int]:
    with Image.open(path) as image:
        return [image.width, image.height]


def shader_family(shader: str | None, final_published: bool = False) -> str:
    if final_published:
        return "synthetic-puppet-final"
    shader_name = (shader or "").lower()
    if "genericimage" in shader_name:
        return "synthetic-puppet-final"
    if "pulse" in shader_name:
        return "pulse"
    if "waterwaves" in shader_name:
        return "waterwaves"
    if "lut" in shader_name:
        return "lut"
    if "shake" in shader_name:
        return "shake"
    return "unknown"


def material_output_order(stage: str) -> tuple[int, int] | None:
    match = re.fullmatch(r"material-output-(\d+)-(\d+)", stage)
    if not match:
        return None
    return (int(match.group(1)), int(match.group(2)))


def material_stage_metadata(manifest_path: Path, layer_id: int = 405) -> dict[str, dict[str, Any]]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    stages: dict[str, dict[str, Any]] = {}
    for capture in manifest.get("captures", []):
        if not isinstance(capture, dict):
            continue
        layer = capture.get("layer")
        if not isinstance(layer, dict) or layer.get("layerId") != layer_id:
            continue
        for material in layer.get("effectMaterials", []):
            if not isinstance(material, dict):
                continue
            stage = material.get("materialOutputCaptureStage")
            if not isinstance(stage, str):
                continue
            shader = material.get("shader")
            final_published = bool(material.get("finalPublishedMaterial"))
            stages[stage] = {
                "effectIndex": material.get("effectIndex"),
                "materialIndex": material.get("materialIndex"),
                "shader": shader,
                "stageFamily": shader_family(shader, final_published),
                "finalPublishedMaterial": final_published,
            }
    return stages


MATERIAL_DETAIL_FIELDS = [
    "authoredCombos",
    "authoredOutputRenderTarget",
    "authoredTextures",
    "debugMaterialOutputCommandSource",
    "debugMaterialOutputSourceRenderTarget",
    "debugSourceFinalEffectOutput",
    "defines",
    "effectIndex",
    "finalPublishedMaterial",
    "localMaterialOutputCaptureStage",
    "materialIndex",
    "materialOutputCaptureStage",
    "materialOutputCopyAfterPos",
    "materialValues",
    "resolvedCombos",
    "resolvedConstValues",
    "resolvedOutputRenderTarget",
    "resolvedTextures",
    "shader",
    "textureBindings",
]


def material_detail(material: dict[str, Any]) -> dict[str, Any]:
    shader = material.get("shader")
    final_published = bool(material.get("finalPublishedMaterial"))
    detail = {
        field: material[field]
        for field in MATERIAL_DETAIL_FIELDS
        if field in material
    }
    detail["stageFamily"] = shader_family(shader, final_published)
    return detail


def material_stage_details(manifest_path: Path, layer_id: int = 405) -> dict[str, dict[str, Any]]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    stages: dict[str, dict[str, Any]] = {}
    for capture in manifest.get("captures", []):
        if not isinstance(capture, dict):
            continue
        layer = capture.get("layer")
        if not isinstance(layer, dict) or layer.get("layerId") != layer_id:
            continue
        for material in layer.get("effectMaterials", []):
            if not isinstance(material, dict):
                continue
            stage = material.get("materialOutputCaptureStage")
            if isinstance(stage, str):
                stages[stage] = material_detail(material)
    return stages


def image_summary_stats(path: Path, max_size: int = 512) -> dict[str, Any]:
    with Image.open(path) as image:
        rgba_image = image.convert("RGBA")
        source_dimensions = [rgba_image.width, rgba_image.height]
        scale = min(1.0, max_size / max(source_dimensions))
        if scale < 1.0:
            stats_size = (
                max(1, round(source_dimensions[0] * scale)),
                max(1, round(source_dimensions[1] * scale)),
            )
            rgba_image = rgba_image.resize(stats_size, Image.Resampling.BILINEAR)
        else:
            stats_size = tuple(source_dimensions)
        rgba = np.asarray(rgba_image, dtype=np.float64) / 255.0
    return {
        "path": str(path),
        "dimensions": source_dimensions,
        "statsDimensions": [int(stats_size[0]), int(stats_size[1])],
        "meanRgba": [float(value) for value in np.mean(rgba, axis=(0, 1))],
        "stddevRgba": [round(float(value), 8) for value in np.std(rgba, axis=(0, 1))],
        "minRgba": [float(value) for value in np.min(rgba, axis=(0, 1))],
        "maxRgba": [float(value) for value in np.max(rgba, axis=(0, 1))],
    }


def selected_step_entries(microscope_result: dict[str, Any]) -> list[tuple[str, dict[str, Any]]]:
    entries: list[tuple[str, dict[str, Any]]] = []
    seen: set[tuple[str, str]] = set()
    for reason in ("selectedTarget", "strongestTowardStep", "strongestAwayStep"):
        step = microscope_result.get(reason)
        if not isinstance(step, dict):
            continue
        from_stage = step.get("fromStage")
        to_stage = step.get("toStage")
        if not isinstance(from_stage, str) or not isinstance(to_stage, str):
            continue
        key = (from_stage, to_stage)
        if key in seen:
            continue
        seen.add(key)
        entries.append((reason, step))
    return entries


def selected_step_metadata_entry(
    reason: str,
    step: dict[str, Any],
    captures: dict[str, str],
    material_details: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    from_stage = step["fromStage"]
    to_stage = step["toStage"]
    from_path = Path(step.get("fromPath") or captures[from_stage])
    to_path = Path(step.get("toPath") or captures[to_stage])
    if not from_path.is_file():
        raise FreshPublishError(f"selected step missing input image: {from_path}")
    if not to_path.is_file():
        raise FreshPublishError(f"selected step missing output image: {to_path}")
    keep_fields = [
        "fromStage",
        "toStage",
        "fromOrder",
        "toOrder",
        "fromStageFamily",
        "toStageFamily",
        "fromShader",
        "toShader",
        "direction",
        "prefix7RmseChange",
        "prefix3RmseChange",
        "windowsBlockDeltaRmse",
        "windowsBlockAlphaDeltaRmse",
        "windowsBlockDeltaCosine",
        "yakkaiRgbDeltaMagnitude",
        "yakkaiAlphaDeltaMagnitude",
        "comparisonDimensions",
        "fromRmseToPrefix7",
        "toRmseToPrefix7",
        "fromRmseToPrefix3",
        "toRmseToPrefix3",
    ]
    entry = {
        "reason": reason,
        "fromPath": str(from_path),
        "toPath": str(to_path),
        "fromMaterial": material_details.get(from_stage),
        "toMaterial": material_details.get(to_stage),
        "fromImageStats": image_summary_stats(from_path),
        "toImageStats": image_summary_stats(to_path),
        "transitionImageStats": image_delta_magnitude_metrics(from_path, to_path),
    }
    for field in keep_fields:
        if field in step:
            entry[field] = step[field]
    return entry


def build_yakkai_selected_step_metadata(
    yakkai_variants: dict[str, dict[str, Any]],
    microscope: dict[str, dict[str, Any]],
) -> dict[str, dict[str, Any]]:
    metadata: dict[str, dict[str, Any]] = {}
    for variant_name in REQUIRED_VARIANTS:
        if variant_name not in yakkai_variants:
            raise FreshPublishError(f"missing Yakkai variant manifest: {variant_name}")
        if variant_name not in microscope:
            raise FreshPublishError(f"missing middle-block microscope result: {variant_name}")
        yakkai_variant = yakkai_variants[variant_name]
        manifest_path = Path(yakkai_variant["manifestPath"])
        material_details = material_stage_details(manifest_path)
        steps = [
            selected_step_metadata_entry(reason, step, yakkai_variant["captures"], material_details)
            for reason, step in selected_step_entries(microscope[variant_name])
        ]
        metadata[variant_name] = {
            "classification": "selected-step-metadata-ready" if steps else "missing-selected-step-metadata",
            "manifestPath": str(manifest_path),
            "windowsPrefix3Path": microscope[variant_name].get("windowsPrefix3Path"),
            "windowsPrefix7Path": microscope[variant_name].get("windowsPrefix7Path"),
            "stepCount": len(steps),
            "steps": steps,
        }
    return metadata


def material_brief(material: dict[str, Any] | None) -> str:
    if not material:
        return "no material metadata"
    shader = material.get("shader", "")
    effect_index = material.get("effectIndex", "")
    combos = material.get("resolvedCombos") or {}
    combo_text = ", ".join(f"{key}={value}" for key, value in sorted(combos.items()))
    return f"shader={shader}; effectIndex={effect_index}; combos={combo_text or 'none'}"


def middle_block_windows_request_markdown(
    metadata: dict[str, dict[str, Any]],
    scene_id: str = "3228578419",
    layer_id: int = 405,
) -> str:
    lines = [
        "# Arona Layer 405 Middle-Block Windows Request",
        "",
        f"Scene: `{scene_id}`",
        f"Layer: `{layer_id} / ARONA_CROP_SHEET`",
        "",
        "This is a fresh Layer 405 internal pass export request.",
        "",
        "Please export every internal pass output between Windows `prefix-3` and `prefix-7` "
        "for `day`, `sunset`, and `night`. The current Linux/Yakkai report can identify "
        "which Yakkai material steps diverge, but it only has Windows endpoints for this block.",
        "",
        "For each variant, include every draw/pass after the `prefix-3` event through the "
        "`prefix-7` event, not only the endpoints.",
        "",
        "Required per internal pass:",
        "",
        "- replay event id and XML chunk/draw id",
        "- pass order/name and shader name",
        "- RTV/SRV resource ids, formats, dimensions, and all SRV bindings",
        "- constant buffer values, resolved combos/defines, material/user constants if visible",
        "- blend state, color write mask, viewport, scissor, render-target load/store behavior",
        "- full-resolution after-RTV PNG, plus before/source texture PNGs where available",
        "- a metadata JSON/CSV row tying each PNG to the event id and resource id",
        "",
        "Do not infer variant labels, copy endpoint textures into missing folders, or use final screenshots "
        "as pass-boundary replacements.",
        "",
        "## Yakkai Steps Needing Windows Internal Boundaries",
        "",
    ]
    for variant_name in REQUIRED_VARIANTS:
        variant = metadata.get(variant_name, {})
        for step in variant.get("steps", []):
            lines.append(f"- {variant_name}: `{step.get('fromStage', '')} -> {step.get('toStage', '')}`")
    lines.extend(
        [
            "",
        "| Variant | Yakkai Step | Reason | Direction | Prefix-7 Change | Windows Delta RMSE | Delta Cosine | To Material |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | --- |",
        ]
    )
    for variant_name in REQUIRED_VARIANTS:
        variant = metadata.get(variant_name, {})
        for step in variant.get("steps", []):
            prefix7_change = step.get("prefix7RmseChange")
            delta_rmse = step.get("windowsBlockDeltaRmse")
            delta_cosine = step.get("windowsBlockDeltaCosine")
            lines.append(
                "| {variant} | `{step_text}` | `{reason}` | `{direction}` | "
                "{change} | {rmse} | {cosine} | `{material}` |".format(
                    variant=variant_name,
                    step_text=f"{step.get('fromStage', '')} -> {step.get('toStage', '')}",
                    reason=step.get("reason", ""),
                    direction=step.get("direction", ""),
                    change="" if prefix7_change is None else f"{prefix7_change:.8f}",
                    rmse="" if delta_rmse is None else f"{delta_rmse:.8f}",
                    cosine="" if delta_cosine is None else f"{delta_cosine:.8f}",
                    material=material_brief(step.get("toMaterial")),
                )
            )
    lines.extend(
        [
            "",
            "Expected output shape:",
            "",
            "```text",
            "layer405_middle_block_internal_passes/",
            "  README.md",
            "  source_manifest.json",
            "  day/",
            "    passes.csv",
            "    eventNNN_after_rtv_resourceXXX.png",
            "    eventNNN_srv0_before_resourceYYY.png",
            "    eventNNN_metadata.json",
            "  sunset/",
            "    same files as day/",
            "  night/",
            "    same files as day/",
            "```",
            "",
            "The key comparison window is Windows `prefix-3-after-first-lut-pair.png` to "
            "`prefix-7-after-visible-effect-7.png`.",
        ]
    )
    return "\n".join(lines) + "\n"


def yakkai_content_stage_candidates(yakkai_variant: dict[str, Any]) -> list[dict[str, Any]]:
    captures = yakkai_variant.get("captures", {})
    if not isinstance(captures, dict):
        raise FreshPublishError("Yakkai variant captures must be a dictionary")
    manifest_path = Path(yakkai_variant["manifestPath"])
    material_metadata = material_stage_metadata(manifest_path)
    candidates: list[dict[str, Any]] = []
    effect_input = captures.get("effect-input")
    if isinstance(effect_input, str) and Path(effect_input).is_file():
        candidates.append(
            {
                "stage": "effect-input",
                "path": effect_input,
                "order": 0,
                "materialOrder": None,
                "stageFamily": "effect-input",
                "dimensions": image_dimensions(Path(effect_input)),
            }
        )
    for stage, capture_path in captures.items():
        material_order = material_output_order(stage)
        if material_order is None:
            continue
        path = Path(capture_path)
        if not path.is_file():
            continue
        metadata = material_metadata.get(stage, {})
        candidates.append(
            {
                "stage": stage,
                "path": str(path),
                "order": material_order[0],
                "materialOrder": list(material_order),
                "stageFamily": metadata.get("stageFamily", "unknown"),
                "shader": metadata.get("shader"),
                "effectIndex": metadata.get("effectIndex"),
                "materialIndex": metadata.get("materialIndex"),
                "finalPublishedMaterial": bool(metadata.get("finalPublishedMaterial", False)),
                "dimensions": image_dimensions(path),
            }
        )
    return sorted(candidates, key=lambda candidate: (candidate["order"], candidate["stage"]))


def image_pair_metrics(left_path: Path, right_path: Path, max_size: int = 512) -> dict[str, Any]:
    left_dimensions = image_dimensions(left_path)
    right_dimensions = image_dimensions(right_path)
    if left_dimensions != right_dimensions:
        return {
            "classification": "dimension-mismatch",
            "leftDimensions": left_dimensions,
            "rightDimensions": right_dimensions,
            "rmse": None,
            "alphaWeightedRmse": None,
        }
    scale = min(1.0, max_size / max(left_dimensions))
    compare_size = (
        max(1, round(left_dimensions[0] * scale)),
        max(1, round(left_dimensions[1] * scale)),
    )
    left = rgba_array(left_path, compare_size)
    right = rgba_array(right_path, compare_size)
    return {
        "classification": "compared",
        "leftDimensions": left_dimensions,
        "rightDimensions": right_dimensions,
        "comparisonDimensions": [compare_size[0], compare_size[1]],
        "rmse": round(rmse(left, right), 8),
        "alphaWeightedRmse": round(alpha_weighted_rmse(left, right), 8),
    }


def content_stage_sort_key(anchor: str, match: dict[str, Any]) -> tuple[float, float, int, str]:
    target_order = WINDOWS_CONTENT_STAGE_TARGET_ORDERS[anchor]
    order = int(match.get("order", 0))
    if target_order >= 9999:
        order_penalty = -order
    else:
        order_penalty = abs(order - target_order)
    return (
        float(match["rmse"]),
        float(match["alphaWeightedRmse"]),
        order_penalty,
        match["stage"],
    )


def compare_windows_anchor_to_yakkai_stages(
    anchor: str,
    windows_path: Path,
    candidates: list[dict[str, Any]],
) -> dict[str, Any]:
    matches = []
    skipped = []
    for candidate in candidates:
        metrics = image_pair_metrics(windows_path, Path(candidate["path"]))
        if metrics["classification"] != "compared":
            skipped.append(
                {
                    "stage": candidate["stage"],
                    "path": candidate["path"],
                    "stageFamily": candidate["stageFamily"],
                    "reason": "dimension-mismatch",
                    "windowsDimensions": metrics["leftDimensions"],
                    "yakkaiDimensions": metrics["rightDimensions"],
                }
            )
            continue
        matches.append(
            {
                "stage": candidate["stage"],
                "path": candidate["path"],
                "stageFamily": candidate["stageFamily"],
                "shader": candidate.get("shader"),
                "effectIndex": candidate.get("effectIndex"),
                "materialIndex": candidate.get("materialIndex"),
                "finalPublishedMaterial": candidate.get("finalPublishedMaterial", False),
                "order": candidate["order"],
                "dimensions": metrics["leftDimensions"],
                "comparisonDimensions": metrics["comparisonDimensions"],
                "rmse": metrics["rmse"],
                "alphaWeightedRmse": metrics["alphaWeightedRmse"],
            }
        )
    matches.sort(key=lambda match: content_stage_sort_key(anchor, match))
    return {
        "windowsPath": str(windows_path),
        "bestMatch": matches[0] if matches else None,
        "topMatches": matches[:5],
        "skippedCandidates": skipped,
    }


def classify_content_stage_attribution(anchors: dict[str, dict[str, Any]]) -> str:
    best_matches = [anchor.get("bestMatch") for anchor in anchors.values()]
    if not best_matches or any(match is None for match in best_matches):
        return "missing-comparable-content-stage"
    worst_rmse = max(float(match["rmse"]) for match in best_matches if match is not None)
    if worst_rmse >= 0.06:
        return "content-stage-mismatch"
    if worst_rmse >= 0.02:
        return "content-stage-drift"
    return "content-stage-close"


def build_yakkai_content_stage_attribution(
    yakkai_variants: dict[str, dict[str, Any]],
    windows_root: Path,
) -> dict[str, dict[str, Any]]:
    attribution: dict[str, dict[str, Any]] = {}
    for variant_name in REQUIRED_VARIANTS:
        if variant_name not in yakkai_variants:
            raise FreshPublishError(f"missing Yakkai variant manifest: {variant_name}")
        variant_root = windows_root / variant_name
        if not variant_root.is_dir():
            raise FreshPublishError(f"missing extracted Windows variant directory: {variant_root}")
        candidates = yakkai_content_stage_candidates(yakkai_variants[variant_name])
        anchors = {}
        for anchor, filename in WINDOWS_CONTENT_STAGE_FILES.items():
            windows_path = variant_root / filename
            if not windows_path.is_file():
                raise FreshPublishError(f"{variant_name} missing extracted Windows anchor: {windows_path}")
            anchors[anchor] = compare_windows_anchor_to_yakkai_stages(anchor, windows_path, candidates)
        ranking = sorted(
            (
                {
                    "anchor": anchor,
                    "stage": result["bestMatch"]["stage"] if result["bestMatch"] else None,
                    "stageFamily": result["bestMatch"]["stageFamily"] if result["bestMatch"] else None,
                    "rmse": result["bestMatch"]["rmse"] if result["bestMatch"] else None,
                    "alphaWeightedRmse": result["bestMatch"]["alphaWeightedRmse"] if result["bestMatch"] else None,
                }
                for anchor, result in anchors.items()
            ),
            key=lambda item: -1.0 if item["rmse"] is None else -float(item["rmse"]),
        )
        attribution[variant_name] = {
            "classification": classify_content_stage_attribution(anchors),
            "manifestPath": yakkai_variants[variant_name]["manifestPath"],
            "candidateCount": len(candidates),
            "anchors": anchors,
            "ranking": ranking,
            "selectedTarget": ranking[0] if ranking else None,
        }
    return attribution


def image_transition_delta(path_before: Path, path_after: Path, size: tuple[int, int]) -> np.ndarray:
    before = rgba_array(path_before, size)
    after = rgba_array(path_after, size)
    return after - before


def delta_cosine(left: np.ndarray, right: np.ndarray) -> float:
    left_flat = left.reshape(-1).astype(np.float32)
    right_flat = right.reshape(-1).astype(np.float32)
    left_norm = float(np.linalg.norm(left_flat))
    right_norm = float(np.linalg.norm(right_flat))
    if left_norm <= 1e-8 or right_norm <= 1e-8:
        return 1.0 if left_norm <= 1e-8 and right_norm <= 1e-8 else 0.0
    value = float(np.dot(left_flat, right_flat) / (left_norm * right_norm))
    return min(1.0, max(-1.0, value))


def image_transition_pair_metrics(
    windows_before: Path,
    windows_after: Path,
    yakkai_before: Path,
    yakkai_after: Path,
    max_size: int = 512,
) -> dict[str, Any]:
    dimensions = {
        "windowsBeforeDimensions": image_dimensions(windows_before),
        "windowsAfterDimensions": image_dimensions(windows_after),
        "yakkaiBeforeDimensions": image_dimensions(yakkai_before),
        "yakkaiAfterDimensions": image_dimensions(yakkai_after),
    }
    unique_dimensions = {tuple(value) for value in dimensions.values()}
    if len(unique_dimensions) != 1:
        return {
            "classification": "dimension-mismatch",
            **dimensions,
            "deltaRmse": None,
            "alphaDeltaRmse": None,
            "deltaCosine": None,
        }
    source_dimensions = dimensions["windowsBeforeDimensions"]
    scale = min(1.0, max_size / max(source_dimensions))
    compare_size = (
        max(1, round(source_dimensions[0] * scale)),
        max(1, round(source_dimensions[1] * scale)),
    )
    windows_delta = image_transition_delta(windows_before, windows_after, compare_size)
    yakkai_delta = image_transition_delta(yakkai_before, yakkai_after, compare_size)
    rgb_delta_rmse = rmse(windows_delta[..., :3], yakkai_delta[..., :3])
    alpha_delta_rmse = rmse(windows_delta[..., 3:4], yakkai_delta[..., 3:4])
    return {
        "classification": "compared",
        **dimensions,
        "comparisonDimensions": [compare_size[0], compare_size[1]],
        "deltaRmse": round(rgb_delta_rmse, 8),
        "alphaDeltaRmse": round(alpha_delta_rmse, 8),
        "deltaCosine": round(delta_cosine(windows_delta[..., :3], yakkai_delta[..., :3]), 8),
    }


def image_delta_magnitude_metrics(
    before_path: Path,
    after_path: Path,
    max_size: int = 512,
) -> dict[str, Any]:
    before_dimensions = image_dimensions(before_path)
    after_dimensions = image_dimensions(after_path)
    if before_dimensions != after_dimensions:
        return {
            "classification": "dimension-mismatch",
            "beforeDimensions": before_dimensions,
            "afterDimensions": after_dimensions,
            "rgbDeltaMagnitude": None,
            "alphaDeltaMagnitude": None,
        }
    scale = min(1.0, max_size / max(before_dimensions))
    compare_size = (
        max(1, round(before_dimensions[0] * scale)),
        max(1, round(before_dimensions[1] * scale)),
    )
    delta = image_transition_delta(before_path, after_path, compare_size)
    return {
        "classification": "compared",
        "beforeDimensions": before_dimensions,
        "afterDimensions": after_dimensions,
        "comparisonDimensions": [compare_size[0], compare_size[1]],
        "rgbDeltaMagnitude": round(float(np.sqrt(np.mean(np.square(delta[..., :3])))), 8),
        "alphaDeltaMagnitude": round(float(np.sqrt(np.mean(np.square(delta[..., 3:4])))), 8),
    }


def yakkai_content_transitions(candidates: list[dict[str, Any]]) -> list[dict[str, Any]]:
    transitions = []
    for before, after in zip(candidates, candidates[1:]):
        if before["dimensions"] != after["dimensions"]:
            continue
        transitions.append(
            {
                "fromStage": before["stage"],
                "toStage": after["stage"],
                "fromPath": before["path"],
                "toPath": after["path"],
                "fromOrder": before["order"],
                "toOrder": after["order"],
                "fromStageFamily": before["stageFamily"],
                "toStageFamily": after["stageFamily"],
                "fromShader": before.get("shader"),
                "toShader": after.get("shader"),
                "dimensions": before["dimensions"],
            }
        )
    return transitions


def content_transition_sort_key(transition: str, match: dict[str, Any]) -> tuple[float, float, int, str, str]:
    _from_anchor, _to_anchor, target_order = WINDOWS_CONTENT_TRANSITIONS[transition]
    to_order = int(match.get("toOrder", 0))
    if target_order >= 9999:
        order_penalty = -to_order
    else:
        order_penalty = abs(to_order - target_order)
    return (
        float(match["deltaRmse"]),
        float(match["alphaDeltaRmse"]),
        order_penalty,
        match["fromStage"],
        match["toStage"],
    )


def compare_windows_transition_to_yakkai_transitions(
    transition: str,
    windows_before: Path,
    windows_after: Path,
    yakkai_transitions: list[dict[str, Any]],
) -> dict[str, Any]:
    matches = []
    skipped = []
    for candidate in yakkai_transitions:
        metrics = image_transition_pair_metrics(
            windows_before,
            windows_after,
            Path(candidate["fromPath"]),
            Path(candidate["toPath"]),
        )
        if metrics["classification"] != "compared":
            skipped.append(
                {
                    "fromStage": candidate["fromStage"],
                    "toStage": candidate["toStage"],
                    "fromStageFamily": candidate["fromStageFamily"],
                    "toStageFamily": candidate["toStageFamily"],
                    "reason": "dimension-mismatch",
                    "windowsBeforeDimensions": metrics["windowsBeforeDimensions"],
                    "windowsAfterDimensions": metrics["windowsAfterDimensions"],
                    "yakkaiBeforeDimensions": metrics["yakkaiBeforeDimensions"],
                    "yakkaiAfterDimensions": metrics["yakkaiAfterDimensions"],
                }
            )
            continue
        matches.append(
            {
                **candidate,
                "comparisonDimensions": metrics["comparisonDimensions"],
                "deltaRmse": metrics["deltaRmse"],
                "alphaDeltaRmse": metrics["alphaDeltaRmse"],
                "deltaCosine": metrics["deltaCosine"],
            }
        )
    matches.sort(key=lambda match: content_transition_sort_key(transition, match))
    return {
        "windowsBeforePath": str(windows_before),
        "windowsAfterPath": str(windows_after),
        "bestMatch": matches[0] if matches else None,
        "topMatches": matches[:5],
        "skippedCandidates": skipped,
    }


def classify_content_transition_attribution(transitions: dict[str, dict[str, Any]]) -> str:
    best_matches = [transition.get("bestMatch") for transition in transitions.values()]
    if not best_matches or any(match is None for match in best_matches):
        return "missing-comparable-content-transition"
    worst_rmse = max(float(match["deltaRmse"]) for match in best_matches if match is not None)
    if worst_rmse >= 0.06:
        return "content-transition-mismatch"
    if worst_rmse >= 0.02:
        return "content-transition-drift"
    return "content-transition-close"


def build_yakkai_content_transition_attribution(
    yakkai_variants: dict[str, dict[str, Any]],
    windows_root: Path,
) -> dict[str, dict[str, Any]]:
    attribution: dict[str, dict[str, Any]] = {}
    for variant_name in REQUIRED_VARIANTS:
        if variant_name not in yakkai_variants:
            raise FreshPublishError(f"missing Yakkai variant manifest: {variant_name}")
        variant_root = windows_root / variant_name
        if not variant_root.is_dir():
            raise FreshPublishError(f"missing extracted Windows variant directory: {variant_root}")
        candidates = yakkai_content_stage_candidates(yakkai_variants[variant_name])
        yakkai_transitions = yakkai_content_transitions(candidates)
        transitions = {}
        for transition, (from_anchor, to_anchor, _target_order) in WINDOWS_CONTENT_TRANSITIONS.items():
            windows_before = variant_root / WINDOWS_CONTENT_STAGE_FILES[from_anchor]
            windows_after = variant_root / WINDOWS_CONTENT_STAGE_FILES[to_anchor]
            if not windows_before.is_file():
                raise FreshPublishError(f"{variant_name} missing extracted Windows transition input: {windows_before}")
            if not windows_after.is_file():
                raise FreshPublishError(f"{variant_name} missing extracted Windows transition output: {windows_after}")
            transitions[transition] = compare_windows_transition_to_yakkai_transitions(
                transition,
                windows_before,
                windows_after,
                yakkai_transitions,
            )
        ranking = sorted(
            (
                {
                    "transition": transition,
                    "fromStage": result["bestMatch"]["fromStage"] if result["bestMatch"] else None,
                    "toStage": result["bestMatch"]["toStage"] if result["bestMatch"] else None,
                    "fromStageFamily": result["bestMatch"]["fromStageFamily"] if result["bestMatch"] else None,
                    "toStageFamily": result["bestMatch"]["toStageFamily"] if result["bestMatch"] else None,
                    "deltaRmse": result["bestMatch"]["deltaRmse"] if result["bestMatch"] else None,
                    "alphaDeltaRmse": result["bestMatch"]["alphaDeltaRmse"] if result["bestMatch"] else None,
                    "deltaCosine": result["bestMatch"]["deltaCosine"] if result["bestMatch"] else None,
                }
                for transition, result in transitions.items()
            ),
            key=lambda item: -1.0 if item["deltaRmse"] is None else -float(item["deltaRmse"]),
        )
        attribution[variant_name] = {
            "classification": classify_content_transition_attribution(transitions),
            "manifestPath": yakkai_variants[variant_name]["manifestPath"],
            "candidateTransitionCount": len(yakkai_transitions),
            "transitions": transitions,
            "ranking": ranking,
            "selectedTarget": ranking[0] if ranking else None,
        }
    return attribution


def yakkai_content_ranges(candidates: list[dict[str, Any]]) -> list[dict[str, Any]]:
    ranges = []
    for start_index, before in enumerate(candidates):
        for end_index in range(start_index + 1, len(candidates)):
            candidate_slice = candidates[start_index : end_index + 1]
            dimensions = {tuple(candidate["dimensions"]) for candidate in candidate_slice}
            if len(dimensions) != 1:
                continue
            after = candidate_slice[-1]
            ranges.append(
                {
                    "fromStage": before["stage"],
                    "toStage": after["stage"],
                    "fromPath": before["path"],
                    "toPath": after["path"],
                    "fromOrder": before["order"],
                    "toOrder": after["order"],
                    "fromStageFamily": before["stageFamily"],
                    "toStageFamily": after["stageFamily"],
                    "fromShader": before.get("shader"),
                    "toShader": after.get("shader"),
                    "dimensions": before["dimensions"],
                    "rangeLength": end_index - start_index,
                    "stages": [candidate["stage"] for candidate in candidate_slice],
                    "stageFamilies": [candidate["stageFamily"] for candidate in candidate_slice],
                    "shaders": [candidate.get("shader") for candidate in candidate_slice],
                }
            )
    return ranges


def content_range_sort_key(range_name: str, match: dict[str, Any]) -> tuple[float, float, int, int, str, str]:
    _from_anchor, _to_anchor, from_order, to_order = WINDOWS_CONTENT_RANGES[range_name]
    order_penalty = abs(int(match.get("fromOrder", 0)) - from_order) + abs(int(match.get("toOrder", 0)) - to_order)
    length_penalty = abs(int(match.get("rangeLength", 0)) - max(1, to_order - from_order))
    return (
        float(match["deltaRmse"]),
        float(match["alphaDeltaRmse"]),
        order_penalty,
        length_penalty,
        match["fromStage"],
        match["toStage"],
    )


def compare_windows_range_to_yakkai_ranges(
    range_name: str,
    windows_before: Path,
    windows_after: Path,
    yakkai_ranges: list[dict[str, Any]],
) -> dict[str, Any]:
    matches = []
    skipped = []
    for candidate in yakkai_ranges:
        metrics = image_transition_pair_metrics(
            windows_before,
            windows_after,
            Path(candidate["fromPath"]),
            Path(candidate["toPath"]),
        )
        if metrics["classification"] != "compared":
            skipped.append(
                {
                    "fromStage": candidate["fromStage"],
                    "toStage": candidate["toStage"],
                    "fromStageFamily": candidate["fromStageFamily"],
                    "toStageFamily": candidate["toStageFamily"],
                    "reason": "dimension-mismatch",
                    "windowsBeforeDimensions": metrics["windowsBeforeDimensions"],
                    "windowsAfterDimensions": metrics["windowsAfterDimensions"],
                    "yakkaiBeforeDimensions": metrics["yakkaiBeforeDimensions"],
                    "yakkaiAfterDimensions": metrics["yakkaiAfterDimensions"],
                }
            )
            continue
        matches.append(
            {
                **candidate,
                "comparisonDimensions": metrics["comparisonDimensions"],
                "deltaRmse": metrics["deltaRmse"],
                "alphaDeltaRmse": metrics["alphaDeltaRmse"],
                "deltaCosine": metrics["deltaCosine"],
            }
        )
    matches.sort(key=lambda match: content_range_sort_key(range_name, match))
    return {
        "windowsBeforePath": str(windows_before),
        "windowsAfterPath": str(windows_after),
        "bestMatch": matches[0] if matches else None,
        "topMatches": matches[:8],
        "skippedCandidates": skipped,
    }


def classify_content_range_attribution(ranges: dict[str, dict[str, Any]]) -> str:
    best_matches = [range_result.get("bestMatch") for range_result in ranges.values()]
    if not best_matches or any(match is None for match in best_matches):
        return "missing-comparable-content-range"
    worst_rmse = max(float(match["deltaRmse"]) for match in best_matches if match is not None)
    if worst_rmse >= 0.06:
        return "content-range-mismatch"
    if worst_rmse >= 0.02:
        return "content-range-drift"
    return "content-range-close"


def build_yakkai_content_range_attribution(
    yakkai_variants: dict[str, dict[str, Any]],
    windows_root: Path,
) -> dict[str, dict[str, Any]]:
    attribution: dict[str, dict[str, Any]] = {}
    for variant_name in REQUIRED_VARIANTS:
        if variant_name not in yakkai_variants:
            raise FreshPublishError(f"missing Yakkai variant manifest: {variant_name}")
        variant_root = windows_root / variant_name
        if not variant_root.is_dir():
            raise FreshPublishError(f"missing extracted Windows variant directory: {variant_root}")
        candidates = yakkai_content_stage_candidates(yakkai_variants[variant_name])
        yakkai_ranges = yakkai_content_ranges(candidates)
        ranges = {}
        for range_name, (from_anchor, to_anchor, _from_order, _to_order) in WINDOWS_CONTENT_RANGES.items():
            windows_before = variant_root / WINDOWS_CONTENT_STAGE_FILES[from_anchor]
            windows_after = variant_root / WINDOWS_CONTENT_STAGE_FILES[to_anchor]
            if not windows_before.is_file():
                raise FreshPublishError(f"{variant_name} missing extracted Windows range input: {windows_before}")
            if not windows_after.is_file():
                raise FreshPublishError(f"{variant_name} missing extracted Windows range output: {windows_after}")
            ranges[range_name] = compare_windows_range_to_yakkai_ranges(
                range_name,
                windows_before,
                windows_after,
                yakkai_ranges,
            )
        ranking = sorted(
            (
                {
                    "range": range_name,
                    "fromStage": result["bestMatch"]["fromStage"] if result["bestMatch"] else None,
                    "toStage": result["bestMatch"]["toStage"] if result["bestMatch"] else None,
                    "fromStageFamily": result["bestMatch"]["fromStageFamily"] if result["bestMatch"] else None,
                    "toStageFamily": result["bestMatch"]["toStageFamily"] if result["bestMatch"] else None,
                    "stageFamilies": result["bestMatch"]["stageFamilies"] if result["bestMatch"] else None,
                    "rangeLength": result["bestMatch"]["rangeLength"] if result["bestMatch"] else None,
                    "deltaRmse": result["bestMatch"]["deltaRmse"] if result["bestMatch"] else None,
                    "alphaDeltaRmse": result["bestMatch"]["alphaDeltaRmse"] if result["bestMatch"] else None,
                    "deltaCosine": result["bestMatch"]["deltaCosine"] if result["bestMatch"] else None,
                }
                for range_name, result in ranges.items()
            ),
            key=lambda item: -1.0 if item["deltaRmse"] is None else -float(item["deltaRmse"]),
        )
        attribution[variant_name] = {
            "classification": classify_content_range_attribution(ranges),
            "manifestPath": yakkai_variants[variant_name]["manifestPath"],
            "candidateRangeCount": len(yakkai_ranges),
            "ranges": ranges,
            "ranking": ranking,
            "selectedTarget": ranking[0] if ranking else None,
        }
    return attribution


def middle_block_step_direction(prefix7_rmse_change: float) -> str:
    if prefix7_rmse_change <= -DELTA_MAGNITUDE_THRESHOLD:
        return "toward-prefix-7"
    if prefix7_rmse_change >= DELTA_MAGNITUDE_THRESHOLD:
        return "away-from-prefix-7"
    return "flat-prefix-7"


def yakkai_middle_block_stage_distances(
    windows_prefix3: Path,
    windows_prefix7: Path,
    candidates: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    distances = []
    for candidate in candidates:
        prefix3_metrics = image_pair_metrics(windows_prefix3, Path(candidate["path"]))
        prefix7_metrics = image_pair_metrics(windows_prefix7, Path(candidate["path"]))
        if prefix3_metrics["classification"] != "compared" or prefix7_metrics["classification"] != "compared":
            continue
        distances.append(
            {
                "stage": candidate["stage"],
                "path": candidate["path"],
                "order": candidate["order"],
                "materialOrder": candidate.get("materialOrder"),
                "stageFamily": candidate["stageFamily"],
                "shader": candidate.get("shader"),
                "effectIndex": candidate.get("effectIndex"),
                "materialIndex": candidate.get("materialIndex"),
                "finalPublishedMaterial": candidate.get("finalPublishedMaterial", False),
                "dimensions": prefix7_metrics["leftDimensions"],
                "comparisonDimensions": prefix7_metrics["comparisonDimensions"],
                "rmseToPrefix3": prefix3_metrics["rmse"],
                "alphaWeightedRmseToPrefix3": prefix3_metrics["alphaWeightedRmse"],
                "rmseToPrefix7": prefix7_metrics["rmse"],
                "alphaWeightedRmseToPrefix7": prefix7_metrics["alphaWeightedRmse"],
            }
        )
    return sorted(distances, key=lambda distance: (distance["order"], distance["stage"]))


def yakkai_middle_block_steps(
    windows_prefix3: Path,
    windows_prefix7: Path,
    stage_distances: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    steps = []
    for before, after in zip(stage_distances, stage_distances[1:]):
        if before["dimensions"] != after["dimensions"]:
            continue
        pair_metrics = image_transition_pair_metrics(
            windows_prefix3,
            windows_prefix7,
            Path(before["path"]),
            Path(after["path"]),
        )
        if pair_metrics["classification"] != "compared":
            continue
        magnitude_metrics = image_delta_magnitude_metrics(Path(before["path"]), Path(after["path"]))
        prefix7_change = round(float(after["rmseToPrefix7"]) - float(before["rmseToPrefix7"]), 8)
        prefix3_change = round(float(after["rmseToPrefix3"]) - float(before["rmseToPrefix3"]), 8)
        steps.append(
            {
                "fromStage": before["stage"],
                "toStage": after["stage"],
                "fromPath": before["path"],
                "toPath": after["path"],
                "fromOrder": before["order"],
                "toOrder": after["order"],
                "fromStageFamily": before["stageFamily"],
                "toStageFamily": after["stageFamily"],
                "fromShader": before.get("shader"),
                "toShader": after.get("shader"),
                "fromRmseToPrefix7": before["rmseToPrefix7"],
                "toRmseToPrefix7": after["rmseToPrefix7"],
                "fromRmseToPrefix3": before["rmseToPrefix3"],
                "toRmseToPrefix3": after["rmseToPrefix3"],
                "prefix7RmseChange": prefix7_change,
                "prefix3RmseChange": prefix3_change,
                "direction": middle_block_step_direction(prefix7_change),
                "windowsBlockDeltaRmse": pair_metrics["deltaRmse"],
                "windowsBlockAlphaDeltaRmse": pair_metrics["alphaDeltaRmse"],
                "windowsBlockDeltaCosine": pair_metrics["deltaCosine"],
                "yakkaiRgbDeltaMagnitude": magnitude_metrics["rgbDeltaMagnitude"],
                "yakkaiAlphaDeltaMagnitude": magnitude_metrics["alphaDeltaMagnitude"],
                "comparisonDimensions": pair_metrics["comparisonDimensions"],
            }
        )
    return steps


def classify_middle_block_microscope(
    stage_distances: list[dict[str, Any]],
    strongest_away_step: dict[str, Any] | None,
) -> str:
    if not stage_distances:
        return "missing-comparable-middle-block-stage"
    if strongest_away_step is not None and float(strongest_away_step["prefix7RmseChange"]) >= 0.02:
        return "middle-block-regression-step"
    best_prefix7_rmse = min(float(stage["rmseToPrefix7"]) for stage in stage_distances)
    if best_prefix7_rmse >= 0.06:
        return "middle-block-incomplete-progress"
    if best_prefix7_rmse >= 0.02:
        return "middle-block-drift"
    return "middle-block-close"


def build_yakkai_middle_block_microscope(
    yakkai_variants: dict[str, dict[str, Any]],
    windows_root: Path,
) -> dict[str, dict[str, Any]]:
    microscope: dict[str, dict[str, Any]] = {}
    for variant_name in REQUIRED_VARIANTS:
        if variant_name not in yakkai_variants:
            raise FreshPublishError(f"missing Yakkai variant manifest: {variant_name}")
        variant_root = windows_root / variant_name
        if not variant_root.is_dir():
            raise FreshPublishError(f"missing extracted Windows variant directory: {variant_root}")
        windows_prefix3 = variant_root / WINDOWS_CONTENT_STAGE_FILES["prefix-3"]
        windows_prefix7 = variant_root / WINDOWS_CONTENT_STAGE_FILES["prefix-7"]
        if not windows_prefix3.is_file():
            raise FreshPublishError(f"{variant_name} missing extracted Windows prefix-3: {windows_prefix3}")
        if not windows_prefix7.is_file():
            raise FreshPublishError(f"{variant_name} missing extracted Windows prefix-7: {windows_prefix7}")

        candidates = yakkai_content_stage_candidates(yakkai_variants[variant_name])
        stage_distances = yakkai_middle_block_stage_distances(windows_prefix3, windows_prefix7, candidates)
        steps = yakkai_middle_block_steps(windows_prefix3, windows_prefix7, stage_distances)
        toward_steps = [step for step in steps if step["direction"] == "toward-prefix-7"]
        away_steps = [step for step in steps if step["direction"] == "away-from-prefix-7"]
        strongest_toward_step = (
            min(toward_steps, key=lambda step: (float(step["prefix7RmseChange"]), step["toStage"]))
            if toward_steps
            else None
        )
        strongest_away_step = (
            max(away_steps, key=lambda step: (float(step["prefix7RmseChange"]), step["toStage"]))
            if away_steps
            else None
        )
        closest_to_prefix7 = (
            min(stage_distances, key=lambda stage: (float(stage["rmseToPrefix7"]), stage["stage"]))
            if stage_distances
            else None
        )
        selected_target = strongest_away_step or strongest_toward_step or (steps[0] if steps else None)
        microscope[variant_name] = {
            "classification": classify_middle_block_microscope(stage_distances, strongest_away_step),
            "manifestPath": yakkai_variants[variant_name]["manifestPath"],
            "windowsPrefix3Path": str(windows_prefix3),
            "windowsPrefix7Path": str(windows_prefix7),
            "candidateCount": len(candidates),
            "comparableStageCount": len(stage_distances),
            "stageDistances": stage_distances,
            "steps": steps,
            "towardRanking": sorted(steps, key=lambda step: (float(step["prefix7RmseChange"]), step["toStage"])),
            "awayRanking": sorted(steps, key=lambda step: (-float(step["prefix7RmseChange"]), step["toStage"])),
            "closestToPrefix7": closest_to_prefix7,
            "strongestTowardStep": strongest_toward_step,
            "strongestAwayStep": strongest_away_step,
            "selectedTarget": selected_target,
        }
    return microscope


def write_transition_match_crop(
    windows_before_path: Path,
    windows_after_path: Path,
    yakkai_before_path: Path,
    yakkai_after_path: Path,
    coordinate: list[int],
    coordinate_dimensions: list[int],
    output_path: Path,
    crop_radius: int = 48,
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if len(coordinate) != 2 or len(coordinate_dimensions) != 2:
        raise FreshPublishError("transition crop coordinate and dimensions must contain two values")
    with (
        Image.open(windows_before_path) as windows_before_image,
        Image.open(windows_after_path) as windows_after_image,
        Image.open(yakkai_before_path) as yakkai_before_image,
        Image.open(yakkai_after_path) as yakkai_after_image,
    ):
        images = [
            windows_before_image.convert("RGBA"),
            windows_after_image.convert("RGBA"),
            yakkai_before_image.convert("RGBA"),
            yakkai_after_image.convert("RGBA"),
        ]
        sizes = {image.size for image in images}
        if len(sizes) != 1:
            raise FreshPublishError("transition crop images must all have matching size")
        width, height = images[0].size
        x = min(width - 1, max(0, round(coordinate[0] * width / coordinate_dimensions[0])))
        y = min(height - 1, max(0, round(coordinate[1] * height / coordinate_dimensions[1])))
        left = max(0, x - crop_radius)
        top = max(0, y - crop_radius)
        right = min(width, x + crop_radius + 1)
        bottom = min(height, y + crop_radius + 1)
        windows_before_crop, windows_after_crop, yakkai_before_crop, yakkai_after_crop = [
            image.crop((left, top, right, bottom)) for image in images
        ]
        windows_delta = np.asarray(windows_after_crop, dtype=np.int16) - np.asarray(windows_before_crop, dtype=np.int16)
        yakkai_delta = np.asarray(yakkai_after_crop, dtype=np.int16) - np.asarray(yakkai_before_crop, dtype=np.int16)

        def delta_preview(delta: np.ndarray) -> Image.Image:
            return Image.fromarray(
                np.clip(delta[:, :, :3] + 128, 0, 255).astype(np.uint8),
                mode="RGB",
            ).convert("RGBA")

        diff_preview = Image.fromarray(
            np.clip(np.abs(yakkai_delta[:, :, :3] - windows_delta[:, :, :3]) * 4, 0, 255).astype(np.uint8),
            mode="RGB",
        ).convert("RGBA")
        windows_preview = delta_preview(windows_delta)
        yakkai_preview = delta_preview(yakkai_delta)
        sheet = Image.new("RGBA", (windows_preview.width * 3, windows_preview.height), (0, 0, 0, 255))
        sheet.paste(windows_preview, (0, 0))
        sheet.paste(yakkai_preview, (windows_preview.width, 0))
        sheet.paste(diff_preview, (windows_preview.width * 2, 0))
        sheet.save(output_path)


def write_stage_match_crop(
    windows_path: Path,
    yakkai_path: Path,
    coordinate: list[int],
    coordinate_dimensions: list[int],
    output_path: Path,
    crop_radius: int = 48,
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if len(coordinate) != 2 or len(coordinate_dimensions) != 2:
        raise FreshPublishError("stage crop coordinate and dimensions must contain two values")
    with Image.open(windows_path) as windows_image, Image.open(yakkai_path) as yakkai_image:
        windows_rgba = windows_image.convert("RGBA")
        yakkai_rgba = yakkai_image.convert("RGBA")
        if windows_rgba.size != yakkai_rgba.size:
            raise FreshPublishError(f"stage crop image size mismatch: {windows_path} vs {yakkai_path}")
        width, height = windows_rgba.size
        x = min(width - 1, max(0, round(coordinate[0] * width / coordinate_dimensions[0])))
        y = min(height - 1, max(0, round(coordinate[1] * height / coordinate_dimensions[1])))
        left = max(0, x - crop_radius)
        top = max(0, y - crop_radius)
        right = min(width, x + crop_radius + 1)
        bottom = min(height, y + crop_radius + 1)
        windows_crop = windows_rgba.crop((left, top, right, bottom))
        yakkai_crop = yakkai_rgba.crop((left, top, right, bottom))
        delta = Image.fromarray(
            np.clip(
                np.abs(
                    np.asarray(yakkai_crop, dtype=np.int16)[:, :, :3]
                    - np.asarray(windows_crop, dtype=np.int16)[:, :, :3]
                )
                * 4,
                0,
                255,
            ).astype(np.uint8),
            mode="RGB",
        ).convert("RGBA")
        sheet = Image.new("RGBA", (windows_crop.width * 3, windows_crop.height), (0, 0, 0, 255))
        sheet.paste(windows_crop, (0, 0))
        sheet.paste(yakkai_crop, (windows_crop.width, 0))
        sheet.paste(delta, (windows_crop.width * 2, 0))
        sheet.save(output_path)


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


def classify_isolated_publish_sample(metrics: dict[str, Any]) -> str:
    classification = metrics["classification"]
    if classification == "default-delta-close":
        return "isolated-publish-close"
    if classification == "default-delta-directional-match":
        return "isolated-publish-directional-match"
    return "isolated-publish-mismatch"


def rgba_for_isolated_publish_metrics(rgba: list[float]) -> list[float]:
    if any(abs(value) > 1.0 for value in rgba):
        return [value / 255.0 for value in rgba]
    return rgba


def build_yakkai_isolated_publish_sample(
    windows_variant: dict[str, Any],
    oracle_sample: dict[str, Any],
    before_path: Path,
    after_path: Path,
    from_stage: str,
    to_stage: str,
) -> dict[str, Any]:
    sample_name = oracle_sample["sampleName"]
    pixel_history = windows_variant.get("pixelHistory", {}).get(sample_name)
    if not isinstance(pixel_history, dict):
        raise FreshPublishError(f"{windows_variant.get('variant')} missing pixel history for {sample_name}")
    before_sample = sample_rgba_at_default_coordinate(
        before_path,
        oracle_sample["coordinate"],
        oracle_sample["coordinateDimensions"],
    )
    after_sample = sample_rgba_at_default_coordinate(
        after_path,
        oracle_sample["coordinate"],
        oracle_sample["coordinateDimensions"],
    )
    if (
        before_sample["imageDimensions"] != after_sample["imageDimensions"]
        or before_sample["samplePixel"] != after_sample["samplePixel"]
    ):
        raise FreshPublishError(
            f"{windows_variant.get('variant')} {sample_name} isolated publish sample pixel mismatch: "
            f"before pixel {before_sample['samplePixel']} in {before_sample['imageDimensions']}, "
            f"after pixel {after_sample['samplePixel']} in {after_sample['imageDimensions']}"
        )
    metrics = rgb_delta_metrics(
        rgba_for_isolated_publish_metrics(pixel_history["preModRgba"]),
        rgba_for_isolated_publish_metrics(pixel_history["postModRgba"]),
        before_sample["rgba"],
        after_sample["rgba"],
    )
    return {
        "sampleName": sample_name,
        "fromStage": from_stage,
        "toStage": to_stage,
        "coordinate": oracle_sample["coordinate"],
        "coordinateDimensions": oracle_sample["coordinateDimensions"],
        "windowsPreRgba": pixel_history["preModRgba"],
        "windowsPostRgba": pixel_history["postModRgba"],
        "beforeSample": before_sample,
        "afterSample": after_sample,
        "metrics": metrics,
        "classification": classify_isolated_publish_sample(metrics),
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


def combine_isolated_publish_classifications(samples: dict[str, dict[str, Any]]) -> str:
    classifications = [sample["classification"] for sample in samples.values()]
    if not classifications:
        raise FreshPublishError("no isolated publish samples were produced")
    if all(classification == "isolated-publish-close" for classification in classifications):
        return "isolated-publish-close"
    if all(
        classification in {"isolated-publish-close", "isolated-publish-directional-match"}
        for classification in classifications
    ):
        return "isolated-publish-directional-match"
    if any(
        classification in {"isolated-publish-close", "isolated-publish-directional-match"}
        for classification in classifications
    ):
        return "isolated-publish-mixed"
    return "isolated-publish-mismatch"


def build_yakkai_isolated_publish_parity(
    report: dict[str, Any],
    yakkai_variants: dict[str, dict[str, Any]],
) -> dict[str, dict[str, Any]]:
    if "yakkaiDefaultDeltaOracle" not in report:
        raise FreshPublishError("yakkaiDefaultDeltaOracle must be built before yakkaiIsolatedPublishParity")
    windows_variants = {
        variant["variant"]: variant
        for variant in report.get("variants", [])
        if isinstance(variant, dict) and isinstance(variant.get("variant"), str)
    }
    parity: dict[str, dict[str, Any]] = {}
    for variant_name in REQUIRED_VARIANTS:
        if variant_name not in windows_variants:
            raise FreshPublishError(f"missing Windows variant in report: {variant_name}")
        if variant_name not in yakkai_variants:
            raise FreshPublishError(f"missing Yakkai variant manifest: {variant_name}")
        windows_variant = windows_variants[variant_name]
        captures = yakkai_variants[variant_name]["captures"]
        from_stage, to_stage = layer_final_publish_boundary(captures)
        if from_stage != "final-display-before" or to_stage != "final-display-after":
            raise FreshPublishError(
                f"{variant_name} missing isolated final-display boundary; got {from_stage} -> {to_stage}"
            )
        from_path = Path(captures[from_stage])
        to_path = Path(captures[to_stage])
        for stage, capture_path in ((from_stage, from_path), (to_stage, to_path)):
            if not capture_path.is_file():
                raise FreshPublishError(f"{variant_name} missing {stage} capture file: {capture_path}")
        oracle_samples = report["yakkaiDefaultDeltaOracle"][variant_name]["samples"]
        samples = {
            sample_name: build_yakkai_isolated_publish_sample(
                windows_variant,
                oracle_sample,
                from_path,
                to_path,
                from_stage,
                to_stage,
            )
            for sample_name, oracle_sample in oracle_samples.items()
        }
        parity[variant_name] = {
            "classification": combine_isolated_publish_classifications(samples),
            "fromStage": from_stage,
            "toStage": to_stage,
            "samples": samples,
        }
    return parity


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
        lines.extend(["", "## Yakkai Layer Final-Publish Boundary", ""])
        lines.append(
            "| Variant | Boundary | Classification | Sample | Sample Mag | Nearest Pixel | Distance | Peak Pixel | Peak Mag |"
        )
        lines.append("| --- | --- | --- | --- | ---: | --- | ---: | --- | ---: |")
        for variant, result in report["yakkaiDefaultDeltaLocator"].items():
            boundary = result["layerFinalPublishBoundary"]
            boundary_name = f"{boundary['fromStage']} -> {boundary['toStage']}"
            for sample_name, sample in boundary["samples"].items():
                distance = sample["nearestDistancePixels"]
                distance_text = "" if distance is None else f"{distance:.4f}"
                lines.append(
                    "| {variant} | `{boundary}` | `{classification}` | {sample_name} | {sample_mag:.6f} | "
                    "`{nearest}` | {distance} | `{peak}` | {peak_mag:.6f} |".format(
                        variant=variant,
                        boundary=boundary_name,
                        classification=sample["classification"],
                        sample_name=sample_name,
                        sample_mag=sample["sampleMagnitude"],
                        nearest=sample["nearestPixel"],
                        distance=distance_text,
                        peak=sample["peakPixel"],
                        peak_mag=sample["peakMagnitude"],
                    )
                )
    if "yakkaiIsolatedPublishParity" in report:
        lines.extend(["", "## Yakkai Isolated Final-Publish Parity", ""])
        lines.append(
            "| Variant | Boundary | Classification | Sample | Sample Classification | Delta RMSE | Delta Cosine | Magnitude Ratio | Crop |"
        )
        lines.append("| --- | --- | --- | --- | --- | ---: | ---: | ---: | --- |")
        for variant, result in report["yakkaiIsolatedPublishParity"].items():
            boundary_name = f"{result['fromStage']} -> {result['toStage']}"
            for sample_name, sample in result["samples"].items():
                metrics = sample["metrics"]
                ratio = metrics["magnitudeRatio"]
                ratio_text = "" if ratio is None else f"{ratio:.6f}"
                crop = sample.get("cropPath", "")
                lines.append(
                    "| {variant} | `{boundary}` | `{variant_class}` | {sample_name} | `{sample_class}` | "
                    "{delta_rmse:.6f} | {delta_cosine:.6f} | {ratio} | `{crop}` |".format(
                        variant=variant,
                        boundary=boundary_name,
                        variant_class=result["classification"],
                        sample_name=sample_name,
                        sample_class=sample["classification"],
                        delta_rmse=metrics["deltaRmse"],
                        delta_cosine=metrics["deltaCosine"],
                        ratio=ratio_text,
                        crop=crop,
                    )
                )
    if "yakkaiContentStageAttribution" in report:
        lines.extend(["", "## Yakkai Content Stage Attribution", ""])
        lines.append("| Variant | Classification | Worst Anchor | Best Yakkai Stage | Family | RMSE | Alpha RMSE |")
        lines.append("| --- | --- | --- | --- | --- | ---: | ---: |")
        for variant, result in report["yakkaiContentStageAttribution"].items():
            target = result.get("selectedTarget") or {}
            rmse_text = "" if target.get("rmse") is None else f"{target['rmse']:.6f}"
            alpha_text = (
                ""
                if target.get("alphaWeightedRmse") is None
                else f"{target['alphaWeightedRmse']:.6f}"
            )
            lines.append(
                "| {variant} | `{classification}` | {anchor} | `{stage}` | `{family}` | {rmse} | {alpha} |".format(
                    variant=variant,
                    classification=result["classification"],
                    anchor=target.get("anchor", ""),
                    stage=target.get("stage", ""),
                    family=target.get("stageFamily", ""),
                    rmse=rmse_text,
                    alpha=alpha_text,
                )
            )
    if "yakkaiContentTransitionAttribution" in report:
        lines.extend(["", "## Yakkai Content Transition Attribution", ""])
        lines.append(
            "| Variant | Classification | Worst Transition | Best Yakkai Transition | Families | Delta RMSE | Delta Cosine |"
        )
        lines.append("| --- | --- | --- | --- | --- | ---: | ---: |")
        for variant, result in report["yakkaiContentTransitionAttribution"].items():
            target = result.get("selectedTarget") or {}
            delta_rmse_text = "" if target.get("deltaRmse") is None else f"{target['deltaRmse']:.6f}"
            delta_cosine_text = "" if target.get("deltaCosine") is None else f"{target['deltaCosine']:.6f}"
            transition_text = "{from_stage} -> {to_stage}".format(
                from_stage=target.get("fromStage", ""),
                to_stage=target.get("toStage", ""),
            )
            family_text = "{from_family} -> {to_family}".format(
                from_family=target.get("fromStageFamily", ""),
                to_family=target.get("toStageFamily", ""),
            )
            lines.append(
                "| {variant} | `{classification}` | {transition} | `{best}` | `{families}` | {rmse} | {cosine} |".format(
                    variant=variant,
                    classification=result["classification"],
                    transition=target.get("transition", ""),
                    best=transition_text,
                    families=family_text,
                    rmse=delta_rmse_text,
                    cosine=delta_cosine_text,
                )
            )
    if "yakkaiContentRangeAttribution" in report:
        lines.extend(["", "## Yakkai Content Range Attribution", ""])
        lines.append(
            "| Variant | Classification | Worst Range | Best Yakkai Range | Families | Delta RMSE | Delta Cosine |"
        )
        lines.append("| --- | --- | --- | --- | --- | ---: | ---: |")
        for variant, result in report["yakkaiContentRangeAttribution"].items():
            target = result.get("selectedTarget") or {}
            delta_rmse_text = "" if target.get("deltaRmse") is None else f"{target['deltaRmse']:.6f}"
            delta_cosine_text = "" if target.get("deltaCosine") is None else f"{target['deltaCosine']:.6f}"
            range_text = "{from_stage} -> {to_stage}".format(
                from_stage=target.get("fromStage", ""),
                to_stage=target.get("toStage", ""),
            )
            family_text = " -> ".join(target.get("stageFamilies") or [])
            lines.append(
                "| {variant} | `{classification}` | {range_name} | `{best}` | `{families}` | {rmse} | {cosine} |".format(
                    variant=variant,
                    classification=result["classification"],
                    range_name=target.get("range", ""),
                    best=range_text,
                    families=family_text,
                    rmse=delta_rmse_text,
                    cosine=delta_cosine_text,
                )
            )
    if "yakkaiMiddleBlockMicroscope" in report:
        lines.extend(["", "## Yakkai Middle-Block Microscope", ""])
        lines.append(
            "| Variant | Classification | Selected Step | Direction | Prefix-7 RMSE Change | Block Delta RMSE | Block Delta Cosine | Closest Stage | Closest RMSE |"
        )
        lines.append("| --- | --- | --- | --- | ---: | ---: | ---: | --- | ---: |")
        for variant, result in report["yakkaiMiddleBlockMicroscope"].items():
            target = result.get("selectedTarget") or {}
            closest = result.get("closestToPrefix7") or {}
            selected_text = "{from_stage} -> {to_stage}".format(
                from_stage=target.get("fromStage", ""),
                to_stage=target.get("toStage", ""),
            )
            change_text = (
                ""
                if target.get("prefix7RmseChange") is None
                else f"{target['prefix7RmseChange']:.6f}"
            )
            delta_rmse_text = (
                ""
                if target.get("windowsBlockDeltaRmse") is None
                else f"{target['windowsBlockDeltaRmse']:.6f}"
            )
            delta_cosine_text = (
                ""
                if target.get("windowsBlockDeltaCosine") is None
                else f"{target['windowsBlockDeltaCosine']:.6f}"
            )
            closest_rmse_text = (
                ""
                if closest.get("rmseToPrefix7") is None
                else f"{closest['rmseToPrefix7']:.6f}"
            )
            lines.append(
                "| {variant} | `{classification}` | `{selected}` | `{direction}` | {change} | {rmse} | {cosine} | `{closest_stage}` | {closest_rmse} |".format(
                    variant=variant,
                    classification=result["classification"],
                    selected=selected_text,
                    direction=target.get("direction", ""),
                    change=change_text,
                    rmse=delta_rmse_text,
                    cosine=delta_cosine_text,
                    closest_stage=closest.get("stage", ""),
                    closest_rmse=closest_rmse_text,
                )
            )
    if "yakkaiSelectedStepMetadata" in report:
        lines.extend(["", "## Yakkai Selected Step Metadata", ""])
        lines.append(
            "| Variant | Reason | Step | Direction | Prefix-7 Change | Block Delta RMSE | Delta Cosine | From Shader | To Shader |"
        )
        lines.append("| --- | --- | --- | --- | ---: | ---: | ---: | --- | --- |")
        for variant, result in report["yakkaiSelectedStepMetadata"].items():
            for step in result.get("steps", []):
                prefix7_change = step.get("prefix7RmseChange")
                delta_rmse = step.get("windowsBlockDeltaRmse")
                delta_cosine = step.get("windowsBlockDeltaCosine")
                from_material = step.get("fromMaterial") or {}
                to_material = step.get("toMaterial") or {}
                lines.append(
                    "| {variant} | `{reason}` | `{from_stage} -> {to_stage}` | `{direction}` | "
                    "{change} | {rmse} | {cosine} | `{from_shader}` | `{to_shader}` |".format(
                        variant=variant,
                        reason=step.get("reason", ""),
                        from_stage=step.get("fromStage", ""),
                        to_stage=step.get("toStage", ""),
                        direction=step.get("direction", ""),
                        change="" if prefix7_change is None else f"{prefix7_change:.6f}",
                        rmse="" if delta_rmse is None else f"{delta_rmse:.6f}",
                        cosine="" if delta_cosine is None else f"{delta_cosine:.6f}",
                        from_shader=from_material.get("shader", ""),
                        to_shader=to_material.get("shader", ""),
                    )
                )
        request_path = report.get("middleBlockWindowsRequestPath")
        if request_path:
            lines.extend(["", f"Windows request: `{request_path}`"])
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
        report["yakkaiIsolatedPublishParity"] = build_yakkai_isolated_publish_parity(report, yakkai_variants)
        report["yakkaiContentStageAttribution"] = build_yakkai_content_stage_attribution(
            yakkai_variants,
            extracted_root,
        )
        report["yakkaiContentTransitionAttribution"] = build_yakkai_content_transition_attribution(
            yakkai_variants,
            extracted_root,
        )
        report["yakkaiContentRangeAttribution"] = build_yakkai_content_range_attribution(
            yakkai_variants,
            extracted_root,
        )
        report["yakkaiMiddleBlockMicroscope"] = build_yakkai_middle_block_microscope(
            yakkai_variants,
            extracted_root,
        )
        report["yakkaiSelectedStepMetadata"] = build_yakkai_selected_step_metadata(
            yakkai_variants,
            report["yakkaiMiddleBlockMicroscope"],
        )
        for variant_name, parity_result in report["yakkaiIsolatedPublishParity"].items():
            captures = yakkai_variants[variant_name]["captures"]
            for sample_name, sample in parity_result["samples"].items():
                crop_path = (
                    args.output
                    / "isolated-publish-crops"
                    / f"{variant_name}-{sample_name}-final-display-delta.png"
                )
                write_locator_crop(
                    Path(captures[parity_result["fromStage"]]),
                    Path(captures[parity_result["toStage"]]),
                    sample["beforeSample"]["samplePixel"],
                    crop_path,
                )
                sample["cropPath"] = str(crop_path)
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
        for variant_name, attribution_result in report["yakkaiContentStageAttribution"].items():
            oracle_samples = report["yakkaiDefaultDeltaOracle"][variant_name]["samples"]
            for anchor, anchor_result in attribution_result["anchors"].items():
                best_match = anchor_result.get("bestMatch")
                if not best_match:
                    continue
                windows_path = Path(anchor_result["windowsPath"])
                yakkai_path = Path(best_match["path"])
                for sample_name, sample in oracle_samples.items():
                    crop_path = (
                        args.output
                        / "content-stage-crops"
                        / f"{variant_name}-{anchor}-{sample_name}-{best_match['stage']}.png"
                    )
                    write_stage_match_crop(
                        windows_path,
                        yakkai_path,
                        sample["coordinate"],
                        sample["coordinateDimensions"],
                        crop_path,
                    )
                    best_match.setdefault("cropPaths", {})[sample_name] = str(crop_path)
        for variant_name, attribution_result in report["yakkaiContentTransitionAttribution"].items():
            oracle_samples = report["yakkaiDefaultDeltaOracle"][variant_name]["samples"]
            for transition, transition_result in attribution_result["transitions"].items():
                best_match = transition_result.get("bestMatch")
                if not best_match:
                    continue
                windows_before_path = Path(transition_result["windowsBeforePath"])
                windows_after_path = Path(transition_result["windowsAfterPath"])
                yakkai_before_path = Path(best_match["fromPath"])
                yakkai_after_path = Path(best_match["toPath"])
                for sample_name, sample in oracle_samples.items():
                    crop_path = (
                        args.output
                        / "content-transition-crops"
                        / (
                            f"{variant_name}-{transition}-{sample_name}-"
                            f"{best_match['fromStage']}-to-{best_match['toStage']}.png"
                        )
                    )
                    write_transition_match_crop(
                        windows_before_path,
                        windows_after_path,
                        yakkai_before_path,
                        yakkai_after_path,
                        sample["coordinate"],
                        sample["coordinateDimensions"],
                        crop_path,
                    )
                    best_match.setdefault("cropPaths", {})[sample_name] = str(crop_path)
        for variant_name, attribution_result in report["yakkaiContentRangeAttribution"].items():
            oracle_samples = report["yakkaiDefaultDeltaOracle"][variant_name]["samples"]
            for range_name, range_result in attribution_result["ranges"].items():
                best_match = range_result.get("bestMatch")
                if not best_match:
                    continue
                windows_before_path = Path(range_result["windowsBeforePath"])
                windows_after_path = Path(range_result["windowsAfterPath"])
                yakkai_before_path = Path(best_match["fromPath"])
                yakkai_after_path = Path(best_match["toPath"])
                for sample_name, sample in oracle_samples.items():
                    crop_path = (
                        args.output
                        / "content-range-crops"
                        / (
                            f"{variant_name}-{range_name}-{sample_name}-"
                            f"{best_match['fromStage']}-to-{best_match['toStage']}.png"
                        )
                    )
                    write_transition_match_crop(
                        windows_before_path,
                        windows_after_path,
                        yakkai_before_path,
                        yakkai_after_path,
                        sample["coordinate"],
                        sample["coordinateDimensions"],
                        crop_path,
                    )
                    best_match.setdefault("cropPaths", {})[sample_name] = str(crop_path)
        for variant_name, microscope_result in report["yakkaiMiddleBlockMicroscope"].items():
            oracle_samples = report["yakkaiDefaultDeltaOracle"][variant_name]["samples"]
            selected_target = microscope_result.get("selectedTarget")
            if not selected_target:
                continue
            windows_prefix3_path = Path(microscope_result["windowsPrefix3Path"])
            windows_prefix7_path = Path(microscope_result["windowsPrefix7Path"])
            yakkai_before_path = Path(selected_target["fromPath"])
            yakkai_after_path = Path(selected_target["toPath"])
            for sample_name, sample in oracle_samples.items():
                crop_path = (
                    args.output
                    / "middle-block-crops"
                    / (
                        f"{variant_name}-selected-{sample_name}-"
                        f"{selected_target['fromStage']}-to-{selected_target['toStage']}.png"
                    )
                )
                write_transition_match_crop(
                    windows_prefix3_path,
                    windows_prefix7_path,
                    yakkai_before_path,
                    yakkai_after_path,
                    sample["coordinate"],
                    sample["coordinateDimensions"],
                    crop_path,
                )
                selected_target.setdefault("cropPaths", {})[sample_name] = str(crop_path)
        for variant_name, metadata_result in report["yakkaiSelectedStepMetadata"].items():
            oracle_samples = report["yakkaiDefaultDeltaOracle"][variant_name]["samples"]
            windows_prefix3_path = Path(metadata_result["windowsPrefix3Path"])
            windows_prefix7_path = Path(metadata_result["windowsPrefix7Path"])
            for step in metadata_result["steps"]:
                yakkai_before_path = Path(step["fromPath"])
                yakkai_after_path = Path(step["toPath"])
                for sample_name, sample in oracle_samples.items():
                    crop_path = (
                        args.output
                        / "selected-step-crops"
                        / (
                            f"{variant_name}-{step['reason']}-{sample_name}-"
                            f"{step['fromStage']}-to-{step['toStage']}.png"
                        )
                    )
                    write_transition_match_crop(
                        windows_prefix3_path,
                        windows_prefix7_path,
                        yakkai_before_path,
                        yakkai_after_path,
                        sample["coordinate"],
                        sample["coordinateDimensions"],
                        crop_path,
                    )
                    step.setdefault("cropPaths", {})[sample_name] = str(crop_path)
        request_path = args.output / "middle-block-windows-request.md"
        request_path.write_text(
            middle_block_windows_request_markdown(report["yakkaiSelectedStepMetadata"]),
            encoding="utf-8",
        )
        report["middleBlockWindowsRequestPath"] = str(request_path)
    write_summary(report, args.windows, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
