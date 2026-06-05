#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ctypes
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
import sys
from typing import Any

import numpy as np
from PIL import Image


@dataclass(frozen=True)
class TexImage:
    width: int
    height: int
    pixels: np.ndarray
    format_id: int = 0
    flags: int = 0
    filter_mode: str = "bilinear"
    wrap_mode: str = "repeat"
    clamp_uvs: bool = False
    no_interpolation: bool = False


@dataclass(frozen=True)
class LutCandidate:
    quad_size: int
    flip_y: bool
    filter_mode: str
    color_mode: str


@dataclass(frozen=True)
class MaterialContext:
    blend_mode: int = 0
    multiply: float = 1.0
    translucent_compensation: float = 0.0
    clamp: bool = True


@dataclass(frozen=True)
class CandidateScore:
    candidate: LutCandidate
    rmse: float
    metrics: dict[str, Any]
    score_mode: str = "rmse"


class BinaryReader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def read(self, size: int) -> bytes:
        if self.pos + size > len(self.data):
            raise ValueError("unexpected end of data")
        result = self.data[self.pos:self.pos + size]
        self.pos += size
        return result

    def int32(self) -> int:
        value = int(np.frombuffer(self.read(4), dtype="<i4", count=1)[0])
        return value

    def uint32(self) -> int:
        value = int(np.frombuffer(self.read(4), dtype="<u4", count=1)[0])
        return value

    def sized_string(self) -> str:
        size = self.int32()
        if size < 0:
            raise ValueError(f"negative string size: {size}")
        return self.read(size).decode("utf-8")

    def version(self, expected: str) -> int:
        raw = self.read(9)
        prefix = raw[:4].decode("ascii")
        if prefix != expected:
            raise ValueError(f"expected {expected}, got {prefix}")
        if raw[8] != 0:
            raise ValueError(f"{expected} version is not null-terminated")
        return int(raw[4:8].decode("ascii"))


class PkgIndex:
    def __init__(self, data: bytes, entries: dict[str, tuple[int, int]]):
        self._data = data
        self._entries = entries

    @classmethod
    def from_bytes(cls, data: bytes) -> "PkgIndex":
        reader = BinaryReader(data)
        reader.sized_string()
        count = reader.int32()
        if count < 0:
            raise ValueError(f"negative package entry count: {count}")
        entries: list[tuple[str, int, int]] = []
        for _ in range(count):
            path = reader.sized_string()
            offset = reader.int32()
            length = reader.int32()
            if offset < 0 or length < 0:
                raise ValueError(f"invalid package entry: {path}")
            entries.append((path, offset, length))
        header_size = reader.pos
        return cls(data, {path: (header_size + offset, length) for path, offset, length in entries})

    @classmethod
    def from_path(cls, path: Path) -> "PkgIndex":
        return cls.from_bytes(path.read_bytes())

    def read(self, path: str) -> bytes:
        normalized = path.lstrip("/")
        if normalized not in self._entries:
            raise KeyError(f"package entry not found: {normalized}")
        offset, length = self._entries[normalized]
        return self._data[offset:offset + length]


_LZ4: ctypes.CDLL | None = None


def lz4_library() -> ctypes.CDLL:
    global _LZ4
    if _LZ4 is not None:
        return _LZ4
    for name in ("liblz4.so.1", "liblz4.so"):
        try:
            lib = ctypes.CDLL(name)
            lib.LZ4_decompress_safe.argtypes = [
                ctypes.c_char_p,
                ctypes.c_char_p,
                ctypes.c_int,
                ctypes.c_int,
            ]
            lib.LZ4_decompress_safe.restype = ctypes.c_int
            _LZ4 = lib
            return lib
        except OSError:
            continue
    raise RuntimeError("liblz4 is required to decode compressed WE textures")


def lz4_decompress_block(data: bytes, decompressed_size: int) -> bytes:
    if decompressed_size <= 0:
        raise ValueError(f"invalid LZ4 decompressed size: {decompressed_size}")
    source = ctypes.create_string_buffer(data)
    target = ctypes.create_string_buffer(decompressed_size)
    written = lz4_library().LZ4_decompress_safe(
        source,
        target,
        len(data),
        decompressed_size,
    )
    if written < 0:
        raise ValueError("LZ4 block decompression failed")
    return target.raw[:written]


def decode_tex_rgba8(data: bytes) -> TexImage:
    reader = BinaryReader(data)
    reader.version("TEXV")
    reader.version("TEXI")
    tex_format = reader.int32()
    flags = reader.uint32()
    header_width = reader.int32()
    header_height = reader.int32()
    reader.int32()
    reader.int32()
    reader.int32()
    texb_version = reader.version("TEXB")
    image_count = reader.int32()
    if image_count <= 0:
        raise ValueError("texture has no images")

    if texb_version == 3:
        reader.int32()

    if texb_version >= 4:
        reader.int32()
        reader.int32()
        tex_format = reader.int32()
        width = reader.int32()
        height = reader.int32()
        compressed = reader.int32() == 1
        decompressed_size = reader.int32()
        source_size = reader.int32()
    else:
        mip_count = reader.int32()
        if mip_count <= 0:
            raise ValueError("texture has no mipmaps")
        width = reader.int32()
        height = reader.int32()
        compressed = False
        decompressed_size = 0
        if texb_version > 1:
            compressed = reader.int32() == 1
            decompressed_size = reader.int32()
        source_size = reader.int32()

    if tex_format != 0:
        raise ValueError(f"only RGBA8 textures are supported by this lab, got format {tex_format}")
    if width <= 0 or height <= 0:
        raise ValueError(f"invalid texture dimensions: {width}x{height}")
    payload = reader.read(source_size)
    if compressed:
        payload = lz4_decompress_block(payload, decompressed_size)
    expected_size = width * height * 4
    if len(payload) < expected_size:
        raise ValueError(
            f"RGBA8 payload too small for {width}x{height}: {len(payload)} < {expected_size}"
        )
    pixels = np.frombuffer(payload[:expected_size], dtype=np.uint8).reshape((height, width, 4))
    no_interpolation = bool(flags & 1)
    clamp_uvs = bool(flags & 2)
    return TexImage(
        header_width or width,
        header_height or height,
        pixels.astype(np.float32) / 255.0,
        format_id=tex_format,
        flags=flags,
        filter_mode="nearest" if no_interpolation else "bilinear",
        wrap_mode="clamp" if clamp_uvs else "repeat",
        clamp_uvs=clamp_uvs,
        no_interpolation=no_interpolation,
    )


def srgb_to_linear(values: np.ndarray) -> np.ndarray:
    return np.where(values <= 0.04045, values / 12.92, ((values + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(values: np.ndarray) -> np.ndarray:
    return np.where(values <= 0.0031308, values * 12.92, 1.055 * np.power(values, 1.0 / 2.4) - 0.055)


def bilinear_sample(image: np.ndarray, uv: np.ndarray, filter_mode: str) -> np.ndarray:
    height, width, channels = image.shape
    coords = np.empty_like(uv, dtype=np.float32)
    coords[..., 0] = np.clip(uv[..., 0], 0.0, 1.0) * width - 0.5
    coords[..., 1] = np.clip(uv[..., 1], 0.0, 1.0) * height - 0.5
    if filter_mode == "nearest":
        xi = np.clip(np.rint(coords[..., 0]).astype(np.int32), 0, width - 1)
        yi = np.clip(np.rint(coords[..., 1]).astype(np.int32), 0, height - 1)
        return image[yi, xi]
    if filter_mode != "bilinear":
        raise ValueError(f"unknown filter mode: {filter_mode}")

    x0 = np.floor(coords[..., 0]).astype(np.int32)
    y0 = np.floor(coords[..., 1]).astype(np.int32)
    x1 = x0 + 1
    y1 = y0 + 1
    wx = coords[..., 0] - x0
    wy = coords[..., 1] - y0
    x0 = np.clip(x0, 0, width - 1)
    x1 = np.clip(x1, 0, width - 1)
    y0 = np.clip(y0, 0, height - 1)
    y1 = np.clip(y1, 0, height - 1)

    c00 = image[y0, x0]
    c10 = image[y0, x1]
    c01 = image[y1, x0]
    c11 = image[y1, x1]
    wx = wx[..., None]
    wy = wy[..., None]
    return (
        c00 * (1.0 - wx) * (1.0 - wy)
        + c10 * wx * (1.0 - wy)
        + c01 * (1.0 - wx) * wy
        + c11 * wx * wy
    )


def lut_positions(rgb: np.ndarray, quad_size: int, flip_y: bool) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    tiles_per_row = int(round(math.sqrt(quad_size)))
    if tiles_per_row * tiles_per_row != quad_size:
        raise ValueError(f"quad_size must be a square number, got {quad_size}")
    lut_size = quad_size * tiles_per_row
    texel_size = 1.0 / lut_size
    half_texel = 0.5 / lut_size
    tile_size = 1.0 / tiles_per_row

    blue = rgb[..., 2] * float(quad_size - 1)
    low = np.floor(blue)
    high = np.ceil(blue)
    fraction = blue - low

    def uv_for(slice_index: np.ndarray) -> np.ndarray:
        row = np.floor(slice_index / tiles_per_row)
        col = slice_index - row * tiles_per_row
        uv = np.empty(rgb.shape[:2] + (2,), dtype=np.float32)
        uv[..., 0] = col * tile_size + half_texel + ((tile_size - texel_size) * rgb[..., 0])
        uv[..., 1] = row * tile_size + half_texel + ((tile_size - texel_size) * rgb[..., 1])
        if flip_y:
            uv[..., 1] = 1.0 - uv[..., 1]
        return uv

    return uv_for(low), uv_for(high), fraction.astype(np.float32)


def blend_amount(context: MaterialContext, alpha: np.ndarray) -> np.ndarray:
    return context.multiply + context.translucent_compensation * (1.0 - alpha)


def apply_normal_blend(source: np.ndarray, sampled: np.ndarray, opacity: np.ndarray) -> np.ndarray:
    return source * (1.0 - opacity) + sampled * opacity


def apply_lut(
    source: np.ndarray,
    lut: np.ndarray,
    candidate: LutCandidate,
    context: MaterialContext | None = None,
) -> np.ndarray:
    context = context or MaterialContext()
    rgb = source[..., :3].astype(np.float32)
    if context.clamp:
        rgb = np.clip(rgb, 0.0, 1.0)
    alpha = source[..., 3:4].astype(np.float32) if source.shape[2] > 3 else np.ones(rgb.shape[:2] + (1,), dtype=np.float32)
    sample_lut = lut.astype(np.float32)
    if candidate.color_mode == "input-linear":
        rgb = srgb_to_linear(rgb)
    elif candidate.color_mode == "lut-linear-output-srgb":
        sample_lut = sample_lut.copy()
        sample_lut[..., :3] = srgb_to_linear(sample_lut[..., :3])
    elif candidate.color_mode != "shader":
        raise ValueError(f"unknown color mode: {candidate.color_mode}")

    pos1, pos2, amount = lut_positions(rgb, candidate.quad_size, candidate.flip_y)
    color1 = bilinear_sample(sample_lut, pos1, candidate.filter_mode)[..., :3]
    color2 = bilinear_sample(sample_lut, pos2, candidate.filter_mode)[..., :3]
    sampled = color1 * (1.0 - amount[..., None]) + color2 * amount[..., None]
    if candidate.color_mode == "lut-linear-output-srgb":
        sampled = linear_to_srgb(np.clip(sampled, 0.0, 1.0))
    opacity = blend_amount(context, alpha)
    if context.blend_mode == 0:
        sampled = apply_normal_blend(source[..., :3].astype(np.float32), sampled, opacity)
    return np.concatenate([np.clip(sampled, 0.0, 1.0), alpha], axis=2)


def rmse(left: np.ndarray, right: np.ndarray) -> float:
    diff = left.astype(np.float32) - right.astype(np.float32)
    return float(np.sqrt(np.mean(diff * diff)))


def visible_weights(expected: np.ndarray) -> np.ndarray:
    if expected.shape[2] > 3:
        return np.clip(expected[..., 3].astype(np.float32), 0.0, 1.0)
    return np.ones(expected.shape[:2], dtype=np.float32)


def weighted_rmse(left: np.ndarray, right: np.ndarray, weights: np.ndarray) -> float | None:
    total_weight = float(np.sum(weights))
    if total_weight <= 0.0:
        return None
    diff = left[..., :3].astype(np.float32) - right[..., :3].astype(np.float32)
    per_pixel = np.mean(diff * diff, axis=2)
    return float(np.sqrt(np.sum(per_pixel * weights) / total_weight))


def score_prediction(predicted: np.ndarray, expected: np.ndarray) -> dict[str, Any]:
    weights = visible_weights(expected)
    visible_mask = weights > 0.05
    opaque_mask = weights >= 0.95
    pixel_count = max(1, weights.size)
    return {
        "rmse": rmse(predicted[..., :3], expected[..., :3]),
        "alphaWeightedRmse": weighted_rmse(predicted, expected, weights),
        "opaqueRmse": weighted_rmse(predicted, expected, opaque_mask.astype(np.float32)),
        "visibleFraction": float(np.count_nonzero(visible_mask) / pixel_count),
        "opaqueFraction": float(np.count_nonzero(opaque_mask) / pixel_count),
    }


def score_value(metrics: dict[str, Any], score_mode: str) -> float:
    key = {
        "rmse": "rmse",
        "alpha-weighted": "alphaWeightedRmse",
        "opaque": "opaqueRmse",
    }.get(score_mode)
    if key is None:
        raise ValueError(f"unknown score mode: {score_mode}")
    value = metrics.get(key)
    if value is None:
        value = metrics["rmse"]
    return float(value)


def trusted_visible(metrics: dict[str, Any]) -> bool:
    return metrics.get("visibleFraction", 0.0) >= 0.25 and metrics.get("opaqueFraction", 0.0) >= 0.10


def candidate_summary(score: CandidateScore) -> dict[str, Any]:
    return {
        "rmse": score.rmse,
        "plainRmse": score.metrics["rmse"],
        "alphaWeightedRmse": score.metrics["alphaWeightedRmse"],
        "opaqueRmse": score.metrics["opaqueRmse"],
        "visibleFraction": score.metrics["visibleFraction"],
        "opaqueFraction": score.metrics["opaqueFraction"],
        **asdict(score.candidate),
    }


def default_candidates() -> list[LutCandidate]:
    return [
        LutCandidate(quad_size=quad_size, flip_y=flip_y, filter_mode=filter_mode, color_mode=color_mode)
        for quad_size in (16, 64)
        for flip_y in (False, True)
        for filter_mode in ("bilinear", "nearest")
        for color_mode in ("shader", "input-linear", "lut-linear-output-srgb")
    ]


def rank_lut_candidates(
    source: np.ndarray,
    expected: np.ndarray,
    lut: np.ndarray,
    context: MaterialContext | None = None,
    candidates: list[LutCandidate] | None = None,
    score_mode: str = "rmse",
) -> list[CandidateScore]:
    scores: list[CandidateScore] = []
    for candidate in candidates or default_candidates():
        predicted = apply_lut(source, lut, candidate, context)
        metrics = score_prediction(predicted, expected)
        scores.append(CandidateScore(candidate, score_value(metrics, score_mode), metrics, score_mode))
    return sorted(scores, key=lambda score: score.rmse)


def default_source() -> Path:
    return Path.home() / ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3228578419/scene.pkg"


def image_to_array(path: Path, size: tuple[int, int] | None = None) -> np.ndarray:
    image = Image.open(path).convert("RGBA")
    if size is not None and image.size != size:
        image = image.resize(size, Image.Resampling.BILINEAR)
    return np.asarray(image, dtype=np.float32) / 255.0


def array_to_image(pixels: np.ndarray) -> Image.Image:
    return Image.fromarray(np.clip(pixels * 255.0, 0, 255).astype(np.uint8), "RGBA")


def image_dimensions(path: Path) -> list[int]:
    with Image.open(path) as image:
        return [image.width, image.height]


def constrained_size(width: int, height: int, max_size: int) -> tuple[int, int]:
    scale = min(1.0, max_size / max(width, height))
    return (max(1, round(width * scale)), max(1, round(height * scale)))


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def find_variant(summary: dict[str, Any], name: str) -> dict[str, Any]:
    for variant in summary.get("variants", []):
        if variant.get("name") == name:
            return variant
    raise ValueError(f"variant not found in summary: {name}")


def layer_records(manifest: dict[str, Any], layer_id: int) -> list[dict[str, Any]]:
    return [
        record for record in manifest.get("captures", [])
        if record.get("layer", {}).get("layerId") == layer_id
    ]


def layer_name_from_records(records: list[dict[str, Any]]) -> str:
    for record in records:
        name = record.get("layer", {}).get("layerName")
        if name:
            return str(name)
    return "layer"


def layer_slug(layer_id: int, layer_name: str | None) -> str:
    safe_name = "".join(ch if ch.isalnum() else "_" for ch in str(layer_name or "layer"))
    safe_name = "_".join(part for part in safe_name.split("_") if part)
    return f"layer-{layer_id}-{safe_name or 'layer'}"


def stage_record(records: list[dict[str, Any]], stage: str) -> dict[str, Any]:
    for record in records:
        if record.get("stage") == stage:
            return record
    raise ValueError(f"stage not found for layer: {stage}")


def optional_stage_record(records: list[dict[str, Any]], stage: str) -> dict[str, Any] | None:
    for record in records:
        if record.get("stage") == stage:
            return record
    return None


def first_stage_with_prefix(
    records: list[dict[str, Any]],
    prefix: str,
    excluded_prefixes: tuple[str, ...] = (),
) -> dict[str, Any] | None:
    for record in records:
        stage = str(record.get("stage", ""))
        if stage.startswith(prefix) and not any(stage.startswith(excluded) for excluded in excluded_prefixes):
            return record
    return None


def output_stage_record(records: list[dict[str, Any]]) -> dict[str, Any]:
    for record in records:
        if str(record.get("stage", "")).startswith("material-output-local"):
            return record
    for record in records:
        if str(record.get("stage", "")).startswith("material-output"):
            return record
    return stage_record(records, "effect-output")


def has_output_stage(records: list[dict[str, Any]]) -> bool:
    try:
        output_stage_record(records)
        return True
    except ValueError:
        return False


def lut_material(record: dict[str, Any]) -> dict[str, Any]:
    for material in record.get("layer", {}).get("effectMaterials", []):
        if "lut_loader" in material.get("shader", ""):
            return material
    raise ValueError("no lut_loader material found in layer record")


def has_lut_material(records: list[dict[str, Any]]) -> bool:
    for record in records:
        try:
            lut_material(record)
            return True
        except ValueError:
            continue
    return False


def lut_layer_ids(manifest: dict[str, Any]) -> list[int]:
    seen: set[int] = set()
    result: list[int] = []
    for record in manifest.get("captures", []):
        layer_id = record.get("layer", {}).get("layerId")
        if not isinstance(layer_id, int) or layer_id in seen:
            continue
        records = layer_records(manifest, layer_id)
        if has_lut_material(records) and has_output_stage(records):
            seen.add(layer_id)
            result.append(layer_id)
    return result


def lut_texture_name(material: dict[str, Any]) -> str:
    for binding in material.get("textureBindings", []):
        if binding.get("slot") == 1 and binding.get("resolved"):
            return str(binding["resolved"])
    resolved = material.get("resolvedTextures", [])
    if len(resolved) > 1:
        return str(resolved[1])
    raise ValueError("no LUT texture binding found")


def first_float(values: Any, default: float) -> float:
    if isinstance(values, list) and values:
        try:
            return float(values[0])
        except (TypeError, ValueError):
            return default
    try:
        return float(values)
    except (TypeError, ValueError):
        return default


def material_context(material: dict[str, Any]) -> MaterialContext:
    combos = material.get("resolvedCombos", {})
    values = material.get("materialValues", {})
    try:
        blend_mode = int(combos.get("BLENDMODE", "0"))
    except (TypeError, ValueError):
        blend_mode = 0
    return MaterialContext(
        blend_mode=blend_mode,
        multiply=first_float(values.get("multiply1"), 1.0),
        translucent_compensation=first_float(values.get("tc"), 0.0),
        clamp=str(combos.get("CLAMP", "1")) != "0",
    )


def material_texture_path(texture_name: str) -> str:
    if texture_name.endswith(".tex"):
        texture_name = texture_name[:-4]
    if texture_name.startswith("materials/"):
        return texture_name + ".tex"
    return f"materials/{texture_name}.tex"


def load_lut_from_pkg(source: Path, texture_name: str) -> TexImage:
    index = PkgIndex.from_path(source)
    return decode_tex_rgba8(index.read(material_texture_path(texture_name)))


def rounded(value: float | None) -> float | None:
    if value is None:
        return None
    return round(float(value), 6)


def rounded_rgb(values: list[float] | np.ndarray | None) -> list[float] | None:
    if values is None:
        return None
    return [round(float(value), 6) for value in values]


def capture_dimensions(record: dict[str, Any] | None) -> list[int] | None:
    if record is None:
        return None
    info = record.get("renderTargetInfo", {})
    width = info.get("width")
    height = info.get("height")
    if isinstance(width, int) and isinstance(height, int) and width > 0 and height > 0:
        return [width, height]
    path = record.get("path")
    if not path:
        return None
    with Image.open(Path(path)) as image:
        return [image.width, image.height]


def alpha_bounds(pixels: np.ndarray) -> list[float] | None:
    if pixels.shape[2] <= 3:
        return [0.0, 0.0, 1.0, 1.0]
    mask = pixels[..., 3] > 0.05
    if not np.any(mask):
        return None
    ys, xs = np.nonzero(mask)
    height, width = mask.shape
    return [
        round(float(xs.min() / width), 6),
        round(float(ys.min() / height), 6),
        round(float((xs.max() + 1) / width), 6),
        round(float((ys.max() + 1) / height), 6),
    ]


def alpha_weighted_rgb_mean(pixels: np.ndarray) -> list[float]:
    rgb = pixels[..., :3].astype(np.float32)
    weights = visible_weights(pixels)
    total_weight = float(np.sum(weights))
    if total_weight <= 0.0:
        return rounded_rgb(np.mean(rgb.reshape((-1, 3)), axis=0)) or [0.0, 0.0, 0.0]
    mean = np.sum(rgb * weights[..., None], axis=(0, 1)) / total_weight
    return rounded_rgb(mean) or [0.0, 0.0, 0.0]


def capture_visibility(record: dict[str, Any] | None, max_size: int) -> dict[str, Any] | None:
    if record is None or not record.get("path"):
        return None
    dimensions = capture_dimensions(record)
    if dimensions is None:
        return None
    compare_size = constrained_size(dimensions[0], dimensions[1], max_size)
    pixels = image_to_array(Path(record["path"]), compare_size)
    metrics = score_prediction(pixels, pixels)
    return {
        "stage": record.get("stage"),
        "dimensions": dimensions,
        "visibleFraction": rounded(metrics["visibleFraction"]),
        "opaqueFraction": rounded(metrics["opaqueFraction"]),
        "meanRgb": alpha_weighted_rgb_mean(pixels),
        "alphaBounds": alpha_bounds(pixels),
    }


def compare_capture_records(
    left_record: dict[str, Any] | None,
    right_record: dict[str, Any] | None,
    max_size: int,
) -> dict[str, Any] | None:
    if left_record is None or right_record is None:
        return None
    right_dimensions = capture_dimensions(right_record)
    if right_dimensions is None:
        return None
    compare_size = constrained_size(right_dimensions[0], right_dimensions[1], max_size)
    left_pixels = image_to_array(Path(left_record["path"]), compare_size)
    right_pixels = image_to_array(Path(right_record["path"]), compare_size)
    metrics = score_prediction(left_pixels, right_pixels)
    left_mean = np.array(alpha_weighted_rgb_mean(left_pixels), dtype=np.float32)
    right_mean = np.array(alpha_weighted_rgb_mean(right_pixels), dtype=np.float32)
    return {
        "fromStage": left_record.get("stage"),
        "toStage": right_record.get("stage"),
        "compareSize": list(compare_size),
        "rmse": rounded(metrics["rmse"]),
        "alphaWeightedRmse": rounded(metrics["alphaWeightedRmse"]),
        "opaqueRmse": rounded(metrics["opaqueRmse"]),
        "visibleFraction": rounded(metrics["visibleFraction"]),
        "opaqueFraction": rounded(metrics["opaqueFraction"]),
        "meanRgbDelta": rounded_rgb(right_mean - left_mean),
    }


def compare_image_paths(left_path: Path, right_path: Path, max_size: int) -> dict[str, Any]:
    right_dimensions = image_dimensions(right_path)
    compare_size = constrained_size(right_dimensions[0], right_dimensions[1], max_size)
    left_pixels = image_to_array(left_path, compare_size)
    right_pixels = image_to_array(right_path, compare_size)
    metrics = score_prediction(left_pixels, right_pixels)
    left_mean = np.array(alpha_weighted_rgb_mean(left_pixels), dtype=np.float32)
    right_mean = np.array(alpha_weighted_rgb_mean(right_pixels), dtype=np.float32)
    return {
        "compareSize": list(compare_size),
        "rmse": rounded(metrics["rmse"]),
        "alphaWeightedRmse": rounded(metrics["alphaWeightedRmse"]),
        "opaqueRmse": rounded(metrics["opaqueRmse"]),
        "visibleFraction": rounded(metrics["visibleFraction"]),
        "opaqueFraction": rounded(metrics["opaqueFraction"]),
        "meanRgbDelta": rounded_rgb(right_mean - left_mean),
    }


def publish_diagnostics(records: list[dict[str, Any]]) -> dict[str, Any] | None:
    for record in records:
        layer = record.get("layer", {})
        publish = layer.get("publish")
        if not isinstance(publish, dict):
            continue
        result = dict(publish)
        try:
            material = lut_material(record)
        except ValueError:
            material = {}
        for key in (
            "authoredOutputRenderTarget",
            "resolvedOutputRenderTarget",
            "finalPublishedMaterial",
            "debugMaterialOutputSourceRenderTarget",
            "debugMaterialOutputCommandSource",
            "debugSourceFinalEffectOutput",
            "localMaterialOutputCaptureStage",
            "materialOutputCaptureStage",
            "materialOutputCopyAfterPos",
        ):
            if key in material:
                result[key] = material[key]
        return result
    return None


CROP_REGISTRATION_SCALES: tuple[float, ...] = (1.0, 0.875, 0.75, 2.0 / 3.0, 0.5)
CROP_REGISTRATION_OFFSETS: tuple[float, ...] = (0.0, -0.25, 0.25, -0.5, 0.5, -1.0, 1.0)


def bounded_crop_candidates(
    local_width: int,
    local_height: int,
    target_width: int,
    target_height: int,
) -> list[dict[str, Any]]:
    if local_width <= 0 or local_height <= 0 or target_width <= 0 or target_height <= 0:
        return []
    target_aspect = target_width / target_height
    local_aspect = local_width / local_height
    if local_aspect >= target_aspect:
        base_height = local_height
        base_width = max(1, round(base_height * target_aspect))
    else:
        base_width = local_width
        base_height = max(1, round(base_width / target_aspect))

    candidates: list[dict[str, Any]] = []
    seen: set[tuple[int, int, int, int]] = set()
    for scale in CROP_REGISTRATION_SCALES:
        crop_width = max(1, min(local_width, round(base_width * scale)))
        crop_height = max(1, round(crop_width / target_aspect))
        if crop_height > local_height:
            crop_height = local_height
            crop_width = max(1, round(crop_height * target_aspect))
        crop_width = min(crop_width, local_width)
        crop_height = min(crop_height, local_height)
        margin_x = local_width - crop_width
        margin_y = local_height - crop_height
        for offset_y in CROP_REGISTRATION_OFFSETS:
            for offset_x in CROP_REGISTRATION_OFFSETS:
                x = round((margin_x * 0.5) + (offset_x * margin_x * 0.5))
                y = round((margin_y * 0.5) + (offset_y * margin_y * 0.5))
                x = max(0, min(margin_x, x))
                y = max(0, min(margin_y, y))
                key = (x, y, crop_width, crop_height)
                if key in seen:
                    continue
                seen.add(key)
                candidates.append({
                    "cropPixels": [x, y, crop_width, crop_height],
                    "cropNorm": [
                        rounded(x / local_width),
                        rounded(y / local_height),
                        rounded((x + crop_width) / local_width),
                        rounded((y + crop_height) / local_height),
                    ],
                    "cropScale": rounded(crop_width / base_width),
                    "offsetX": offset_x,
                    "offsetY": offset_y,
                })
    return candidates


def registered_crop_compare_records(
    left_record: dict[str, Any] | None,
    right_record: dict[str, Any] | None,
    max_size: int,
) -> dict[str, Any] | None:
    if left_record is None or right_record is None:
        return None
    local_dimensions = capture_dimensions(left_record)
    target_dimensions = capture_dimensions(right_record)
    if local_dimensions is None or target_dimensions is None:
        return None
    compare_size = constrained_size(target_dimensions[0], target_dimensions[1], min(max_size, 256))
    target_pixels = image_to_array(Path(right_record["path"]), compare_size)
    with Image.open(Path(left_record["path"])).convert("RGBA") as local_image:
        local_width, local_height = local_image.size
        search_width, search_height = constrained_size(local_width, local_height, min(max_size, 512))
        search_image = local_image.resize((search_width, search_height), Image.Resampling.BILINEAR)

    best_candidate: dict[str, Any] | None = None
    best_pixels: np.ndarray | None = None
    best_metrics: dict[str, Any] | None = None
    best_score = math.inf
    for candidate in bounded_crop_candidates(local_dimensions[0], local_dimensions[1], target_dimensions[0], target_dimensions[1]):
        x, y, width, height = candidate["cropPixels"]
        sx = search_width / local_dimensions[0]
        sy = search_height / local_dimensions[1]
        box = (
            max(0, min(search_width - 1, round(x * sx))),
            max(0, min(search_height - 1, round(y * sy))),
            max(1, min(search_width, round((x + width) * sx))),
            max(1, min(search_height, round((y + height) * sy))),
        )
        if box[2] <= box[0] or box[3] <= box[1]:
            continue
        candidate_image = search_image.crop(box).resize(compare_size, Image.Resampling.BILINEAR)
        candidate_pixels = np.asarray(candidate_image, dtype=np.float32) / 255.0
        metrics = score_prediction(candidate_pixels, target_pixels)
        score = score_value(metrics, "alpha-weighted")
        score_key = (score, metrics["rmse"])
        best_key = (
            best_score,
            best_metrics["rmse"] if best_metrics is not None else math.inf,
        )
        if score_key < best_key:
            best_candidate = candidate
            best_pixels = candidate_pixels
            best_metrics = metrics
            best_score = score

    if best_candidate is None or best_pixels is None or best_metrics is None:
        return None
    naive = compare_capture_records(left_record, right_record, max_size)
    left_mean = np.array(alpha_weighted_rgb_mean(best_pixels), dtype=np.float32)
    right_mean = np.array(alpha_weighted_rgb_mean(target_pixels), dtype=np.float32)
    naive_rmse = float(naive.get("rmse") or 0.0) if naive else None
    rmse_value = float(best_metrics["rmse"])
    improvement = None if naive_rmse is None else max(0.0, naive_rmse - rmse_value)
    result = {
        "fromStage": left_record.get("stage"),
        "toStage": right_record.get("stage"),
        "metric": "bounded-crop-alpha-weighted-rmse",
        "compareSize": list(compare_size),
        "localDimensions": local_dimensions,
        "targetDimensions": target_dimensions,
        "candidateCount": len(bounded_crop_candidates(local_dimensions[0], local_dimensions[1], target_dimensions[0], target_dimensions[1])),
        "rmse": rounded(best_metrics["rmse"]),
        "alphaWeightedRmse": rounded(best_metrics["alphaWeightedRmse"]),
        "opaqueRmse": rounded(best_metrics["opaqueRmse"]),
        "visibleFraction": rounded(best_metrics["visibleFraction"]),
        "opaqueFraction": rounded(best_metrics["opaqueFraction"]),
        "meanRgbDelta": rounded_rgb(right_mean - left_mean),
        "naiveRmse": rounded(naive_rmse),
        "improvement": rounded(improvement),
    }
    result.update(best_candidate)
    return result


def variant_screen_drift(variant: dict[str, Any]) -> dict[str, Any]:
    metrics = variant.get("metrics", {})
    reference_mean = metrics.get("referenceMeanRgb")
    yakkai_mean = metrics.get("yakkaiMeanRgb")
    mean_delta = None
    if (
        isinstance(reference_mean, list)
        and isinstance(yakkai_mean, list)
        and len(reference_mean) >= 3
        and len(yakkai_mean) >= 3
    ):
        mean_delta = [
            round(float(yakkai_mean[index]) - float(reference_mean[index]), 6)
            for index in range(3)
        ]
    return {
        "rmse": rounded(metrics.get("registeredRmse", metrics.get("rmse"))),
        "structureRmse": rounded(metrics.get("registrationStructureRmse")),
        "referenceMeanRgb": rounded_rgb(reference_mean),
        "yakkaiMeanRgb": rounded_rgb(yakkai_mean),
        "meanRgbDelta": mean_delta,
    }


def classify_publish_drift(
    local_record: dict[str, Any] | None,
    final_record: dict[str, Any] | None,
    published_record: dict[str, Any] | None,
    local_to_final: dict[str, Any] | None,
    registered_local_to_final: dict[str, Any] | None,
    final_to_published: dict[str, Any] | None,
    screen_drift: dict[str, Any],
    diagnostics: dict[str, Any] | None,
) -> str:
    if local_record is None:
        return "missing-local-material-output"
    if final_record is None and published_record is None:
        return "missing-final-publish-output"
    local_stage_drift = local_to_final is not None and float(local_to_final.get("rmse") or 0.0) > 0.08
    registered_transform_match = (
        registered_local_to_final is not None
        and float(registered_local_to_final.get("rmse") or 0.0) <= 0.05
        and float(registered_local_to_final.get("improvement") or 0.0) > 0.05
    )
    final_stage_drift = final_to_published is not None and float(final_to_published.get("rmse") or 0.0) > 0.02
    post_frame_publish = (
        diagnostics is not None
        and diagnostics.get("finalPublishCaptureTiming") == "post-frame-render-target-dump"
    )
    if registered_transform_match and final_stage_drift:
        if post_frame_publish:
            return "registered-transform-and-post-frame-composite-delta"
        return "registered-transform-and-final-stage-drift"
    if registered_transform_match:
        return "registered-transform-only-drift"
    if local_stage_drift and final_stage_drift:
        if post_frame_publish:
            return "publish-transform-and-post-frame-composite-delta"
        return "publish-transform-and-final-stage-drift"
    if final_stage_drift:
        if post_frame_publish:
            return "post-frame-composite-delta"
        return "final-publish-stage-drift"
    if local_stage_drift:
        return "publish-transform-or-color-drift"
    if float(screen_drift.get("rmse") or 0.0) > 0.08:
        return "downstream-or-missing-effects"
    return "publish-path-consistent"


def publish_drift_report(records: list[dict[str, Any]], variant: dict[str, Any], max_size: int) -> dict[str, Any]:
    local_record = first_stage_with_prefix(records, "material-output-local")
    final_record = first_stage_with_prefix(records, "material-output", ("material-output-local",))
    published_record = optional_stage_record(records, "final-publish")
    local_to_final = compare_capture_records(local_record, final_record or published_record, max_size)
    registered_local_to_final = registered_crop_compare_records(local_record, final_record or published_record, max_size)
    final_to_published = compare_capture_records(final_record, published_record, max_size)
    screen_drift = variant_screen_drift(variant)
    diagnostics = publish_diagnostics(records)
    classification = classify_publish_drift(
        local_record,
        final_record,
        published_record,
        local_to_final,
        registered_local_to_final,
        final_to_published,
        screen_drift,
        diagnostics,
    )
    return {
        "classification": classification,
        "localStage": local_record.get("stage") if local_record else None,
        "finalStage": final_record.get("stage") if final_record else None,
        "publishedStage": published_record.get("stage") if published_record else None,
        "localDimensions": capture_dimensions(local_record),
        "finalDimensions": capture_dimensions(final_record),
        "publishedDimensions": capture_dimensions(published_record),
        "localVisibility": capture_visibility(local_record, max_size),
        "finalVisibility": capture_visibility(final_record, max_size),
        "publishedVisibility": capture_visibility(published_record, max_size),
        "localToFinal": local_to_final,
        "registeredLocalToFinal": registered_local_to_final,
        "finalToPublished": final_to_published,
        "variantScreenDrift": screen_drift,
        "publishDiagnostics": diagnostics,
    }


def variant_image_path(variant: dict[str, Any], *keys: str) -> Path:
    for key in keys:
        value = variant.get(key)
        if value:
            return Path(str(value))
    raise ValueError(f"variant has none of the required image paths: {', '.join(keys)}")


def capture_sort_index(record: dict[str, Any], fallback_index: int) -> int:
    capture_index = record.get("captureIndex")
    if isinstance(capture_index, int):
        return capture_index
    return fallback_index


def is_default_frame_lut_snapshot(record: dict[str, Any], final_dimensions: list[int]) -> bool:
    stage = str(record.get("stage", ""))
    if not stage.startswith("material-output-") or stage.startswith("material-output-local-"):
        return False
    if not record.get("path"):
        return False
    try:
        material = lut_material(record)
    except ValueError:
        return False
    dimensions = capture_dimensions(record)
    if dimensions != final_dimensions:
        return False
    layer = record.get("layer", {})
    publish = layer.get("publish", {}) if isinstance(layer.get("publish"), dict) else {}
    candidate_targets = {
        record.get("renderTarget"),
        material.get("debugMaterialOutputSourceRenderTarget"),
        material.get("resolvedOutputRenderTarget"),
        publish.get("effectOutputSourceTarget"),
        publish.get("finalPublishRenderTarget"),
    }
    return "_rt_default" in candidate_targets


def default_frame_snapshots(manifest: dict[str, Any], final_dimensions: list[int]) -> list[tuple[int, int, dict[str, Any]]]:
    snapshots: list[tuple[int, int, dict[str, Any]]] = []
    for fallback_index, record in enumerate(manifest.get("captures", [])):
        if is_default_frame_lut_snapshot(record, final_dimensions):
            snapshots.append((capture_sort_index(record, fallback_index), fallback_index, record))
    return sorted(snapshots, key=lambda item: (item[0], item[1]))


def progression_step_summary(snapshot: dict[str, Any]) -> dict[str, Any] | None:
    if snapshot.get("deltaFromPreviousRmse") is None:
        return None
    return {
        "fromCaptureIndex": snapshot.get("previousCaptureIndex"),
        "fromLayerId": snapshot.get("previousLayerId"),
        "toCaptureIndex": snapshot.get("captureIndex"),
        "toLayerId": snapshot.get("layerId"),
        "toLayerName": snapshot.get("layerName"),
        "stage": snapshot.get("stage"),
        "deltaFromPreviousRmse": snapshot.get("deltaFromPreviousRmse"),
        "referenceRmseDeltaFromPrevious": snapshot.get("referenceRmseDeltaFromPrevious"),
        "referenceRmse": snapshot.get("referenceRmse"),
        "finalRmse": snapshot.get("finalRmse"),
    }


def compare_default_frame_progression(
    summary_path: Path,
    variant_name: str,
    output_dir: Path,
    max_size: int,
) -> dict[str, Any]:
    summary = load_json(summary_path)
    variant = find_variant(summary, variant_name)
    if not variant.get("effectManifest"):
        raise ValueError(f"variant has no effect manifest: {variant_name}")
    manifest_path = Path(variant["effectManifest"])
    manifest = load_json(manifest_path)
    reference_path = variant_image_path(variant, "normalizedReference", "reference")
    final_path = variant_image_path(variant, "yakkai", "registeredYakkai")
    final_dimensions = image_dimensions(final_path)
    snapshots = default_frame_snapshots(manifest, final_dimensions)
    final_reference = compare_image_paths(final_path, reference_path, max_size)

    snapshot_results: list[dict[str, Any]] = []
    previous_record: dict[str, Any] | None = None
    previous_snapshot: dict[str, Any] | None = None
    for sort_index, fallback_index, record in snapshots:
        layer = record.get("layer", {})
        path = Path(str(record["path"]))
        reference_compare = compare_image_paths(path, reference_path, max_size)
        final_compare = compare_image_paths(path, final_path, max_size)
        step_compare = (
            compare_image_paths(Path(str(previous_record["path"])), path, max_size)
            if previous_record is not None
            else None
        )
        reference_delta = None
        if previous_snapshot is not None:
            reference_delta = float(reference_compare["rmse"]) - float(previous_snapshot["referenceRmse"])
        diagnostics = publish_diagnostics([record])
        snapshot_result = {
            "captureIndex": record.get("captureIndex", fallback_index),
            "sortIndex": sort_index,
            "manifestOrder": fallback_index,
            "layerId": layer.get("layerId"),
            "layerName": layer.get("layerName"),
            "stage": record.get("stage"),
            "path": str(path),
            "renderTarget": record.get("renderTarget"),
            "dimensions": capture_dimensions(record),
            "referenceRmse": reference_compare["rmse"],
            "referenceAlphaWeightedRmse": reference_compare["alphaWeightedRmse"],
            "finalRmse": final_compare["rmse"],
            "deltaFromPreviousRmse": None if step_compare is None else step_compare["rmse"],
            "referenceRmseDeltaFromPrevious": rounded(reference_delta),
            "previousCaptureIndex": None if previous_snapshot is None else previous_snapshot["captureIndex"],
            "previousLayerId": None if previous_snapshot is None else previous_snapshot["layerId"],
            "compareSize": reference_compare["compareSize"],
            "publishDiagnostics": diagnostics,
        }
        snapshot_results.append(snapshot_result)
        previous_record = record
        previous_snapshot = snapshot_result

    step_summaries = [
        summary for summary in (progression_step_summary(snapshot) for snapshot in snapshot_results)
        if summary is not None
    ]
    improvements = [
        summary for summary in step_summaries
        if float(summary.get("referenceRmseDeltaFromPrevious") or 0.0) < 0.0
    ]
    regressions = [
        summary for summary in step_summaries
        if float(summary.get("referenceRmseDeltaFromPrevious") or 0.0) > 0.0
    ]
    result = {
        "summary": str(summary_path),
        "variant": variant_name,
        "effectManifest": str(manifest_path),
        "reference": str(reference_path),
        "finalFrame": str(final_path),
        "finalDimensions": final_dimensions,
        "finalReferenceRmse": final_reference["rmse"],
        "snapshotCount": len(snapshot_results),
        "skippedCaptureCount": len(manifest.get("captures", [])) - len(snapshot_results),
        "largestStepDeltas": sorted(
            step_summaries,
            key=lambda item: float(item.get("deltaFromPreviousRmse") or 0.0),
            reverse=True,
        )[:10],
        "largestReferenceImprovements": sorted(
            improvements,
            key=lambda item: float(item.get("referenceRmseDeltaFromPrevious") or 0.0),
        )[:10],
        "largestReferenceRegressions": sorted(
            regressions,
            key=lambda item: float(item.get("referenceRmseDeltaFromPrevious") or 0.0),
            reverse=True,
        )[:10],
        "snapshots": snapshot_results,
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "default-frame-progression.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (output_dir / "default-frame-progression.md").write_text(
        format_default_frame_progression_markdown(result),
        encoding="utf-8",
    )
    return result


def classify_post_lut_drift(delta: float | None) -> str:
    if delta is None:
        return "missing-screen-lut-snapshots"
    if delta > 0.02:
        return "final-frame-regressed-after-lut"
    if delta < -0.02:
        return "final-frame-improved-after-lut"
    return "final-frame-similar-after-lut"


def capture_timeline_after(
    manifest: dict[str, Any],
    sort_index: int,
    manifest_order: int,
    final_dimensions: list[int],
    reference_path: Path,
    final_path: Path,
    last_lut_path: Path,
    max_size: int,
) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    ordered_records = sorted(
        (
            (capture_sort_index(record, fallback_index), fallback_index, record)
            for fallback_index, record in enumerate(manifest.get("captures", []))
        ),
        key=lambda item: (item[0], item[1]),
    )
    for record_sort_index, fallback_index, record in ordered_records:
        if (record_sort_index, fallback_index) <= (sort_index, manifest_order):
            continue
        path_value = record.get("path")
        if not path_value:
            continue
        layer = record.get("layer", {})
        path = Path(str(path_value))
        dimensions = capture_dimensions(record)
        item: dict[str, Any] = {
            "captureIndex": record.get("captureIndex", fallback_index),
            "sortIndex": record_sort_index,
            "manifestOrder": fallback_index,
            "stage": record.get("stage"),
            "layerId": layer.get("layerId"),
            "layerName": layer.get("layerName"),
            "layerAlpha": layer.get("alpha"),
            "materialUserAlpha": material_user_alpha_values(layer),
            "renderTarget": record.get("renderTarget"),
            "dimensions": dimensions,
            "path": str(path),
        }
        if dimensions == final_dimensions:
            item["referenceRmse"] = compare_image_paths(path, reference_path, max_size)["rmse"]
            item["finalRmse"] = compare_image_paths(path, final_path, max_size)["rmse"]
            item["lastLutRmse"] = compare_image_paths(last_lut_path, path, max_size)["rmse"]
        result.append(item)
    return result


def numeric_values(value: Any) -> list[float]:
    if isinstance(value, bool):
        return []
    if isinstance(value, (int, float)):
        return [float(value)]
    if isinstance(value, list):
        result: list[float] = []
        for item in value:
            result.extend(numeric_values(item))
        return result
    return []


def material_user_alpha_values(layer: dict[str, Any]) -> list[float]:
    values: list[float] = []
    for material in layer.get("effectMaterials", []):
        if not isinstance(material, dict):
            continue
        consts = material.get("resolvedConstValues", {})
        if not isinstance(consts, dict):
            continue
        values.extend(numeric_values(consts.get("g_UserAlpha")))
    return values


def region_bounds(width: int, height: int) -> list[dict[str, Any]]:
    third_w = max(1, width // 3)
    third_h = max(1, height // 3)
    two_thirds_w = max(third_w + 1, (width * 2) // 3)
    two_thirds_h = max(third_h + 1, (height * 2) // 3)
    two_thirds_w = min(width, two_thirds_w)
    two_thirds_h = min(height, two_thirds_h)
    return [
        {"region": "top", "bounds": [0, 0, width, third_h]},
        {"region": "middle", "bounds": [0, third_h, width, two_thirds_h]},
        {"region": "bottom", "bounds": [0, two_thirds_h, width, height]},
        {"region": "left", "bounds": [0, 0, third_w, height]},
        {"region": "center", "bounds": [third_w, 0, two_thirds_w, height]},
        {"region": "right", "bounds": [two_thirds_w, 0, width, height]},
    ]


def region_score(left: np.ndarray, right: np.ndarray, bounds: list[int]) -> float:
    x1, y1, x2, y2 = bounds
    if x2 <= x1 or y2 <= y1:
        return 0.0
    return score_prediction(left[y1:y2, x1:x2, :], right[y1:y2, x1:x2, :])["rmse"]


def post_lut_region_drift(
    last_lut_path: Path,
    final_path: Path,
    reference_path: Path,
    final_dimensions: list[int],
    max_size: int,
) -> list[dict[str, Any]]:
    compare_size = constrained_size(final_dimensions[0], final_dimensions[1], max_size)
    last_lut = image_to_array(last_lut_path, compare_size)
    final = image_to_array(final_path, compare_size)
    reference = image_to_array(reference_path, compare_size)
    result: list[dict[str, Any]] = []
    for region in region_bounds(compare_size[0], compare_size[1]):
        bounds = region["bounds"]
        last_lut_reference = region_score(last_lut, reference, bounds)
        final_reference = region_score(final, reference, bounds)
        last_lut_to_final = region_score(last_lut, final, bounds)
        result.append({
            "region": region["region"],
            "bounds": bounds,
            "lastLutReferenceRmse": rounded(last_lut_reference),
            "finalReferenceRmse": rounded(final_reference),
            "lastLutToFinalRmse": rounded(last_lut_to_final),
            "downstreamReferenceRmseDelta": rounded(final_reference - last_lut_reference),
        })
    return sorted(
        result,
        key=lambda item: float(item.get("downstreamReferenceRmseDelta") or 0.0),
        reverse=True,
    )


def classify_full_frame_attribution(
    full_frame_capture_count: int,
    active_steps: list[dict[str, Any]],
    disabled_captures: list[dict[str, Any]],
) -> str:
    if full_frame_capture_count == 0:
        return "no-post-lut-full-frame-captures"
    if not active_steps and disabled_captures:
        return "disabled-post-lut-captures-only"
    if not active_steps:
        return "no-active-post-lut-full-frame-captures"
    largest_step = max(float(step.get("previousRmse") or 0.0) for step in active_steps)
    if largest_step > 0.02:
        return "active-post-lut-full-frame-transition"
    return "post-lut-full-frame-captures-similar"


def is_output_capture_stage(stage: Any) -> bool:
    stage_text = str(stage or "")
    return (
        stage_text in {"effect-output", "final-publish"} or
        stage_text.startswith("material-output")
    )


def is_default_rt_boundary_stage(stage: Any) -> bool:
    return str(stage or "") in {"default-before-effect", "default-after-effect"}


def empty_default_rt_boundary_attribution(classification: str) -> dict[str, Any]:
    return {
        "classification": classification,
        "boundaryCaptureCount": 0,
        "firstTransition": None,
        "largestTransitions": [],
        "steps": [],
    }


def classify_default_rt_boundary_attribution(
    boundary_capture_count: int,
    steps: list[dict[str, Any]],
) -> str:
    if boundary_capture_count == 0:
        return "no-post-lut-default-rt-boundary-captures"
    if any(float(step.get("previousRmse") or 0.0) > 0.02 for step in steps):
        return "default-rt-boundary-transition-detected"
    return "default-rt-boundaries-similar"


def post_lut_default_rt_boundary_attribution(
    post_lut_captures: list[dict[str, Any]],
    last_lut_snapshot: dict[str, Any] | None,
    last_lut_reference_rmse: float | None,
    reference_path: Path,
    final_dimensions: list[int],
    max_size: int,
) -> dict[str, Any]:
    if last_lut_snapshot is None:
        return empty_default_rt_boundary_attribution("missing-screen-lut-snapshots")

    steps: list[dict[str, Any]] = []
    previous_path = Path(str(last_lut_snapshot["path"]))
    previous_reference = float(last_lut_reference_rmse or 0.0)
    previous_capture_index = last_lut_snapshot.get("captureIndex")
    previous_layer_id = last_lut_snapshot.get("layerId")

    for capture in post_lut_captures:
        if (
            capture.get("dimensions") != final_dimensions or
            "referenceRmse" not in capture or
            not is_default_rt_boundary_stage(capture.get("stage"))
        ):
            continue

        current_path = Path(str(capture["path"]))
        current_reference = float(capture.get("referenceRmse") or 0.0)
        previous_compare = compare_image_paths(previous_path, current_path, max_size)
        step = {
            "captureIndex": capture.get("captureIndex"),
            "stage": capture.get("stage"),
            "layerId": capture.get("layerId"),
            "layerName": capture.get("layerName"),
            "previousCaptureIndex": previous_capture_index,
            "previousLayerId": previous_layer_id,
            "referenceRmse": capture.get("referenceRmse"),
            "referenceRmseDeltaFromPrevious": rounded(current_reference - previous_reference),
            "previousRmse": previous_compare["rmse"],
            "finalRmse": capture.get("finalRmse"),
            "lastLutRmse": capture.get("lastLutRmse"),
            "renderTarget": capture.get("renderTarget"),
            "path": capture.get("path"),
            "disabledReason": disabled_zero_alpha_reason(capture),
            "regionDeltas": region_delta_from_previous(
                previous_path,
                current_path,
                reference_path,
                final_dimensions,
                max_size,
            ),
        }
        steps.append(step)
        previous_path = current_path
        previous_reference = current_reference
        previous_capture_index = capture.get("captureIndex")
        previous_layer_id = capture.get("layerId")

    largest_transitions = sorted(
        steps,
        key=lambda item: float(item.get("previousRmse") or 0.0),
        reverse=True,
    )
    first_transition = next(
        (step for step in steps if float(step.get("previousRmse") or 0.0) > 0.02),
        None,
    )
    return {
        "classification": classify_default_rt_boundary_attribution(len(steps), steps),
        "boundaryCaptureCount": len(steps),
        "firstTransition": first_transition,
        "largestTransitions": largest_transitions,
        "steps": steps,
    }


def post_lut_full_frame_attribution(
    post_lut_captures: list[dict[str, Any]],
    last_lut_snapshot: dict[str, Any] | None,
    last_lut_reference_rmse: float | None,
    reference_path: Path,
    final_dimensions: list[int],
    max_size: int,
) -> dict[str, Any]:
    if last_lut_snapshot is None:
        return {
            "classification": "missing-screen-lut-snapshots",
            "fullFrameCaptureCount": 0,
            "activeCaptureCount": 0,
            "disabledCaptureCount": 0,
            "firstActiveStep": None,
            "largestSteps": [],
            "steps": [],
            "disabledCaptures": [],
        }

    full_frame_count = 0
    disabled_captures: list[dict[str, Any]] = []
    active_steps: list[dict[str, Any]] = []
    previous_path = Path(str(last_lut_snapshot["path"]))
    previous_reference = float(last_lut_reference_rmse or 0.0)
    previous_capture_index = last_lut_snapshot.get("captureIndex")
    previous_layer_id = last_lut_snapshot.get("layerId")

    for capture in post_lut_captures:
        if (
            capture.get("dimensions") != final_dimensions or
            "referenceRmse" not in capture or
            not is_output_capture_stage(capture.get("stage"))
        ):
            continue
        full_frame_count += 1
        disabled_reason = disabled_zero_alpha_reason(capture)
        if disabled_reason:
            item = dict(capture)
            item["disabledReason"] = disabled_reason
            disabled_captures.append(item)
            continue

        current_path = Path(str(capture["path"]))
        current_reference = float(capture.get("referenceRmse") or 0.0)
        previous_compare = compare_image_paths(previous_path, current_path, max_size)
        step = {
            "captureIndex": capture.get("captureIndex"),
            "stage": capture.get("stage"),
            "layerId": capture.get("layerId"),
            "layerName": capture.get("layerName"),
            "previousCaptureIndex": previous_capture_index,
            "previousLayerId": previous_layer_id,
            "referenceRmse": capture.get("referenceRmse"),
            "referenceRmseDeltaFromPrevious": rounded(current_reference - previous_reference),
            "previousRmse": previous_compare["rmse"],
            "finalRmse": capture.get("finalRmse"),
            "lastLutRmse": capture.get("lastLutRmse"),
            "renderTarget": capture.get("renderTarget"),
            "path": capture.get("path"),
            "regionDeltas": region_delta_from_previous(
                previous_path,
                current_path,
                reference_path,
                final_dimensions,
                max_size,
            ),
        }
        active_steps.append(step)
        previous_path = current_path
        previous_reference = current_reference
        previous_capture_index = capture.get("captureIndex")
        previous_layer_id = capture.get("layerId")

    largest_steps = sorted(
        active_steps,
        key=lambda item: float(item.get("previousRmse") or 0.0),
        reverse=True,
    )
    return {
        "classification": classify_full_frame_attribution(
            full_frame_count,
            active_steps,
            disabled_captures,
        ),
        "fullFrameCaptureCount": full_frame_count,
        "activeCaptureCount": len(active_steps),
        "disabledCaptureCount": len(disabled_captures),
        "firstActiveStep": active_steps[0] if active_steps else None,
        "largestSteps": largest_steps,
        "steps": active_steps,
        "disabledCaptures": disabled_captures,
    }


def compare_post_lut_drift(
    summary_path: Path,
    variant_name: str,
    output_dir: Path,
    max_size: int,
) -> dict[str, Any]:
    summary = load_json(summary_path)
    variant = find_variant(summary, variant_name)
    if not variant.get("effectManifest"):
        raise ValueError(f"variant has no effect manifest: {variant_name}")
    manifest_path = Path(variant["effectManifest"])
    manifest = load_json(manifest_path)
    reference_path = variant_image_path(variant, "normalizedReference", "reference")
    final_path = variant_image_path(variant, "yakkai", "registeredYakkai")
    final_dimensions = image_dimensions(final_path)
    final_reference = compare_image_paths(final_path, reference_path, max_size)
    snapshots = default_frame_snapshots(manifest, final_dimensions)

    if not snapshots:
        result = {
            "summary": str(summary_path),
            "variant": variant_name,
            "effectManifest": str(manifest_path),
            "reference": str(reference_path),
            "finalFrame": str(final_path),
            "finalDimensions": final_dimensions,
            "classification": classify_post_lut_drift(None),
            "snapshotCount": 0,
            "finalReferenceRmse": final_reference["rmse"],
            "lastLutSnapshot": None,
            "lastLutReferenceRmse": None,
            "lastLutToFinal": None,
            "downstreamReferenceRmseDelta": None,
            "postLutCaptures": [],
            "fullFrameAttribution": {
                "classification": "missing-screen-lut-snapshots",
                "fullFrameCaptureCount": 0,
                "activeCaptureCount": 0,
                "disabledCaptureCount": 0,
                "firstActiveStep": None,
                "largestSteps": [],
                "steps": [],
                "disabledCaptures": [],
            },
            "defaultRtBoundaryAttribution": empty_default_rt_boundary_attribution(
                "missing-screen-lut-snapshots"
            ),
            "protectedPuppetDiagnostics": protected_puppet_diagnostics(manifest),
            "regionDrift": [],
        }
    else:
        sort_index, manifest_order, last_record = snapshots[-1]
        layer = last_record.get("layer", {})
        last_lut_path = Path(str(last_record["path"]))
        last_lut_reference = compare_image_paths(last_lut_path, reference_path, max_size)
        last_lut_to_final = compare_image_paths(last_lut_path, final_path, max_size)
        downstream_delta = float(final_reference["rmse"]) - float(last_lut_reference["rmse"])
        last_lut_snapshot = {
            "captureIndex": last_record.get("captureIndex", manifest_order),
            "sortIndex": sort_index,
            "manifestOrder": manifest_order,
            "layerId": layer.get("layerId"),
            "layerName": layer.get("layerName"),
            "stage": last_record.get("stage"),
            "path": str(last_lut_path),
            "renderTarget": last_record.get("renderTarget"),
            "dimensions": capture_dimensions(last_record),
            "publishDiagnostics": publish_diagnostics([last_record]),
        }
        post_lut_captures = capture_timeline_after(
            manifest,
            sort_index,
            manifest_order,
            final_dimensions,
            reference_path,
            final_path,
            last_lut_path,
            max_size,
        )
        result = {
            "summary": str(summary_path),
            "variant": variant_name,
            "effectManifest": str(manifest_path),
            "reference": str(reference_path),
            "finalFrame": str(final_path),
            "finalDimensions": final_dimensions,
            "classification": classify_post_lut_drift(downstream_delta),
            "snapshotCount": len(snapshots),
            "finalReferenceRmse": final_reference["rmse"],
            "lastLutSnapshot": last_lut_snapshot,
            "lastLutReferenceRmse": last_lut_reference["rmse"],
            "lastLutToFinal": last_lut_to_final,
            "downstreamReferenceRmseDelta": rounded(downstream_delta),
            "postLutCaptures": post_lut_captures,
            "fullFrameAttribution": post_lut_full_frame_attribution(
                post_lut_captures,
                last_lut_snapshot,
                float(last_lut_reference["rmse"]),
                reference_path,
                final_dimensions,
                max_size,
            ),
            "defaultRtBoundaryAttribution": post_lut_default_rt_boundary_attribution(
                post_lut_captures,
                last_lut_snapshot,
                float(last_lut_reference["rmse"]),
                reference_path,
                final_dimensions,
                max_size,
            ),
            "protectedPuppetDiagnostics": protected_puppet_diagnostics(manifest),
            "regionDrift": post_lut_region_drift(
                last_lut_path,
                final_path,
                reference_path,
                final_dimensions,
                max_size,
            ),
        }

    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "post-lut-drift.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (output_dir / "post-lut-drift.md").write_text(
        format_post_lut_drift_markdown(result),
        encoding="utf-8",
    )
    return result


def first_dict(*values: Any) -> dict[str, Any]:
    for value in values:
        if isinstance(value, dict):
            return value
    return {}


def string_list(value: Any) -> list[str]:
    if not isinstance(value, list):
        return []
    return [str(item) for item in value if isinstance(item, str)]


def protected_puppet_diagnostics(manifest: dict[str, Any]) -> dict[str, Any]:
    records = manifest.get("protectedPuppetDiagnostics", [])
    if not isinstance(records, list):
        records = []

    layers: list[dict[str, Any]] = []
    chain_counts: dict[str, int] = {}
    for record in records:
        if not isinstance(record, dict):
            continue
        nested_layer = first_dict(record.get("layer"))
        publish = first_dict(record.get("publish"), nested_layer.get("publish"))
        alpha_evidence = first_dict(record.get("alphaEvidence"))
        routing = first_dict(record.get("routing"))
        layer_id = record.get("layerId", nested_layer.get("layerId"))
        layer_name = record.get("layerName", nested_layer.get("layerName"))
        chain_shape = str(
            record.get("candidateChainShape")
            or nested_layer.get("candidateChainShape")
            or "unknown"
        )
        chain_counts[chain_shape] = chain_counts.get(chain_shape, 0) + 1
        layers.append({
            "layerId": layer_id,
            "layerName": layer_name,
            "class": (
                record.get("candidateEffectClass")
                or nested_layer.get("candidateEffectClass")
                or "unknown"
            ),
            "diagnosticKind": record.get("diagnosticKind"),
            "captureMode": record.get("captureMode"),
            "alpha": record.get("alpha", nested_layer.get("alpha")),
            "alphaEvidence": alpha_evidence,
            "effectOrder": string_list(record.get("effectOrder")) or
                string_list(record.get("effectNames")) or
                string_list(nested_layer.get("effectNames")),
            "materialShaders": string_list(record.get("materialShaders")) or
                string_list(nested_layer.get("materialShaders")),
            "chainShape": chain_shape,
            "candidateRisk": record.get("candidateRisk", nested_layer.get("candidateRisk")),
            "finalPublishRenderTarget": (
                record.get("finalPublishRenderTarget")
                or routing.get("finalPublishRenderTarget")
                or publish.get("finalPublishRenderTarget")
            ),
        })

    return {
        "classification": (
            "protected-puppet-diagnostics-present"
            if layers else
            "no-protected-puppet-diagnostics"
        ),
        "count": len(layers),
        "chainShapeCounts": dict(sorted(chain_counts.items())),
        "layers": layers,
    }


def post_lut_effect_capture_kind(capture: dict[str, Any], final_dimensions: list[int]) -> str | None:
    dimensions = capture.get("dimensions")
    if dimensions != final_dimensions:
        return None
    stage = str(capture.get("stage") or "")
    if stage not in {"effect-output", "final-publish"}:
        return None
    name = str(capture.get("layerName") or "").lower()
    if "flare" in name:
        return "flare"
    if "lens" in name or "lense" in name:
        return "lens"
    return "post-lut-effect"


def disabled_zero_alpha_reason(capture: dict[str, Any]) -> str | None:
    alpha = capture.get("layerAlpha")
    if not isinstance(alpha, (int, float)) or isinstance(alpha, bool):
        return None
    if abs(float(alpha)) > 1.0e-6:
        return None
    user_alphas = [
        float(value)
        for value in capture.get("materialUserAlpha", [])
        if isinstance(value, (int, float)) and not isinstance(value, bool)
    ]
    if user_alphas:
        if all(abs(value) <= 1.0e-6 for value in user_alphas):
            return "zero-alpha-layer-and-material"
        return None
    return "zero-alpha-layer"


def region_delta_from_previous(
    previous_path: Path,
    current_path: Path,
    reference_path: Path,
    final_dimensions: list[int],
    max_size: int,
) -> list[dict[str, Any]]:
    compare_size = constrained_size(final_dimensions[0], final_dimensions[1], max_size)
    previous = image_to_array(previous_path, compare_size)
    current = image_to_array(current_path, compare_size)
    reference = image_to_array(reference_path, compare_size)
    result: list[dict[str, Any]] = []
    for region in region_bounds(compare_size[0], compare_size[1]):
        bounds = region["bounds"]
        previous_reference = region_score(previous, reference, bounds)
        current_reference = region_score(current, reference, bounds)
        previous_to_current = region_score(previous, current, bounds)
        result.append({
            "region": region["region"],
            "bounds": bounds,
            "previousReferenceRmse": rounded(previous_reference),
            "currentReferenceRmse": rounded(current_reference),
            "previousToCurrentRmse": rounded(previous_to_current),
            "referenceRmseDeltaFromPrevious": rounded(current_reference - previous_reference),
        })
    return sorted(
        result,
        key=lambda item: abs(float(item.get("referenceRmseDeltaFromPrevious") or 0.0)),
        reverse=True,
    )


def classify_flare_drift(flare_steps: list[dict[str, Any]]) -> str:
    if not flare_steps:
        return "no-post-lut-flare-captures"
    sorted_steps = sorted(
        flare_steps,
        key=lambda item: float(item.get("previousRmse") or 0.0),
        reverse=True,
    )
    if len(sorted_steps) == 1:
        return "single-flare-dominates"
    top = float(sorted_steps[0].get("previousRmse") or 0.0)
    second = float(sorted_steps[1].get("previousRmse") or 0.0)
    if top > 0.05 and second <= max(0.05, top * 0.35):
        return "shared-flare-default-frame-drift"
    return "multiple-flare-step-drift"


def classify_flare_attribution(
    flare_steps: list[dict[str, Any]],
    disabled_flare_captures: list[dict[str, Any]],
) -> str:
    if not flare_steps and disabled_flare_captures:
        return "no-active-post-lut-flare-captures"
    return classify_flare_drift(flare_steps)


def compare_post_lut_flare_drift(
    summary_path: Path,
    variant_name: str,
    output_dir: Path,
    max_size: int,
) -> dict[str, Any]:
    post_lut = compare_post_lut_drift(summary_path, variant_name, output_dir, max_size)
    final_dimensions = post_lut["finalDimensions"]
    reference_path = Path(post_lut["reference"])
    last_lut = post_lut.get("lastLutSnapshot")
    flare_captures: list[dict[str, Any]] = []
    for capture in post_lut["postLutCaptures"]:
        kind = post_lut_effect_capture_kind(capture, final_dimensions)
        if kind is None or "referenceRmse" not in capture:
            continue
        item = dict(capture)
        item["layerKind"] = kind
        disabled_reason = disabled_zero_alpha_reason(item)
        if disabled_reason:
            item["disabledReason"] = disabled_reason
        flare_captures.append(item)

    active_flare_captures = [
        capture for capture in flare_captures
        if "disabledReason" not in capture
    ]
    disabled_flare_captures = [
        capture for capture in flare_captures
        if "disabledReason" in capture
    ]

    flare_steps: list[dict[str, Any]] = []
    previous_path = Path(last_lut["path"]) if last_lut else None
    previous_reference = float(post_lut.get("lastLutReferenceRmse") or 0.0)
    previous_capture_index = last_lut.get("captureIndex") if last_lut else None
    previous_layer_id = last_lut.get("layerId") if last_lut else None
    for capture in active_flare_captures:
        current_path = Path(capture["path"])
        if previous_path is None:
            previous_path = current_path
            previous_reference = float(capture.get("referenceRmse") or 0.0)
            previous_capture_index = capture.get("captureIndex")
            previous_layer_id = capture.get("layerId")
            continue
        current_reference = float(capture.get("referenceRmse") or 0.0)
        previous_compare = compare_image_paths(previous_path, current_path, max_size)
        step = {
            "captureIndex": capture.get("captureIndex"),
            "stage": capture.get("stage"),
            "layerId": capture.get("layerId"),
            "layerName": capture.get("layerName"),
            "layerKind": capture.get("layerKind"),
            "previousCaptureIndex": previous_capture_index,
            "previousLayerId": previous_layer_id,
            "referenceRmse": capture.get("referenceRmse"),
            "referenceRmseDeltaFromPrevious": rounded(current_reference - previous_reference),
            "previousRmse": previous_compare["rmse"],
            "finalRmse": capture.get("finalRmse"),
            "lastLutRmse": capture.get("lastLutRmse"),
            "renderTarget": capture.get("renderTarget"),
            "path": capture.get("path"),
            "regionDeltas": region_delta_from_previous(
                previous_path,
                current_path,
                reference_path,
                final_dimensions,
                max_size,
            ),
        }
        flare_steps.append(step)
        previous_path = current_path
        previous_reference = current_reference
        previous_capture_index = capture.get("captureIndex")
        previous_layer_id = capture.get("layerId")

    layer_map: dict[int, dict[str, Any]] = {}
    for capture in flare_captures:
        layer_id = capture.get("layerId")
        if not isinstance(layer_id, int):
            continue
        entry = layer_map.setdefault(layer_id, {
            "layerId": layer_id,
            "layerName": capture.get("layerName"),
            "layerKind": capture.get("layerKind"),
            "captureCount": 0,
            "captures": [],
        })
        entry["captureCount"] += 1
        entry["captures"].append({
            "captureIndex": capture.get("captureIndex"),
            "stage": capture.get("stage"),
            "referenceRmse": capture.get("referenceRmse"),
            "finalRmse": capture.get("finalRmse"),
            "lastLutRmse": capture.get("lastLutRmse"),
            "disabledReason": capture.get("disabledReason"),
        })

    largest_steps = sorted(
        flare_steps,
        key=lambda item: float(item.get("previousRmse") or 0.0),
        reverse=True,
    )
    result = {
        "summary": post_lut["summary"],
        "variant": variant_name,
        "effectManifest": post_lut["effectManifest"],
        "reference": post_lut["reference"],
        "finalFrame": post_lut["finalFrame"],
        "classification": classify_flare_attribution(flare_steps, disabled_flare_captures),
        "postLutClassification": post_lut["classification"],
        "postLutDownstreamReferenceRmseDelta": post_lut["downstreamReferenceRmseDelta"],
        "lastLutSnapshot": post_lut["lastLutSnapshot"],
        "flareLayerCount": len(layer_map),
        "flareCaptureCount": len(flare_captures),
        "activeFlareCaptureCount": len(active_flare_captures),
        "disabledFlareCaptureCount": len(disabled_flare_captures),
        "disabledFlareCaptures": disabled_flare_captures,
        "flareLayers": sorted(layer_map.values(), key=lambda item: item["layerId"]),
        "largestFlareSteps": largest_steps,
        "flareSteps": flare_steps,
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "post-lut-flare-drift.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (output_dir / "post-lut-flare-drift.md").write_text(
        format_post_lut_flare_drift_markdown(result),
        encoding="utf-8",
    )
    return result


def inferred_quad_size(width: int, height: int) -> int | None:
    if width != height:
        return None
    if width == 64:
        return 16
    if width == 512:
        return 64
    return None


def compare_layer(summary_path: Path, variant_name: str, layer_id: int, source: Path, output_dir: Path, max_size: int) -> dict[str, Any]:
    summary = load_json(summary_path)
    variant = find_variant(summary, variant_name)
    if not variant.get("effectManifest"):
        raise ValueError(f"variant has no effect manifest: {variant_name}")
    manifest_path = Path(variant["effectManifest"])
    manifest = load_json(manifest_path)
    records = layer_records(manifest, layer_id)
    input_record = stage_record(records, "effect-input")
    output_record = output_stage_record(records)
    material = lut_material(input_record)
    texture_name = lut_texture_name(material)
    lut = load_lut_from_pkg(source, texture_name)
    context = material_context(material)

    output_path = Path(output_record["path"])
    with Image.open(output_path) as output_image:
        compare_size = constrained_size(output_image.width, output_image.height, max_size)
    source_pixels = image_to_array(Path(input_record["path"]), compare_size)
    expected_pixels = image_to_array(output_path, compare_size)
    score_mode = "alpha-weighted"
    scores = rank_lut_candidates(source_pixels, expected_pixels, lut.pixels, context, score_mode=score_mode)
    plain_scores = rank_lut_candidates(source_pixels, expected_pixels, lut.pixels, context, score_mode="rmse")
    input_info = input_record.get("renderTargetInfo", {})
    output_info = output_record.get("renderTargetInfo", {})
    dimensions_match = (input_info.get("width"), input_info.get("height")) == (
        output_info.get("width"),
        output_info.get("height"),
    )
    visibility = {
        key: scores[0].metrics[key]
        for key in ("visibleFraction", "opaqueFraction")
    }
    visibility["trustedVisible"] = trusted_visible(scores[0].metrics)
    visibility["dimensionCompatible"] = dimensions_match
    visibility["trustedComparison"] = visibility["trustedVisible"] and dimensions_match

    output_dir.mkdir(parents=True, exist_ok=True)
    preview_paths: list[str] = []
    for index, score in enumerate(scores[:3], start=1):
        predicted = apply_lut(source_pixels, lut.pixels, score.candidate, context)
        preview = output_dir / f"candidate-{index}.png"
        array_to_image(predicted).save(preview)
        preview_paths.append(str(preview))

    warnings: list[str] = []
    if not dimensions_match:
        warnings.append(
            "effect-input and selected output dimensions differ; the lab resizes input before applying color-only LUT candidates"
        )
    if not visibility["trustedVisible"]:
        warnings.append(
            "low visible/opaque pixel coverage; candidate ranking may be dominated by transparent or partial overlay pixels"
        )
    suggested_quad = inferred_quad_size(lut.width, lut.height)
    manifest_quad_text = material.get("resolvedCombos", {}).get("QUAD_SIZE")
    try:
        manifest_quad = int(manifest_quad_text) if manifest_quad_text is not None else None
    except ValueError:
        manifest_quad = None
    if suggested_quad is not None and manifest_quad is not None and suggested_quad != manifest_quad:
        warnings.append(
            f"texture dimensions suggest QUAD_SIZE={suggested_quad} but manifest resolved QUAD_SIZE={manifest_quad}"
        )
    manifest_candidate = LutCandidate(
        quad_size=manifest_quad or suggested_quad or 16,
        flip_y=str(material.get("resolvedCombos", {}).get("LUT_FLIP_Y", "0")) != "0",
        filter_mode=lut.filter_mode,
        color_mode="shader",
    )
    manifest_candidate_summary: dict[str, Any] = {
        **asdict(manifest_candidate),
        "rank": None,
        "rmse": None,
    }
    for index, score in enumerate(scores, start=1):
        if score.candidate == manifest_candidate:
            manifest_candidate_summary["rank"] = index
            manifest_candidate_summary["rmse"] = score.rmse
            break

    result = {
        "summary": str(summary_path),
        "variant": variant_name,
        "layerId": layer_id,
        "layerName": input_record.get("layer", {}).get("layerName"),
        "texture": texture_name,
        "texturePath": material_texture_path(texture_name),
        "outputStage": output_record.get("stage"),
        "scoreMode": score_mode,
        "textureDimensions": [lut.width, lut.height],
        "textureSampler": {
            "formatId": lut.format_id,
            "flags": lut.flags,
            "filterMode": lut.filter_mode,
            "wrapMode": lut.wrap_mode,
            "clampUvs": lut.clamp_uvs,
            "noInterpolation": lut.no_interpolation,
        },
        "manifestResolvedCombos": material.get("resolvedCombos", {}),
        "manifestMaterialValues": material.get("materialValues", {}),
        "materialContext": asdict(context),
        "visibility": visibility,
        "publishDrift": publish_drift_report(records, variant, max_size),
        "compareSize": list(compare_size),
        "warnings": warnings,
        "previews": preview_paths,
        "manifestCandidate": manifest_candidate_summary,
        "bestAlphaWeighted": candidate_summary(scores[0]),
        "bestPlain": candidate_summary(plain_scores[0]),
        "candidates": [
            candidate_summary(score)
            for score in scores
        ],
    }
    (output_dir / "lut-sampling-report.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (output_dir / "lut-sampling-report.md").write_text(format_markdown(result), encoding="utf-8")
    return result


def candidate_key(candidate: dict[str, Any]) -> str:
    return (
        f"quad={candidate['quad_size']} "
        f"flipY={candidate['flip_y']} "
        f"filter={candidate['filter_mode']} "
        f"color={candidate['color_mode']}"
    )


def compare_variant_lut_layers(
    summary_path: Path,
    variant_name: str,
    source: Path,
    output_dir: Path,
    max_size: int,
) -> dict[str, Any]:
    summary = load_json(summary_path)
    variant = find_variant(summary, variant_name)
    if not variant.get("effectManifest"):
        raise ValueError(f"variant has no effect manifest: {variant_name}")
    manifest_path = Path(variant["effectManifest"])
    manifest = load_json(manifest_path)

    output_dir.mkdir(parents=True, exist_ok=True)
    layer_results: list[dict[str, Any]] = []
    best_candidate_counts: dict[str, int] = {}
    trusted_best_candidate_counts: dict[str, int] = {}
    trusted_layer_count = 0
    low_visibility_layer_count = 0
    dimension_mismatch_layer_count = 0
    publish_classification_counts: dict[str, int] = {}
    for layer_id in lut_layer_ids(manifest):
        records = layer_records(manifest, layer_id)
        layer_output_dir = output_dir / layer_slug(layer_id, layer_name_from_records(records))
        layer_result = compare_layer(summary_path, variant_name, layer_id, source, layer_output_dir, max_size)
        best = layer_result["candidates"][0]
        key = candidate_key(best)
        best_candidate_counts[key] = best_candidate_counts.get(key, 0) + 1
        layer_trusted_visible = bool(layer_result["visibility"]["trustedVisible"])
        layer_trusted_comparison = bool(layer_result["visibility"]["trustedComparison"])
        if layer_trusted_comparison:
            trusted_layer_count += 1
            trusted_best_candidate_counts[key] = trusted_best_candidate_counts.get(key, 0) + 1
        elif not layer_trusted_visible:
            low_visibility_layer_count += 1
        else:
            dimension_mismatch_layer_count += 1
        publish_classification = layer_result["publishDrift"]["classification"]
        publish_classification_counts[publish_classification] = publish_classification_counts.get(publish_classification, 0) + 1
        layer_results.append({
            "layerId": layer_result["layerId"],
            "layerName": layer_result["layerName"],
            "texture": layer_result["texture"],
            "outputStage": layer_result["outputStage"],
            "scoreMode": layer_result["scoreMode"],
            "visibility": layer_result["visibility"],
            "trustedVisible": layer_trusted_visible,
            "trustedComparison": layer_trusted_comparison,
            "textureSampler": layer_result["textureSampler"],
            "materialContext": layer_result["materialContext"],
            "manifestCandidate": layer_result["manifestCandidate"],
            "best": best,
            "bestAlphaWeighted": layer_result["bestAlphaWeighted"],
            "bestPlain": layer_result["bestPlain"],
            "publishDrift": {
                "classification": publish_classification,
                "localStage": layer_result["publishDrift"]["localStage"],
                "finalStage": layer_result["publishDrift"]["finalStage"],
                "publishedStage": layer_result["publishDrift"]["publishedStage"],
                "localDimensions": layer_result["publishDrift"]["localDimensions"],
                "finalDimensions": layer_result["publishDrift"]["finalDimensions"],
                "publishedDimensions": layer_result["publishDrift"]["publishedDimensions"],
                "localToFinal": layer_result["publishDrift"]["localToFinal"],
                "registeredLocalToFinal": layer_result["publishDrift"]["registeredLocalToFinal"],
                "finalToPublished": layer_result["publishDrift"]["finalToPublished"],
                "variantScreenDrift": layer_result["publishDrift"]["variantScreenDrift"],
                "publishDiagnostics": layer_result["publishDrift"]["publishDiagnostics"],
            },
            "warnings": layer_result["warnings"],
            "report": str(layer_output_dir / "lut-sampling-report.md"),
        })

    result = {
        "summary": str(summary_path),
        "variant": variant_name,
        "effectManifest": str(manifest_path),
        "layerCount": len(layer_results),
        "trustedLayerCount": trusted_layer_count,
        "lowVisibilityLayerCount": low_visibility_layer_count,
        "dimensionMismatchLayerCount": dimension_mismatch_layer_count,
        "bestCandidateCounts": dict(sorted(best_candidate_counts.items(), key=lambda item: (-item[1], item[0]))),
        "trustedBestCandidateCounts": dict(sorted(trusted_best_candidate_counts.items(), key=lambda item: (-item[1], item[0]))),
        "publishClassificationCounts": dict(sorted(publish_classification_counts.items(), key=lambda item: (-item[1], item[0]))),
        "layers": layer_results,
    }
    (output_dir / "lut-sampling-summary.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (output_dir / "lut-sampling-summary.md").write_text(
        format_summary_markdown(result),
        encoding="utf-8",
    )
    return result


def format_markdown(result: dict[str, Any]) -> str:
    lines = [
        "# Arona LUT Sampling Lab",
        "",
        f"summary: `{result['summary']}`",
        f"variant: `{result['variant']}`",
        f"layer: `{result['layerId']}` `{result.get('layerName')}`",
        f"texture: `{result['texture']}` `{result['textureDimensions'][0]}x{result['textureDimensions'][1]}`",
        f"texture sampler: `{json.dumps(result['textureSampler'], sort_keys=True)}`",
        f"output stage: `{result['outputStage']}`",
        f"score mode: `{result['scoreMode']}`",
        f"visibility: `{json.dumps(result['visibility'], sort_keys=True)}`",
        f"manifest combos: `{json.dumps(result['manifestResolvedCombos'], sort_keys=True)}`",
        f"manifest material values: `{json.dumps(result['manifestMaterialValues'], sort_keys=True)}`",
        f"material context: `{json.dumps(result['materialContext'], sort_keys=True)}`",
        f"publish drift: `{json.dumps(result['publishDrift'], sort_keys=True)}`",
        f"manifest candidate: rank=`{result['manifestCandidate']['rank']}` rmse=`{result['manifestCandidate']['rmse']}`",
        f"best alpha-weighted: rmse=`{result['bestAlphaWeighted']['rmse']}` `{candidate_key(result['bestAlphaWeighted'])}`",
        f"best plain: rmse=`{result['bestPlain']['rmse']}` `{candidate_key(result['bestPlain'])}`",
        f"compare size: `{result['compareSize'][0]}x{result['compareSize'][1]}`",
        "",
    ]
    if result["warnings"]:
        lines.append("## Warnings")
        for warning in result["warnings"]:
            lines.append(f"- {warning}")
        lines.append("")
    lines.append("## Candidates")
    for index, candidate in enumerate(result["candidates"][:12], start=1):
        lines.append(
            f"{index}. rmse=`{candidate['rmse']:.6f}` "
            f"plain=`{candidate['plainRmse']:.6f}` "
            f"alphaWeighted=`{candidate['alphaWeightedRmse']}` "
            f"opaque=`{candidate['opaqueRmse']}` "
            f"quad=`{candidate['quad_size']}` flipY=`{candidate['flip_y']}` "
            f"filter=`{candidate['filter_mode']}` color=`{candidate['color_mode']}`"
        )
    lines.append("")
    return "\n".join(lines)


def format_summary_markdown(result: dict[str, Any]) -> str:
    lines = [
        "# Arona LUT Sampling Summary",
        "",
        f"summary: `{result['summary']}`",
        f"variant: `{result['variant']}`",
        f"effect manifest: `{result['effectManifest']}`",
        f"layer count: `{result['layerCount']}`",
        f"trusted layer count: `{result['trustedLayerCount']}`",
        f"low visibility layer count: `{result['lowVisibilityLayerCount']}`",
        f"dimension mismatch layer count: `{result['dimensionMismatchLayerCount']}`",
        "",
        "## Best Candidate Counts",
    ]
    for key, count in result["bestCandidateCounts"].items():
        lines.append(f"- `{key}`: `{count}`")
    lines.extend(["", "## Trusted Best Candidate Counts"])
    for key, count in result["trustedBestCandidateCounts"].items():
        lines.append(f"- `{key}`: `{count}`")
    lines.extend(["", "## Publish Classification Counts"])
    for key, count in result["publishClassificationCounts"].items():
        lines.append(f"- `{key}`: `{count}`")
    lines.extend(["", "## Layers"])
    for layer in result["layers"]:
        best = layer["best"]
        publish = layer["publishDrift"]
        local_to_final = publish.get("localToFinal") or {}
        registered_local_to_final = publish.get("registeredLocalToFinal") or {}
        final_to_published = publish.get("finalToPublished") or {}
        diagnostics = publish.get("publishDiagnostics") or {}
        lines.append(
            f"- `{layer['layerId']}` `{layer['layerName']}` "
            f"stage=`{layer['outputStage']}` texture=`{layer['texture']}` "
            f"trusted=`{layer['trustedComparison']}` "
            f"visibleTrusted=`{layer['trustedVisible']}` "
            f"dimensionCompatible=`{layer['visibility']['dimensionCompatible']}` "
            f"visible=`{layer['visibility']['visibleFraction']}` "
            f"opaque=`{layer['visibility']['opaqueFraction']}` "
            f"manifestRank=`{layer['manifestCandidate']['rank']}` "
            f"manifestRmse=`{layer['manifestCandidate']['rmse']}` "
            f"bestRmse=`{best['rmse']}` plainRmse=`{best['plainRmse']}` "
            f"best=`{candidate_key(best)}` "
            f"publishClass=`{publish['classification']}` "
            f"localToFinalRmse=`{local_to_final.get('rmse')}` "
            f"registeredLocalToFinalRmse=`{registered_local_to_final.get('rmse')}` "
            f"registeredCrop=`{registered_local_to_final.get('cropPixels')}` "
            f"finalToPublishedRmse=`{final_to_published.get('rmse')}` "
            f"materialTiming=`{diagnostics.get('materialOutputCaptureTiming')}` "
            f"finalPublishTiming=`{diagnostics.get('finalPublishCaptureTiming')}`"
        )
        if layer["warnings"]:
            for warning in layer["warnings"]:
                lines.append(f"  warning: {warning}")
        lines.append(f"  report: `{layer['report']}`")
    lines.append("")
    return "\n".join(lines)


def format_default_frame_progression_markdown(result: dict[str, Any]) -> str:
    lines = [
        "# Arona Default-Frame Progression",
        "",
        f"summary: `{result['summary']}`",
        f"variant: `{result['variant']}`",
        f"effect manifest: `{result['effectManifest']}`",
        f"reference: `{result['reference']}`",
        f"final frame: `{result['finalFrame']}`",
        f"final reference rmse: `{result['finalReferenceRmse']}`",
        f"snapshot count: `{result['snapshotCount']}`",
        f"skipped capture count: `{result['skippedCaptureCount']}`",
        "",
        "## Largest Step Deltas",
    ]
    for step in result["largestStepDeltas"]:
        lines.append(
            f"- `{step['fromLayerId']}` -> `{step['toLayerId']}` `{step['toLayerName']}` "
            f"capture=`{step['toCaptureIndex']}` stage=`{step['stage']}` "
            f"stepRmse=`{step['deltaFromPreviousRmse']}` "
            f"referenceDelta=`{step['referenceRmseDeltaFromPrevious']}` "
            f"referenceRmse=`{step['referenceRmse']}` finalRmse=`{step['finalRmse']}`"
        )
    lines.extend(["", "## Largest Reference Improvements"])
    for step in result["largestReferenceImprovements"]:
        lines.append(
            f"- `{step['fromLayerId']}` -> `{step['toLayerId']}` `{step['toLayerName']}` "
            f"capture=`{step['toCaptureIndex']}` "
            f"referenceDelta=`{step['referenceRmseDeltaFromPrevious']}` "
            f"stepRmse=`{step['deltaFromPreviousRmse']}`"
        )
    lines.extend(["", "## Largest Reference Regressions"])
    for step in result["largestReferenceRegressions"]:
        lines.append(
            f"- `{step['fromLayerId']}` -> `{step['toLayerId']}` `{step['toLayerName']}` "
            f"capture=`{step['toCaptureIndex']}` "
            f"referenceDelta=`{step['referenceRmseDeltaFromPrevious']}` "
            f"stepRmse=`{step['deltaFromPreviousRmse']}`"
        )
    lines.extend(["", "## Snapshots"])
    for snapshot in result["snapshots"]:
        lines.append(
            f"- capture=`{snapshot['captureIndex']}` order=`{snapshot['manifestOrder']}` "
            f"layer=`{snapshot['layerId']}` `{snapshot['layerName']}` "
            f"stage=`{snapshot['stage']}` referenceRmse=`{snapshot['referenceRmse']}` "
            f"finalRmse=`{snapshot['finalRmse']}` "
            f"stepRmse=`{snapshot['deltaFromPreviousRmse']}` "
            f"referenceDelta=`{snapshot['referenceRmseDeltaFromPrevious']}`"
        )
    lines.append("")
    return "\n".join(lines)


def format_post_lut_drift_markdown(result: dict[str, Any]) -> str:
    lines = [
        "# Arona Post-LUT Drift Attribution",
        "",
        f"summary: `{result['summary']}`",
        f"variant: `{result['variant']}`",
        f"effect manifest: `{result['effectManifest']}`",
        f"reference: `{result['reference']}`",
        f"final frame: `{result['finalFrame']}`",
        f"classification: `{result['classification']}`",
        f"snapshot count: `{result['snapshotCount']}`",
        f"final reference rmse: `{result['finalReferenceRmse']}`",
        f"last LUT reference rmse: `{result['lastLutReferenceRmse']}`",
        f"downstream reference rmse delta: `{result['downstreamReferenceRmseDelta']}`",
        "",
    ]
    last = result.get("lastLutSnapshot")
    if last is not None:
        lines.extend([
            "## Last LUT Snapshot",
            (
                f"- capture=`{last['captureIndex']}` layer=`{last['layerId']}` "
                f"`{last['layerName']}` stage=`{last['stage']}` "
                f"dimensions=`{last['dimensions']}` path=`{last['path']}`"
            ),
            f"- last LUT to final: `{json.dumps(result['lastLutToFinal'], sort_keys=True)}`",
            "",
        ])
    attribution = result.get("fullFrameAttribution", {})
    lines.extend([
        "## Full-Frame Attribution",
        f"classification: `{attribution.get('classification')}`",
        f"full-frame capture count: `{attribution.get('fullFrameCaptureCount')}`",
        f"active capture count: `{attribution.get('activeCaptureCount')}`",
        f"disabled capture count: `{attribution.get('disabledCaptureCount')}`",
    ])
    first_step = attribution.get("firstActiveStep")
    if first_step is not None:
        lines.append(
            f"first active step: capture=`{first_step['captureIndex']}` "
            f"layer=`{first_step['layerId']}` `{first_step['layerName']}` "
            f"stage=`{first_step['stage']}` previousRmse=`{first_step['previousRmse']}` "
            f"referenceDelta=`{first_step['referenceRmseDeltaFromPrevious']}`"
        )
    if attribution.get("largestSteps"):
        lines.extend(["", "### Largest Active Full-Frame Steps"])
        for step in attribution["largestSteps"]:
            lines.append(
                f"- capture=`{step['captureIndex']}` layer=`{step['layerId']}` "
                f"`{step['layerName']}` stage=`{step['stage']}` "
                f"previousRmse=`{step['previousRmse']}` "
                f"referenceDelta=`{step['referenceRmseDeltaFromPrevious']}` "
                f"referenceRmse=`{step['referenceRmse']}`"
            )
    if attribution.get("disabledCaptures"):
        lines.extend(["", "### Disabled/No-Op Full-Frame Captures"])
        for capture in attribution["disabledCaptures"]:
            lines.append(
                f"- capture=`{capture['captureIndex']}` layer=`{capture['layerId']}` "
                f"`{capture['layerName']}` stage=`{capture['stage']}` "
                f"reason=`{capture['disabledReason']}` "
                f"alpha=`{capture.get('layerAlpha')}` "
                f"materialUserAlpha=`{capture.get('materialUserAlpha')}` "
                f"referenceRmse=`{capture.get('referenceRmse')}`"
            )
    lines.append("")
    boundary = result.get("defaultRtBoundaryAttribution", {})
    lines.extend([
        "## Default-RT Boundary Attribution",
        f"classification: `{boundary.get('classification')}`",
        f"boundary capture count: `{boundary.get('boundaryCaptureCount')}`",
    ])
    first_boundary = boundary.get("firstTransition")
    if first_boundary is not None:
        lines.append(
            f"first transition: capture=`{first_boundary['captureIndex']}` "
            f"layer=`{first_boundary['layerId']}` `{first_boundary['layerName']}` "
            f"stage=`{first_boundary['stage']}` previousRmse=`{first_boundary['previousRmse']}` "
            f"referenceDelta=`{first_boundary['referenceRmseDeltaFromPrevious']}` "
            f"disabledReason=`{first_boundary.get('disabledReason')}`"
        )
    if boundary.get("largestTransitions"):
        lines.extend(["", "### Largest Default-RT Boundary Transitions"])
        for step in boundary["largestTransitions"]:
            lines.append(
                f"- capture=`{step['captureIndex']}` layer=`{step['layerId']}` "
                f"`{step['layerName']}` stage=`{step['stage']}` "
                f"previousCapture=`{step['previousCaptureIndex']}` "
                f"previousRmse=`{step['previousRmse']}` "
                f"referenceDelta=`{step['referenceRmseDeltaFromPrevious']}` "
                f"referenceRmse=`{step['referenceRmse']}` "
                f"disabledReason=`{step.get('disabledReason')}`"
            )
    lines.append("")
    protected = result.get("protectedPuppetDiagnostics", {})
    lines.extend([
        "## Protected Puppet Diagnostics",
        f"classification: `{protected.get('classification')}`",
        f"count: `{protected.get('count')}`",
        f"chain shape counts: `{json.dumps(protected.get('chainShapeCounts', {}), sort_keys=True)}`",
    ])
    for layer in protected.get("layers", []):
        lines.append(
            f"- `{layer.get('layerId')}` `{layer.get('layerName')}` "
            f"kind=`{layer.get('diagnosticKind')}` mode=`{layer.get('captureMode')}` "
            f"class=`{layer.get('class')}` chain=`{layer.get('chainShape')}` "
            f"risk=`{layer.get('candidateRisk')}` "
            f"alpha=`{layer.get('alpha')}` target=`{layer.get('finalPublishRenderTarget')}` "
            f"effects=`{layer.get('effectOrder')}` shaders=`{layer.get('materialShaders')}`"
        )
    lines.append("")
    lines.append("## Region Drift")
    for region in result["regionDrift"]:
        lines.append(
            f"- `{region['region']}` bounds=`{region['bounds']}` "
            f"delta=`{region['downstreamReferenceRmseDelta']}` "
            f"lastLutRef=`{region['lastLutReferenceRmse']}` "
            f"finalRef=`{region['finalReferenceRmse']}` "
            f"lastToFinal=`{region['lastLutToFinalRmse']}`"
        )
    lines.extend(["", "## Post-LUT Captures"])
    for capture in result["postLutCaptures"]:
        detail = (
            f"- capture=`{capture['captureIndex']}` layer=`{capture['layerId']}` "
            f"`{capture['layerName']}` stage=`{capture['stage']}` "
            f"target=`{capture['renderTarget']}` dimensions=`{capture['dimensions']}`"
        )
        if "referenceRmse" in capture:
            detail += (
                f" referenceRmse=`{capture['referenceRmse']}` "
                f"finalRmse=`{capture['finalRmse']}` "
                f"lastLutRmse=`{capture['lastLutRmse']}`"
            )
        lines.append(detail)
    lines.append("")
    return "\n".join(lines)


def format_post_lut_flare_drift_markdown(result: dict[str, Any]) -> str:
    lines = [
        "# Arona Post-LUT Flare Drift",
        "",
        f"summary: `{result['summary']}`",
        f"variant: `{result['variant']}`",
        f"effect manifest: `{result['effectManifest']}`",
        f"reference: `{result['reference']}`",
        f"final frame: `{result['finalFrame']}`",
        f"classification: `{result['classification']}`",
        f"post-LUT classification: `{result['postLutClassification']}`",
        f"post-LUT downstream reference rmse delta: `{result['postLutDownstreamReferenceRmseDelta']}`",
        f"flare layer count: `{result['flareLayerCount']}`",
        f"flare capture count: `{result['flareCaptureCount']}`",
        f"active flare capture count: `{result['activeFlareCaptureCount']}`",
        f"disabled flare capture count: `{result['disabledFlareCaptureCount']}`",
        "",
        "## Largest Flare Steps",
    ]
    for step in result["largestFlareSteps"]:
        top_region = step["regionDeltas"][0] if step["regionDeltas"] else {}
        lines.append(
            f"- capture=`{step['captureIndex']}` layer=`{step['layerId']}` "
            f"`{step['layerName']}` kind=`{step.get('layerKind')}` stage=`{step['stage']}` "
            f"previousCapture=`{step['previousCaptureIndex']}` "
            f"previousRmse=`{step['previousRmse']}` "
            f"referenceDelta=`{step['referenceRmseDeltaFromPrevious']}` "
            f"referenceRmse=`{step['referenceRmse']}` "
            f"topRegion=`{top_region.get('region')}` "
            f"topRegionDelta=`{top_region.get('referenceRmseDeltaFromPrevious')}`"
        )
    lines.extend(["", "## Disabled/No-Op Flare Captures"])
    for capture in result["disabledFlareCaptures"]:
        lines.append(
            f"- capture=`{capture['captureIndex']}` layer=`{capture['layerId']}` "
            f"`{capture['layerName']}` kind=`{capture.get('layerKind')}` "
            f"stage=`{capture['stage']}` reason=`{capture['disabledReason']}` "
            f"alpha=`{capture.get('layerAlpha')}` materialUserAlpha=`{capture.get('materialUserAlpha')}` "
            f"referenceRmse=`{capture.get('referenceRmse')}` finalRmse=`{capture.get('finalRmse')}`"
        )
    lines.extend(["", "## Flare Layers"])
    for layer in result["flareLayers"]:
        lines.append(
            f"- `{layer['layerId']}` `{layer['layerName']}` kind=`{layer.get('layerKind')}` captures=`{layer['captureCount']}`"
        )
        for capture in layer["captures"]:
            lines.append(
                f"  capture=`{capture['captureIndex']}` stage=`{capture['stage']}` "
                f"referenceRmse=`{capture['referenceRmse']}` finalRmse=`{capture['finalRmse']}` "
                f"lastLutRmse=`{capture['lastLutRmse']}` "
                f"disabledReason=`{capture.get('disabledReason')}`"
            )
    lines.extend(["", "## Region Deltas"])
    for step in result["largestFlareSteps"][:6]:
        lines.append(
            f"capture `{step['captureIndex']}` layer `{step['layerId']}` `{step['layerName']}`:"
        )
        for region in step["regionDeltas"][:3]:
            lines.append(
                f"- `{region['region']}` bounds=`{region['bounds']}` "
                f"referenceDelta=`{region['referenceRmseDeltaFromPrevious']}` "
                f"previousToCurrent=`{region['previousToCurrentRmse']}`"
            )
    lines.append("")
    return "\n".join(lines)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Rank Arona lut_loader sampling variants against effect captures.")
    parser.add_argument("summary", type=Path)
    parser.add_argument("--variant", default="night")
    parser.add_argument("--layer-id", type=int, default=82)
    parser.add_argument("--all-lut-layers", action="store_true")
    parser.add_argument("--default-frame-progression", action="store_true")
    parser.add_argument("--post-lut-drift", action="store_true")
    parser.add_argument("--post-lut-flare-drift", action="store_true")
    parser.add_argument("--source", type=Path, default=default_source())
    parser.add_argument("--output-dir", type=Path, default=Path("/tmp/yakkai-arona-lut-lab"))
    parser.add_argument("--max-size", type=int, default=512)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.default_frame_progression:
        result = compare_default_frame_progression(args.summary, args.variant, args.output_dir, args.max_size)
        print(f"progression: {args.output_dir / 'default-frame-progression.md'}")
        print(f"snapshots: {result['snapshotCount']}")
        print(f"final-reference-rmse: {result['finalReferenceRmse']}")
        return 0
    if args.post_lut_drift:
        result = compare_post_lut_drift(args.summary, args.variant, args.output_dir, args.max_size)
        print(f"post-lut-drift: {args.output_dir / 'post-lut-drift.md'}")
        print(f"classification: {result['classification']}")
        print(f"downstream-reference-rmse-delta: {result['downstreamReferenceRmseDelta']}")
        return 0
    if args.post_lut_flare_drift:
        result = compare_post_lut_flare_drift(args.summary, args.variant, args.output_dir, args.max_size)
        print(f"post-lut-flare-drift: {args.output_dir / 'post-lut-flare-drift.md'}")
        print(f"classification: {result['classification']}")
        print(f"flare-layers: {result['flareLayerCount']}")
        return 0
    if args.all_lut_layers:
        result = compare_variant_lut_layers(args.summary, args.variant, args.source, args.output_dir, args.max_size)
        print(f"summary: {args.output_dir / 'lut-sampling-summary.md'}")
        print(f"layers: {result['layerCount']}")
        for key, count in result["bestCandidateCounts"].items():
            print(f"best-count: {count} {key}")
        return 0
    result = compare_layer(args.summary, args.variant, args.layer_id, args.source, args.output_dir, args.max_size)
    best = result["candidates"][0]
    print(f"report: {args.output_dir / 'lut-sampling-report.md'}")
    print(
        f"best: rmse={best['rmse']:.6f} quad={best['quad_size']} "
        f"flipY={best['flip_y']} filter={best['filter_mode']} color={best['color_mode']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
