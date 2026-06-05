#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Iterable

import numpy as np
from PIL import Image, ImageDraw


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"expected JSON object in {path}")
    return data


def load_rgba(path: Path) -> np.ndarray:
    with Image.open(path).convert("RGBA") as image:
        return np.asarray(image, dtype=np.float32) / np.float32(255.0)


def save_rgba(path: Path, pixels: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    out = (np.clip(pixels, 0.0, 1.0) * 255.0).round().astype(np.uint8)
    Image.fromarray(out, "RGBA").save(path)


def sample_bilinear_rgba(image: np.ndarray, uv: np.ndarray) -> np.ndarray:
    pixels = np.asarray(image, dtype=np.float32)
    coords = np.asarray(uv, dtype=np.float32)
    if pixels.ndim != 3 or pixels.shape[2] < 4:
        raise ValueError("image must be an HxWxRGBA array")
    if coords.shape[-1] < 2:
        raise ValueError("uv must have at least two channels")

    height, width = pixels.shape[:2]
    x = np.clip(coords[..., 0] * width - 0.5, 0.0, max(width - 1, 0))
    y = np.clip(coords[..., 1] * height - 0.5, 0.0, max(height - 1, 0))

    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    x1 = np.clip(x0 + 1, 0, width - 1)
    y1 = np.clip(y0 + 1, 0, height - 1)
    wx = (x - x0)[..., None]
    wy = (y - y0)[..., None]

    top = pixels[y0, x0, :4] * (1.0 - wx) + pixels[y0, x1, :4] * wx
    bottom = pixels[y1, x0, :4] * (1.0 - wx) + pixels[y1, x1, :4] * wx
    return top * (1.0 - wy) + bottom * wy


def sample_nearest_rgba(image: np.ndarray, uv: np.ndarray) -> np.ndarray:
    pixels = np.asarray(image, dtype=np.float32)
    coords = np.asarray(uv, dtype=np.float32)
    if pixels.ndim != 3 or pixels.shape[2] < 4:
        raise ValueError("image must be an HxWxRGBA array")
    height, width = pixels.shape[:2]
    x = np.clip(np.floor(coords[..., 0] * width), 0, width - 1).astype(np.int64)
    y = np.clip(np.floor(coords[..., 1] * height), 0, height - 1).astype(np.int64)
    return pixels[y, x, :4]


def preserve_source_alpha(displaced: np.ndarray, source: np.ndarray, blend_edge_rgb: bool = True) -> np.ndarray:
    displaced_pixels = np.asarray(displaced, dtype=np.float32)
    source_pixels = np.asarray(source, dtype=np.float32)
    if displaced_pixels.shape != source_pixels.shape or displaced_pixels.shape[-1] < 4:
        raise ValueError("displaced and source must be matching RGBA arrays")

    out = displaced_pixels.copy()
    if blend_edge_rgb:
        source_alpha = source_pixels[..., 3:4]
        displaced_alpha = displaced_pixels[..., 3:4]
        coverage = np.clip(displaced_alpha / np.maximum(source_alpha, np.float32(1.0e-4)), 0.0, 1.0)
        out[..., :3] = source_pixels[..., :3] * (1.0 - coverage) + displaced_pixels[..., :3] * coverage
    out[..., 3:4] = source_pixels[..., 3:4]
    return out


def waterwaves_texcoord_offset(
    uv: np.ndarray,
    mask: np.ndarray,
    time_seconds: float,
    strength: float,
    speed: float,
    frequency: float,
    direction: tuple[float, float],
    exponent: float = 1.0,
    phase_direction: tuple[float, float] | None = None,
    secondary_speed: float | None = None,
    secondary_frequency: float = 66.0,
    secondary_offset: float = 0.0,
    secondary_exponent: float = 1.0,
    secondary_phase_direction: tuple[float, float] | None = None,
) -> np.ndarray:
    coords = np.asarray(uv, dtype=np.float32)
    mask_pixels = np.asarray(mask, dtype=np.float32)
    if coords.shape[-1] < 2:
        raise ValueError("uv must have at least two channels")
    if mask_pixels.shape[:2] != coords.shape[:2]:
        raise ValueError("mask and uv must have matching image dimensions")
    if mask_pixels.ndim == 2:
        mask_r = mask_pixels
    else:
        mask_r = mask_pixels[..., 0]

    move = np.asarray(direction, dtype=np.float32)
    phase = np.asarray(phase_direction if phase_direction is not None else direction, dtype=np.float32)
    distance = np.float32(time_seconds * speed) + (
        coords[..., 0] * np.float32(phase[0]) + coords[..., 1] * np.float32(phase[1])
    ) * np.float32(frequency)
    wave = np.sin(distance)
    signed_wave = np.sign(wave) * np.power(np.abs(wave), np.float32(exponent))
    if secondary_speed is not None:
        phase2 = np.asarray(
            secondary_phase_direction if secondary_phase_direction is not None else (0.0, 1.0),
            dtype=np.float32,
        )
        distance2 = np.float32((time_seconds + secondary_offset) * secondary_speed) + (
            coords[..., 0] * np.float32(phase2[0]) + coords[..., 1] * np.float32(phase2[1])
        ) * np.float32(secondary_frequency)
        wave2 = np.sin(distance2)
        signed_wave2 = np.sign(wave2) * np.power(np.abs(wave2), np.float32(secondary_exponent))
        signed_wave = signed_wave * signed_wave2
    return signed_wave[..., None] * move * np.float32(strength * strength) * mask_r[..., None]


def direction_vectors(angle: float) -> tuple[tuple[float, float], tuple[float, float]]:
    wave = (-math.sin(angle), math.cos(angle))
    offset = (math.cos(angle), math.sin(angle))
    return wave, offset


def uv_grid_for_crop(
    full_width: int,
    full_height: int,
    crop: tuple[int, int, int, int],
    half_texel_offset: bool = False,
) -> np.ndarray:
    x0, y0, x1, y1 = crop
    half = np.float32(0.0 if half_texel_offset else 0.5)
    xs = (np.arange(x0, x1, dtype=np.float32) + half) / np.float32(full_width)
    ys = (np.arange(y0, y1, dtype=np.float32) + half) / np.float32(full_height)
    u, v = np.meshgrid(xs, ys)
    return np.stack((u, v), axis=2)


def global_uv_to_local(uv: np.ndarray, full_size: tuple[int, int], crop: tuple[int, int, int, int]) -> np.ndarray:
    full_width, full_height = full_size
    x0, y0, x1, y1 = crop
    crop_width = x1 - x0
    crop_height = y1 - y0
    local = np.empty_like(uv, dtype=np.float32)
    local[..., 0] = (uv[..., 0] * np.float32(full_width) - np.float32(x0)) / np.float32(crop_width)
    local[..., 1] = (uv[..., 1] * np.float32(full_height) - np.float32(y0)) / np.float32(crop_height)
    return local


def first_float(values: dict, key: str, default: float) -> float:
    raw = values.get(key)
    if isinstance(raw, list) and raw:
        try:
            return float(raw[0])
        except (TypeError, ValueError):
            return default
    try:
        return float(raw)
    except (TypeError, ValueError):
        return default


def waterwaves_params(material: dict) -> dict:
    values = material.get("materialValues", {})
    constants = material.get("resolvedConstValues", {})
    return {
        "direction": first_float(constants, "g_Direction", first_float(values, "direction", 0.0)),
        "direction2": first_float(constants, "g_Direction2", first_float(values, "direction2", 0.0)),
        "exponent": first_float(constants, "g_Exponent", first_float(values, "exponent", 1.0)),
        "exponent2": first_float(constants, "g_Exponent2", first_float(values, "exponent2", 1.0)),
        "offset2": first_float(constants, "g_Offset2", first_float(values, "offset2", 0.0)),
        "scale": first_float(constants, "g_Scale", first_float(values, "scale", 1.0)),
        "scale2": first_float(constants, "g_Scale2", first_float(values, "scale2", 66.0)),
        "speed": first_float(constants, "g_Speed", first_float(values, "speed", 1.0)),
        "speed2": first_float(constants, "g_Speed2", first_float(values, "speed2", 3.0)),
        "strength": first_float(constants, "g_Strength", first_float(values, "strength", 0.0)),
    }


def _layer_id(record: dict) -> int | None:
    layer = record.get("layer", {})
    return layer.get("layerId", record.get("layerId"))


def layer_record_for_stage(manifest: dict, layer_id: int, stage: str) -> dict:
    for record in manifest.get("captures", []):
        if record.get("stage") != stage:
            continue
        if _layer_id(record) == layer_id:
            return record
    raise KeyError(f"capture not found for layer {layer_id} stage {stage}")


def capture_path_for_stage(manifest: dict, layer_id: int, stage: str) -> Path:
    record = layer_record_for_stage(manifest, layer_id, stage)
    path = record.get("path")
    if not path:
        raise KeyError(f"capture path missing for layer {layer_id} stage {stage}")
    return Path(path)


def capture_time_seconds(manifest: dict) -> float:
    if manifest.get("shaderTimeSeconds") is not None:
        return float(manifest["shaderTimeSeconds"])
    if manifest.get("effectiveCaptureTimeSeconds") is not None:
        return float(manifest["effectiveCaptureTimeSeconds"])
    return float(manifest.get("captureDelayMs", 0.0)) / 1000.0


def waterwaves_materials(manifest: dict, layer_id: int) -> list[dict]:
    record = layer_record_for_stage(manifest, layer_id, "effect-input")
    return [
        material
        for material in record.get("layer", {}).get("effectMaterials", [])
        if material.get("shader") == "effects/waterwaves"
    ]


def texture_binding(material: dict, slot: int) -> str:
    for binding in material.get("textureBindings", []):
        if binding.get("slot") == slot:
            resolved = binding.get("resolved")
            if resolved:
                return str(resolved)
    raise KeyError(f"texture slot {slot} not found for effect {material.get('effectIndex')}")


def texture_package_path(texture_name: str) -> str:
    if texture_name.endswith(".tex"):
        texture_name = texture_name[:-4]
    if texture_name.startswith("materials/"):
        return texture_name + ".tex"
    return f"materials/{texture_name}.tex"


def load_texture(scene_root: Path, texture_name: str) -> tuple[np.ndarray, dict]:
    from arona_mask_effect_parity import decode_tex_image

    package_path = texture_package_path(texture_name)
    if scene_root.is_dir():
        data = (scene_root / package_path).read_bytes()
    else:
        from arona_lut_sampling_lab import PkgIndex

        data = PkgIndex.from_path(scene_root).read(package_path)
    tex = decode_tex_image(data)
    info = {
        "texture": texture_name,
        "packagePath": package_path,
        "width": tex.width,
        "height": tex.height,
        "formatId": tex.format_id,
        "filterMode": tex.filter_mode,
        "wrapMode": tex.wrap_mode,
        "noInterpolation": tex.no_interpolation,
        "clampUvs": tex.clamp_uvs,
    }
    return np.asarray(tex.pixels, dtype=np.float32), info


def load_mask_textures(scene_root: Path, materials: Iterable[dict]) -> tuple[dict[str, np.ndarray], list[dict]]:
    textures: dict[str, np.ndarray] = {}
    infos: list[dict] = []
    for material in materials:
        texture = texture_binding(material, 1)
        if texture in textures:
            continue
        pixels, info = load_texture(scene_root, texture)
        textures[texture] = pixels
        infos.append(info)
    return textures, infos


def layer_local_bounds_to_pixels(bounds: Iterable[float], image_size: tuple[int, int]) -> tuple[int, int, int, int]:
    width, height = image_size
    min_x, min_y, max_x, max_y = [float(value) for value in bounds]
    return (
        round(min_x + width / 2),
        round(min_y + height / 2),
        round(max_x + width / 2),
        round(max_y + height / 2),
    )


def slot_union_pixels(
    coverage: Iterable[dict],
    image_size: tuple[int, int],
    slots: tuple[int, ...] = (3, 13),
    padding: int = 220,
) -> tuple[int, int, int, int]:
    wanted = set(slots)
    regions: list[tuple[int, int, int, int]] = []
    for item in coverage:
        if item.get("slot") not in wanted:
            continue
        bounds = item.get("layerLocalBounds")
        if not isinstance(bounds, list) or len(bounds) != 4:
            continue
        regions.append(layer_local_bounds_to_pixels(bounds, image_size))
    if not regions:
        raise ValueError(f"no slot bounds found for slots {sorted(wanted)}")

    width, height = image_size
    x0 = max(0, min(region[0] for region in regions) - padding)
    y0 = max(0, min(region[1] for region in regions) - padding)
    x1 = min(width, max(region[2] for region in regions) + padding)
    y1 = min(height, max(region[3] for region in regions) + padding)
    return (x0, y0, x1, y1)


def inner_crop_from_padded(
    padded: tuple[int, int, int, int],
    inner: tuple[int, int, int, int],
) -> tuple[int, int, int, int]:
    px0, py0, _, _ = padded
    x0, y0, x1, y1 = inner
    return (x0 - px0, y0 - py0, x1 - px0, y1 - py0)


def apply_waterwaves_sequence_crop(
    source: np.ndarray,
    materials: list[dict],
    masks: dict[str, np.ndarray],
    time_seconds: float,
    padded_crop: tuple[int, int, int, int],
    alpha_mode: str,
    mask_sampler: str = "bilinear",
    half_texel_offset: bool = False,
    force_dualwaves: bool = False,
) -> np.ndarray:
    full_height, full_width = source.shape[:2]
    x0, y0, x1, y1 = padded_crop
    current = source[y0:y1, x0:x1, :].copy()
    uv = uv_grid_for_crop(full_width, full_height, padded_crop, half_texel_offset=half_texel_offset)
    sample_mask = sample_nearest_rgba if mask_sampler == "nearest" else sample_bilinear_rgba

    for material in materials:
        params = waterwaves_params(material)
        wave_direction, offset_direction = direction_vectors(params["direction"])
        wave_direction2, _ = direction_vectors(params["direction2"])
        mask_texture = masks[texture_binding(material, 1)]
        sampled_mask = sample_mask(mask_texture, uv)
        offset = waterwaves_texcoord_offset(
            uv,
            sampled_mask,
            time_seconds=time_seconds,
            strength=params["strength"],
            speed=params["speed"],
            frequency=params["scale"],
            direction=offset_direction,
            exponent=params["exponent"],
            phase_direction=wave_direction,
            secondary_speed=params["speed2"] if force_dualwaves else None,
            secondary_frequency=params["scale2"],
            secondary_offset=params["offset2"],
            secondary_exponent=params["exponent2"],
            secondary_phase_direction=wave_direction2,
        )
        displaced = sample_bilinear_rgba(
            current,
            global_uv_to_local(uv + offset, (full_width, full_height), padded_crop),
        )
        if alpha_mode == "source-alpha":
            current = preserve_source_alpha(displaced, current, blend_edge_rgb=True)
        elif alpha_mode == "source-alpha-no-edge-blend":
            current = preserve_source_alpha(displaced, current, blend_edge_rgb=False)
        elif alpha_mode == "displaced-alpha":
            current = displaced
        else:
            raise ValueError(f"unknown alpha mode: {alpha_mode}")
    return current


def image_metrics(left: np.ndarray, right: np.ndarray, crop: tuple[int, int, int, int] | None = None) -> dict:
    a = left
    b = right
    if crop is not None:
        x0, y0, x1, y1 = crop
        a = a[y0:y1, x0:x1]
        b = b[y0:y1, x0:x1]
    if a.shape != b.shape:
        raise ValueError(f"image shapes differ: {a.shape} vs {b.shape}")
    diff = a - b
    alpha_a = a[..., 3] > (1.0 / 255.0)
    alpha_b = b[..., 3] > (1.0 / 255.0)
    union = np.logical_or(alpha_a, alpha_b)
    intersection = np.logical_and(alpha_a, alpha_b)
    return {
        "rmse": round(float(np.sqrt(np.mean(diff * diff))), 8),
        "rgbRmse": round(float(np.sqrt(np.mean(diff[..., :3] * diff[..., :3]))), 8),
        "alphaRmse": round(float(np.sqrt(np.mean(diff[..., 3] * diff[..., 3]))), 8),
        "meanAbs": round(float(np.mean(np.abs(diff))), 8),
        "maxAbs": round(float(np.max(np.abs(diff))), 8),
        "visibleIou": round(float(np.count_nonzero(intersection) / max(np.count_nonzero(union), 1)), 8),
        "shape": list(a.shape),
    }


def diff_preview(left: np.ndarray, right: np.ndarray, gain: float = 8.0) -> np.ndarray:
    out = np.clip(np.abs(left - right) * gain, 0.0, 1.0)
    out[..., 3] = 1.0
    return out


def alpha_composite_preview(pixels: np.ndarray, background: tuple[int, int, int] = (32, 36, 42)) -> Image.Image:
    rgba = (np.clip(pixels, 0.0, 1.0) * 255.0).round().astype(np.uint8)
    image = Image.fromarray(rgba, "RGBA")
    bg = Image.new("RGBA", image.size, (*background, 255))
    return Image.alpha_composite(bg, image).convert("RGB")


def write_contact_sheet(
    entries: list[tuple[str, np.ndarray]],
    output_path: Path,
    thumb_width: int = 360,
) -> None:
    previews: list[tuple[str, Image.Image]] = []
    for label, pixels in entries:
        preview = alpha_composite_preview(pixels)
        ratio = thumb_width / preview.width
        preview = preview.resize((thumb_width, max(1, round(preview.height * ratio))), Image.Resampling.LANCZOS)
        previews.append((label, preview))

    label_height = 24
    gutter = 10
    width = thumb_width * len(previews) + gutter * (len(previews) + 1)
    height = max(image.height for _, image in previews) + label_height + gutter * 2
    sheet = Image.new("RGB", (width, height), (18, 22, 28))
    draw = ImageDraw.Draw(sheet)
    for index, (label, image) in enumerate(previews):
        x = gutter + index * (thumb_width + gutter)
        sheet.paste(image, (x, gutter))
        draw.text((x, gutter + image.height + 4), label, fill=(235, 240, 245))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output_path)


def load_prefix_motion_ratio(default_path: Path = Path("md-fixes/arona-we-prefix-boundary-bisect/reports/prefix-boundary-report.json")) -> dict:
    if not default_path.exists():
        return {
            "available": False,
            "ratio": 1.0,
            "weAmplitude": 0.0,
            "yakkaiAmplitude": 0.0,
            "region": "",
            "frequency": "",
        }
    report = load_json(default_path)
    classification = report.get("classification", {})
    return {
        "available": True,
        "ratio": float(classification.get("amplitudeRatio", 1.0)),
        "weAmplitude": float(classification.get("weAmplitude", 0.0)),
        "yakkaiAmplitude": float(classification.get("yakkaiAmplitude", 0.0)),
        "region": str(classification.get("motionRegion", "")),
        "frequency": str(classification.get("motionFrequency", "")),
    }


def classify_waterwaves_mismatch(
    current_gpu_rmse: float,
    source_alpha_rmse: float,
    displaced_alpha_rmse: float,
    uv_variant_rmse: float,
    time_variant_rmse: float,
    final_display_lost_motion: bool,
    windows_motion_ratio_current: float,
    windows_motion_ratio_displaced_alpha: float,
) -> dict:
    if final_display_lost_motion:
        return {
            "classification": "final-display-composition-mismatch",
            "reason": "Layer-local effect output changes materially, but final-display contribution loses the motion.",
        }
    if (
        windows_motion_ratio_current <= 0.35
        and windows_motion_ratio_displaced_alpha >= 0.35
        and displaced_alpha_rmse + 0.002 < source_alpha_rmse
    ):
        return {
            "classification": "source-alpha-suppresses-motion",
            "reason": "Current source-alpha-preserved waterwaves matches Yakkai while the displaced-alpha model better preserves WE-like motion amplitude.",
        }
    if current_gpu_rmse > 0.05 and uv_variant_rmse + 0.01 < current_gpu_rmse:
        return {
            "classification": "sampler-coordinate-mismatch",
            "reason": "A UV/sampler variant is materially closer to the GPU boundary than the current model.",
        }
    if current_gpu_rmse > 0.05 and time_variant_rmse + 0.01 < current_gpu_rmse:
        return {
            "classification": "effect-time-mismatch",
            "reason": "A shader-time variant is materially closer to the GPU boundary than the manifest time.",
        }
    if current_gpu_rmse > 0.05:
        return {
            "classification": "waterwaves-shader-math-mismatch",
            "reason": "The CPU waterwaves model does not match the GPU boundary closely enough.",
        }
    if windows_motion_ratio_current <= 0.35:
        return {
            "classification": "windows-oracle-insufficient",
            "reason": "The CPU model matches Yakkai, but this single-frame evidence does not prove which generic change restores Windows WE motion.",
        }
    return {
        "classification": "no-generic-mismatch-found",
        "reason": "The CPU model matches Yakkai and no Windows motion cliff remains in the supplied prefix report.",
    }


def write_summary_markdown(path: Path, report: dict) -> None:
    lines = [
        "# Arona Waterwaves Output Oracle",
        "",
        f"Classification: {report['classification']}",
        f"Reason: {report['reason']}",
        "",
    ]
    if "motion" in report:
        motion = report["motion"]
        lines.extend([
            "## Prefix Motion",
            "",
            f"- Region: `{motion.get('region', '')}`",
            f"- Frequency: `{motion.get('frequency', '')}`",
            f"- WE amplitude: `{motion.get('weAmplitude', 0.0)}`",
            f"- Yakkai amplitude: `{motion.get('yakkaiAmplitude', 0.0)}`",
            f"- Current ratio: `{motion.get('ratio', 0.0)}`",
            "",
        ])
    if "metrics" in report:
        lines.extend(["## Metrics", ""])
        for name, values in report["metrics"].items():
            if isinstance(values, dict) and "rmse" in values:
                lines.append(
                    f"- `{name}`: rmse `{values['rmse']}`, rgb `{values['rgbRmse']}`, "
                    f"alpha `{values['alphaRmse']}`, visible IoU `{values['visibleIou']}`"
                )
            else:
                lines.append(f"- `{name}`: `{values}`")
        lines.append("")
    if "materials" in report:
        lines.extend(["## Waterwaves Materials", ""])
        for material in report["materials"]:
            lines.append(
                f"- Effect `{material['effectIndex']}` texture `{material['maskTexture']}` "
                f"strength `{material['params']['strength']}` speed `{material['params']['speed']}` "
                f"scale `{material['params']['scale']}` direction `{material['params']['direction']}` "
                f"preserveAlpha `{material['preserveSourceAlpha']}`"
            )
        lines.append("")
    if "outputs" in report:
        lines.extend(["## Outputs", ""])
        for name, output in report["outputs"].items():
            lines.append(f"- `{name}`: `{output}`")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_oracle_report(
    pulse_manifest: dict,
    first_waterwaves_manifest: dict,
    full_waterwaves_manifest: dict,
    scene_root: Path,
    layer_id: int,
    output_dir: Path,
) -> dict:
    output_dir.mkdir(parents=True, exist_ok=True)
    root = output_dir.parent
    cpu_dir = root / "cpu"
    contact_dir = root / "contact-sheets"
    crop_dir = root / "crops"
    cpu_dir.mkdir(parents=True, exist_ok=True)
    contact_dir.mkdir(parents=True, exist_ok=True)
    crop_dir.mkdir(parents=True, exist_ok=True)

    source_path = capture_path_for_stage(pulse_manifest, layer_id, "effect-output")
    first_gpu_path = capture_path_for_stage(first_waterwaves_manifest, layer_id, "effect-output")
    full_gpu_path = capture_path_for_stage(full_waterwaves_manifest, layer_id, "effect-output")
    source = load_rgba(source_path)
    first_gpu = load_rgba(first_gpu_path)
    full_gpu = load_rgba(full_gpu_path)
    if source.shape != full_gpu.shape:
        raise ValueError(f"source/full shape mismatch: {source.shape} vs {full_gpu.shape}")

    full_record = layer_record_for_stage(full_waterwaves_manifest, layer_id, "effect-input")
    first_materials = waterwaves_materials(first_waterwaves_manifest, layer_id)
    full_materials = waterwaves_materials(full_waterwaves_manifest, layer_id)
    if not first_materials:
        raise ValueError("first waterwaves manifest has no waterwaves material")
    if len(full_materials) < 5:
        raise ValueError(f"expected five full waterwaves materials, found {len(full_materials)}")
    masks, mask_infos = load_mask_textures(scene_root, full_materials)

    image_size = (source.shape[1], source.shape[0])
    coverage = full_record.get("layer", {}).get("publish", {}).get("puppetCutoutSlotCoverage", [])
    inner = slot_union_pixels(coverage, image_size, slots=(3, 13), padding=80)
    padded = slot_union_pixels(coverage, image_size, slots=(3, 13), padding=260)
    local_inner = inner_crop_from_padded(padded, inner)
    px0, py0, px1, py1 = padded
    full_gpu_crop = full_gpu[py0:py1, px0:px1]
    first_gpu_crop = first_gpu[py0:py1, px0:px1]
    source_crop = source[py0:py1, px0:px1]

    current = apply_waterwaves_sequence_crop(
        source,
        full_materials,
        masks,
        capture_time_seconds(full_waterwaves_manifest),
        padded,
        alpha_mode="source-alpha",
    )
    displaced = apply_waterwaves_sequence_crop(
        source,
        full_materials,
        masks,
        capture_time_seconds(full_waterwaves_manifest),
        padded,
        alpha_mode="displaced-alpha",
    )
    no_edge_blend = apply_waterwaves_sequence_crop(
        source,
        full_materials,
        masks,
        capture_time_seconds(full_waterwaves_manifest),
        padded,
        alpha_mode="source-alpha-no-edge-blend",
    )
    nearest_mask = apply_waterwaves_sequence_crop(
        source,
        full_materials,
        masks,
        capture_time_seconds(full_waterwaves_manifest),
        padded,
        alpha_mode="source-alpha",
        mask_sampler="nearest",
    )
    half_texel = apply_waterwaves_sequence_crop(
        source,
        full_materials,
        masks,
        capture_time_seconds(full_waterwaves_manifest),
        padded,
        alpha_mode="source-alpha",
        half_texel_offset=True,
    )
    dualwaves = apply_waterwaves_sequence_crop(
        source,
        full_materials,
        masks,
        capture_time_seconds(full_waterwaves_manifest),
        padded,
        alpha_mode="source-alpha",
        force_dualwaves=True,
    )
    time_minus = apply_waterwaves_sequence_crop(
        source,
        full_materials,
        masks,
        capture_time_seconds(full_waterwaves_manifest) - 0.1,
        padded,
        alpha_mode="source-alpha",
    )
    time_plus = apply_waterwaves_sequence_crop(
        source,
        full_materials,
        masks,
        capture_time_seconds(full_waterwaves_manifest) + 0.1,
        padded,
        alpha_mode="source-alpha",
    )
    first_current = apply_waterwaves_sequence_crop(
        source,
        first_materials,
        masks,
        capture_time_seconds(first_waterwaves_manifest),
        padded,
        alpha_mode="source-alpha",
    )

    outputs = {
        "current-yakkai-source-alpha": str(cpu_dir / "current-yakkai-source-alpha.png"),
        "we-displaced-alpha": str(cpu_dir / "we-displaced-alpha.png"),
        "source-alpha-no-edge-blend": str(cpu_dir / "source-alpha-no-edge-blend.png"),
        "gpu-full-waterwaves": str(cpu_dir / "gpu-full-waterwaves.png"),
        "gpu-first-waterwaves": str(cpu_dir / "gpu-first-waterwaves.png"),
        "dualwaves-defaults": str(cpu_dir / "dualwaves-defaults.png"),
        "diff-current-vs-gpu": str(cpu_dir / "diff-current-vs-gpu.png"),
        "waterwaves-variant-contact": str(contact_dir / "waterwaves-variant-contact.png"),
    }
    save_rgba(Path(outputs["current-yakkai-source-alpha"]), current)
    save_rgba(Path(outputs["we-displaced-alpha"]), displaced)
    save_rgba(Path(outputs["source-alpha-no-edge-blend"]), no_edge_blend)
    save_rgba(Path(outputs["gpu-full-waterwaves"]), full_gpu_crop)
    save_rgba(Path(outputs["gpu-first-waterwaves"]), first_gpu_crop)
    save_rgba(Path(outputs["dualwaves-defaults"]), dualwaves)
    save_rgba(Path(outputs["diff-current-vs-gpu"]), diff_preview(current, full_gpu_crop))
    write_contact_sheet(
        [
            ("source pulse", source_crop[local_inner[1]:local_inner[3], local_inner[0]:local_inner[2]]),
            ("current source-alpha", current[local_inner[1]:local_inner[3], local_inner[0]:local_inner[2]]),
            ("displaced alpha", displaced[local_inner[1]:local_inner[3], local_inner[0]:local_inner[2]]),
            ("DUALWAVES defaults", dualwaves[local_inner[1]:local_inner[3], local_inner[0]:local_inner[2]]),
            ("GPU prefix7", full_gpu_crop[local_inner[1]:local_inner[3], local_inner[0]:local_inner[2]]),
            ("diff x8", diff_preview(current, full_gpu_crop)[local_inner[1]:local_inner[3], local_inner[0]:local_inner[2]]),
        ],
        Path(outputs["waterwaves-variant-contact"]),
    )

    metrics = {
        "currentVsGpuCrop": image_metrics(current, full_gpu_crop, local_inner),
        "displacedAlphaVsGpuCrop": image_metrics(displaced, full_gpu_crop, local_inner),
        "noEdgeBlendVsGpuCrop": image_metrics(no_edge_blend, full_gpu_crop, local_inner),
        "nearestMaskVsGpuCrop": image_metrics(nearest_mask, full_gpu_crop, local_inner),
        "halfTexelVsGpuCrop": image_metrics(half_texel, full_gpu_crop, local_inner),
        "dualwavesDefaultsVsGpuCrop": image_metrics(dualwaves, full_gpu_crop, local_inner),
        "timeMinus100msVsGpuCrop": image_metrics(time_minus, full_gpu_crop, local_inner),
        "timePlus100msVsGpuCrop": image_metrics(time_plus, full_gpu_crop, local_inner),
        "currentVsDisplacedAlphaCrop": image_metrics(current, displaced, local_inner),
        "currentVsDualwavesDefaultsCrop": image_metrics(current, dualwaves, local_inner),
        "firstWaterwavesCurrentVsGpuCrop": image_metrics(first_current, first_gpu_crop, local_inner),
        "sourcePulseVsGpuFullWaterwavesCrop": image_metrics(source_crop, full_gpu_crop, local_inner),
    }
    motion = load_prefix_motion_ratio()
    alpha_delta = metrics["currentVsDisplacedAlphaCrop"]["alphaRmse"]
    displaced_motion_proxy = min(1.0, float(motion["ratio"]) + alpha_delta * 24.0)
    best_uv = min(metrics["nearestMaskVsGpuCrop"]["rmse"], metrics["halfTexelVsGpuCrop"]["rmse"])
    best_time = min(metrics["timeMinus100msVsGpuCrop"]["rmse"], metrics["timePlus100msVsGpuCrop"]["rmse"])
    final_display_lost_motion = False
    classification = classify_waterwaves_mismatch(
        current_gpu_rmse=metrics["currentVsGpuCrop"]["rmse"],
        source_alpha_rmse=metrics["currentVsGpuCrop"]["rmse"],
        displaced_alpha_rmse=metrics["displacedAlphaVsGpuCrop"]["rmse"],
        uv_variant_rmse=best_uv,
        time_variant_rmse=best_time,
        final_display_lost_motion=final_display_lost_motion,
        windows_motion_ratio_current=float(motion["ratio"]),
        windows_motion_ratio_displaced_alpha=displaced_motion_proxy,
    )

    material_summary = []
    for material in full_materials:
        combos = material.get("resolvedCombos", {})
        material_summary.append({
            "effectIndex": material.get("effectIndex"),
            "shader": material.get("shader"),
            "maskTexture": texture_binding(material, 1),
            "params": waterwaves_params(material),
            "resolvedCombos": combos,
            "preserveSourceAlpha": combos.get("YAKKAI_PRESERVE_SOURCE_ALPHA") == "1",
        })

    report = {
        **classification,
        "layerId": layer_id,
        "sourcePath": str(source_path),
        "firstWaterwavesGpuPath": str(first_gpu_path),
        "fullWaterwavesGpuPath": str(full_gpu_path),
        "captureTimeSeconds": capture_time_seconds(full_waterwaves_manifest),
        "paddedCropPixels": list(padded),
        "innerCropPixels": list(inner),
        "motion": motion,
        "motionProxy": {
            "displacedAlphaRatioProxy": round(displaced_motion_proxy, 8),
            "currentVsDisplacedAlphaAlphaRmse": alpha_delta,
        },
        "maskTextures": mask_infos,
        "materials": material_summary,
        "metrics": metrics,
        "outputs": outputs,
    }
    json_path = output_dir / "waterwaves-output-oracle.json"
    md_path = output_dir / "final-summary.md"
    json_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_summary_markdown(md_path, report)
    print(f"wrote {json_path}")
    print(f"wrote {md_path}")
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compare Arona layer 405 waterwaves boundaries against CPU variants.")
    parser.add_argument("--pulse-manifest", type=Path, required=True)
    parser.add_argument("--first-waterwaves-manifest", type=Path, required=True)
    parser.add_argument("--full-waterwaves-manifest", type=Path, required=True)
    parser.add_argument("--scene-root", type=Path, required=True)
    parser.add_argument("--layer-id", type=int, default=405)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_oracle_report(
        pulse_manifest=load_json(args.pulse_manifest),
        first_waterwaves_manifest=load_json(args.first_waterwaves_manifest),
        full_waterwaves_manifest=load_json(args.full_waterwaves_manifest),
        scene_root=args.scene_root,
        layer_id=args.layer_id,
        output_dir=args.output_dir,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
