#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
from typing import Iterable

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover - exercised only on missing optional dependency
    Image = None
    ImageDraw = None


DEFAULT_SHADERS = {"effects/pulse", "effects/waterwaves", "effects/shake"}


@dataclass(frozen=True)
class TexImage:
    width: int
    height: int
    pixels: "object"
    format_id: int
    flags: int
    filter_mode: str
    wrap_mode: str
    clamp_uvs: bool
    no_interpolation: bool


def texture_package_path(texture_name: str) -> str:
    if texture_name.endswith(".tex"):
        texture_name = texture_name[:-4]
    if texture_name.startswith("materials/"):
        return texture_name + ".tex"
    return f"materials/{texture_name}.tex"


def safe_texture_name(texture_name: str) -> str:
    return texture_name.replace("/", "__").replace("\\", "__").replace(" ", "_")


def layer_local_bounds_to_pixels(bounds: list[float], image_size: tuple[int, int]) -> tuple[int, int, int, int]:
    width, height = image_size
    min_x, min_y, max_x, max_y = bounds
    return (
        round(min_x + width / 2),
        round(min_y + height / 2),
        round(max_x + width / 2),
        round(max_y + height / 2),
    )


def mask_region_stats(
    mask_pixels: "object",
    source_size: tuple[int, int],
    region_pixels: tuple[int, int, int, int],
) -> dict:
    import numpy as np

    pixels = np.asarray(mask_pixels, dtype=np.float32)
    if pixels.ndim != 3 or pixels.shape[2] < 3:
        raise ValueError("mask pixels must be an HxWxC array with at least three channels")
    source_width, source_height = source_size
    mask_height, mask_width = pixels.shape[:2]
    if source_width <= 0 or source_height <= 0 or mask_width <= 0 or mask_height <= 0:
        raise ValueError("source and mask sizes must be positive")

    x0, y0, x1, y1 = region_pixels
    mx0 = max(0, min(mask_width, math.floor(x0 * mask_width / source_width)))
    my0 = max(0, min(mask_height, math.floor(y0 * mask_height / source_height)))
    mx1 = max(0, min(mask_width, math.ceil(x1 * mask_width / source_width)))
    my1 = max(0, min(mask_height, math.ceil(y1 * mask_height / source_height)))
    if mx1 <= mx0 or my1 <= my0:
        return {
            "maskPixels": [mx0, my0, mx1, my1],
            "nonzeroFraction": 0.0,
            "channelMean": [0.0, 0.0, 0.0],
            "channelMax": [0.0, 0.0, 0.0],
        }

    crop = pixels[my0:my1, mx0:mx1, :3]
    activity = np.max(crop, axis=2)
    return {
        "maskPixels": [mx0, my0, mx1, my1],
        "nonzeroFraction": round(float(np.count_nonzero(activity > 1.0e-4) / activity.size), 6),
        "channelMean": [round(float(value), 6) for value in np.mean(crop, axis=(0, 1))],
        "channelMax": [round(float(value), 6) for value in np.max(crop, axis=(0, 1))],
    }


def _layer_id(record: dict) -> int | None:
    layer = record.get("layer", {})
    return layer.get("layerId", record.get("layerId"))


def select_layer_materials(manifest: dict, layer_id: int, shaders: set[str]) -> list[dict]:
    for record in manifest.get("captures", []):
        if record.get("stage") != "effect-input":
            continue
        if _layer_id(record) != layer_id:
            continue
        return [
            material
            for material in record.get("layer", {}).get("effectMaterials", [])
            if material.get("shader") in shaders
        ]
    return []


def texture_names_for_materials(materials: Iterable[dict]) -> list[str]:
    names: list[str] = []
    seen: set[str] = set()
    for material in materials:
        for binding in material.get("textureBindings", []):
            resolved = binding.get("resolved") or ""
            if not resolved.startswith("masks/"):
                continue
            if resolved in seen:
                continue
            seen.add(resolved)
            names.append(resolved)
    return names


def _first_number(values: dict, key: str, default: float) -> float:
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


def effect_slot_displacement_estimate(
    material: dict,
    slot_stats: dict,
    source_size: tuple[int, int],
) -> dict:
    shader = str(material.get("shader", ""))
    values = material.get("materialValues", {})
    source_width, source_height = source_size
    if shader.endswith("waterwaves"):
        strength = _first_number(values, "strength", 0.0)
        mask_max = max(float(value) for value in slot_stats.get("channelMax", [0.0, 0.0, 0.0])[:3])
        max_uv = strength * strength * mask_max
        return {
            "kind": "waterwaves",
            "strength": round(strength, 6),
            "maskMax": round(mask_max, 6),
            "maxUv": round(max_uv, 6),
            "maxPixels": round(max_uv * max(source_width, source_height), 6),
        }
    if shader.endswith("shake"):
        amp = _first_number(values, "strength", _first_number(values, "amp", 0.0))
        means = slot_stats.get("channelMean", [0.498, 0.498, 0.0])
        flow_x = (float(means[0]) - 0.498) * 2.0
        flow_y = (float(means[1]) - 0.498) * 2.0
        amp2 = amp * amp
        return {
            "kind": "shake",
            "strength": round(amp, 6),
            "meanFlow": [round(flow_x, 6), round(flow_y, 6)],
            "peakUv": [round(abs(flow_x) * amp2, 6), round(abs(flow_y) * amp2, 6)],
            "peakPixels": [
                round(abs(flow_x) * amp2 * source_width, 6),
                round(abs(flow_y) * amp2 * source_height, 6),
            ],
        }
    return {"kind": "unsupported"}


def classify_mismatch(mask_iou: float, constant_delta: float, time_delta: float, crop_rmse: float) -> str:
    if mask_iou < 0.5:
        return "mask-coordinate"
    if constant_delta > 1.0e-3:
        return "shader-constant"
    if time_delta > 1.0e-3:
        return "effect-time"
    if crop_rmse > 0.05:
        return "shader-math"
    return "no-generic-mismatch-found"


def iter_json_values(text: str) -> list[dict]:
    decoder = json.JSONDecoder()
    values: list[dict] = []
    index = 0
    while index < len(text):
        while index < len(text) and text[index].isspace():
            index += 1
        if index >= len(text):
            break
        value, index = decoder.raw_decode(text, index)
        if isinstance(value, list):
            values.extend(item for item in value if isinstance(item, dict))
        elif isinstance(value, dict):
            values.append(value)
    return values


def load_slot_bounds(path: Path) -> dict[str, list[float]]:
    values = iter_json_values(path.read_text(encoding="utf-8"))
    out: dict[str, list[float]] = {}
    for value in values:
        slot = value.get("slot")
        bounds = value.get("layerLocalBounds")
        if slot is None or not isinstance(bounds, list) or len(bounds) != 4:
            continue
        out[str(slot)] = [float(item) for item in bounds]
    return out


def image_size(path: Path | None) -> tuple[int, int]:
    if path is None:
        return (4160, 2923)
    if Image is None:
        return (4160, 2923)
    with Image.open(path) as image:
        return image.size


def material_summary(material: dict) -> dict:
    return {
        "effectIndex": material.get("effectIndex"),
        "materialIndex": material.get("materialIndex"),
        "shader": material.get("shader"),
        "textureBindings": material.get("textureBindings", []),
        "materialValues": material.get("materialValues", {}),
        "resolvedConstValues": material.get("resolvedConstValues", {}),
        "defines": material.get("defines", []),
    }


def build_report(
    manifest: dict,
    layer_id: int,
    slot_bounds: dict[str, list[float]],
    image_size: tuple[int, int],
    mask_images: list[dict] | None = None,
) -> dict:
    materials = select_layer_materials(manifest, layer_id=layer_id, shaders=DEFAULT_SHADERS)
    material_summaries = [material_summary(material) for material in materials]
    mask_images_by_texture = {image.get("texture"): image for image in mask_images or []}
    effect_slot_estimates: list[dict] = []
    for material in material_summaries:
        for binding in material.get("textureBindings", []):
            texture = binding.get("resolved")
            image = mask_images_by_texture.get(texture)
            if image is None:
                continue
            for slot, stats in image.get("slotStats", {}).items():
                estimate = effect_slot_displacement_estimate(material, stats, image_size)
                if estimate.get("kind") == "unsupported":
                    continue
                effect_slot_estimates.append({
                    "effectIndex": material.get("effectIndex"),
                    "shader": material.get("shader"),
                    "texture": texture,
                    "slot": slot,
                    "estimate": estimate,
                })
    return {
        "layerId": layer_id,
        "imageSize": list(image_size),
        "classification": "visual-oracle-insufficient",
        "classificationReason": (
            "Initial report maps authored masks/constants to slot bounds; Windows crop "
            "comparison is required before production renderer changes."
        ),
        "materials": material_summaries,
        "maskTextures": texture_names_for_materials(material_summaries),
        "maskImages": mask_images or [],
        "effectSlotEstimates": effect_slot_estimates,
        "slots": {
            slot: {
                "layerLocalBounds": bounds,
                "pixelBounds": list(layer_local_bounds_to_pixels(bounds, image_size)),
            }
            for slot, bounds in sorted(slot_bounds.items(), key=lambda item: int(item[0]))
        },
    }


def decode_tex_image(data: bytes) -> TexImage:
    import numpy as np
    from arona_lut_sampling_lab import BinaryReader, lz4_decompress_block

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

    if width <= 0 or height <= 0:
        raise ValueError(f"invalid texture dimensions: {width}x{height}")
    payload = reader.read(source_size)
    if compressed:
        payload = lz4_decompress_block(payload, decompressed_size)

    pixel_count = width * height
    if tex_format == 0:
        expected_size = pixel_count * 4
        if len(payload) < expected_size:
            raise ValueError(f"RGBA8 payload too small for {width}x{height}: {len(payload)} < {expected_size}")
        pixels = np.frombuffer(payload[:expected_size], dtype=np.uint8).reshape((height, width, 4))
    elif tex_format == 8:
        expected_size = pixel_count * 2
        if len(payload) < expected_size:
            raise ValueError(f"RG8 payload too small for {width}x{height}: {len(payload)} < {expected_size}")
        rg = np.frombuffer(payload[:expected_size], dtype=np.uint8).reshape((height, width, 2))
        pixels = np.empty((height, width, 4), dtype=np.uint8)
        pixels[:, :, 0] = rg[:, :, 0]
        pixels[:, :, 1] = rg[:, :, 1]
        pixels[:, :, 2] = 0
        pixels[:, :, 3] = 255
    elif tex_format == 9:
        expected_size = pixel_count
        if len(payload) < expected_size:
            raise ValueError(f"R8 payload too small for {width}x{height}: {len(payload)} < {expected_size}")
        r = np.frombuffer(payload[:expected_size], dtype=np.uint8).reshape((height, width))
        pixels = np.empty((height, width, 4), dtype=np.uint8)
        pixels[:, :, 0] = r
        pixels[:, :, 1] = r
        pixels[:, :, 2] = r
        pixels[:, :, 3] = 255
    else:
        raise ValueError(f"unsupported texture format {tex_format}")

    no_interpolation = bool(flags & 1)
    clamp_uvs = bool(flags & 2)
    return TexImage(
        width=header_width or width,
        height=header_height or height,
        pixels=pixels.astype(np.float32) / 255.0,
        format_id=tex_format,
        flags=flags,
        filter_mode="nearest" if no_interpolation else "bilinear",
        wrap_mode="clamp" if clamp_uvs else "repeat",
        clamp_uvs=clamp_uvs,
        no_interpolation=no_interpolation,
    )


def save_mask_textures(scene_pkg: Path, mask_textures: list[str], output_dir: Path) -> list[dict]:
    if Image is None:
        raise RuntimeError("Pillow is required to export mask textures")
    import numpy as np
    from arona_lut_sampling_lab import PkgIndex

    output_dir.mkdir(parents=True, exist_ok=True)
    package = PkgIndex.from_path(scene_pkg)
    outputs: list[dict] = []
    for texture in mask_textures:
        package_path = texture_package_path(texture)
        tex = decode_tex_image(package.read(package_path))
        pixels = (np.clip(tex.pixels, 0.0, 1.0) * 255.0).round().astype(np.uint8)
        out_path = output_dir / f"{safe_texture_name(texture)}.png"
        Image.fromarray(pixels, "RGBA").save(out_path)
        outputs.append({
            "texture": texture,
            "packagePath": package_path,
            "path": str(out_path),
            "width": tex.width,
            "height": tex.height,
            "formatId": tex.format_id,
        })
    return outputs


def annotate_mask_slot_stats(
    mask_images: list[dict],
    slot_bounds: dict[str, list[float]],
    source_size: tuple[int, int],
    slots: tuple[str, ...] = ("3", "13"),
) -> list[dict]:
    if Image is None:
        return mask_images
    import numpy as np

    annotated: list[dict] = []
    for info in mask_images:
        with Image.open(info["path"]).convert("RGBA") as image:
            pixels = np.asarray(image, dtype=np.float32) / 255.0
        slot_stats: dict[str, dict] = {}
        for slot in slots:
            bounds = slot_bounds.get(slot)
            if bounds is None:
                continue
            region = layer_local_bounds_to_pixels(bounds, source_size)
            slot_stats[slot] = mask_region_stats(pixels, source_size, region)
        annotated.append({**info, "slotStats": slot_stats})
    return annotated


def write_mask_contact_sheet(mask_images: list[dict], output_path: Path, thumb_size: tuple[int, int] = (260, 182)) -> None:
    if Image is None or ImageDraw is None:
        return
    if not mask_images:
        return
    cols = 3
    rows = math.ceil(len(mask_images) / cols)
    label_height = 26
    gutter = 8
    width = cols * thumb_size[0] + (cols + 1) * gutter
    height = rows * (thumb_size[1] + label_height) + (rows + 1) * gutter
    sheet = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(sheet)
    for index, info in enumerate(mask_images):
        col = index % cols
        row = index // cols
        x = gutter + col * (thumb_size[0] + gutter)
        y = gutter + row * (thumb_size[1] + label_height + gutter)
        with Image.open(info["path"]).convert("RGBA") as image:
            image.thumbnail(thumb_size)
            preview = Image.new("RGBA", thumb_size, (0, 0, 0, 255))
            offset = ((thumb_size[0] - image.width) // 2, (thumb_size[1] - image.height) // 2)
            preview.alpha_composite(image, offset)
            sheet.paste(preview.convert("RGB"), (x, y))
        label = str(info["texture"]).removeprefix("masks/")
        draw.text((x, y + thumb_size[1] + 4), label[:40], fill=(0, 0, 0))
    sheet.save(output_path)


def write_markdown(report: dict, path: Path) -> None:
    lines = [
        "# Arona Mask/Effect Parity Report",
        "",
        f"- Layer: `{report['layerId']}`",
        f"- Image size: `{report['imageSize'][0]}x{report['imageSize'][1]}`",
        f"- Classification: `{report['classification']}`",
        f"- Reason: {report['classificationReason']}",
        "",
        "## Slot Bounds",
        "",
    ]
    for slot, info in report["slots"].items():
        lines.append(
            f"- Slot `{slot}`: layer-local `{info['layerLocalBounds']}`, pixels `{info['pixelBounds']}`"
        )
    lines.extend(["", "## Materials", ""])
    for material in report["materials"]:
        masks = [
            binding.get("resolved")
            for binding in material.get("textureBindings", [])
            if str(binding.get("resolved", "")).startswith("masks/")
        ]
        lines.append(
            f"- Effect `{material.get('effectIndex')}` material `{material.get('materialIndex')}` "
            f"shader `{material.get('shader')}` masks `{masks}`"
        )
    lines.extend(["", "## Mask Textures", ""])
    for texture in report["maskTextures"]:
        lines.append(f"- `{texture}`")
    if report["maskImages"]:
        lines.extend(["", "## Extracted Mask Images", ""])
        for image in report["maskImages"]:
            lines.append(
                f"- `{image['texture']}` -> `{image['path']}` "
                f"({image['width']}x{image['height']}, format `{image.get('formatId', 'unknown')}`)"
            )
            for slot, stats in image.get("slotStats", {}).items():
                lines.append(
                    f"  - slot `{slot}` coverage `{stats['nonzeroFraction']}` "
                    f"mean `{stats['channelMean']}` max `{stats['channelMax']}`"
                )
    if report["effectSlotEstimates"]:
        lines.extend(["", "## Effect Slot Displacement Estimates", ""])
        for item in report["effectSlotEstimates"]:
            lines.append(
                f"- Effect `{item['effectIndex']}` `{item['shader']}` texture `{item['texture']}` "
                f"slot `{item['slot']}` estimate `{item['estimate']}`"
            )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build Arona layer 405 mask/effect parity diagnostics.")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--layer-id", type=int, default=405)
    parser.add_argument("--slot-bounds-json", type=Path, required=True)
    parser.add_argument("--effect-input", type=Path)
    parser.add_argument("--scene-pkg", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    slots = load_slot_bounds(args.slot_bounds_json)
    size = image_size(args.effect_input)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    mask_textures = texture_names_for_materials(
        [material_summary(material) for material in select_layer_materials(manifest, args.layer_id, DEFAULT_SHADERS)]
    )
    mask_images: list[dict] = []
    if args.scene_pkg is not None:
        mask_images = save_mask_textures(args.scene_pkg, mask_textures, args.output_dir / "masks")
        mask_images = annotate_mask_slot_stats(mask_images, slots, size)
        write_mask_contact_sheet(mask_images, args.output_dir / "mask-contact-sheet.png")

    report = build_report(
        manifest,
        layer_id=args.layer_id,
        slot_bounds=slots,
        image_size=size,
        mask_images=mask_images,
    )

    json_path = args.output_dir / "mask-effect-report.json"
    md_path = args.output_dir / "mask-effect-report.md"
    json_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_markdown(report, md_path)
    print(f"wrote {json_path}")
    print(f"wrote {md_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
