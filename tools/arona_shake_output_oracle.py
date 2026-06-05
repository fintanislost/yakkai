#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Iterable

import numpy as np
from PIL import Image, ImageDraw


def flow_mask_from_rg(rg: np.ndarray) -> np.ndarray:
    pixels = np.asarray(rg, dtype=np.float32)
    if pixels.shape[-1] < 2:
        raise ValueError("flow mask input must have at least two channels")
    return (pixels[..., :2] - np.float32(0.498)) * np.float32(2.0)


def shake_offset_at_time(
    time_seconds: float,
    speed: float,
    bounds: tuple[float, float],
    friction: tuple[float, float],
    time_offset: float = 0.0,
) -> float:
    time_value = speed * time_seconds + time_offset
    two_pi = math.tau
    phase = (time_value / two_pi) % 1.0
    offset = math.sin(phase * two_pi) * 0.498 + 0.5
    if math.cos(time_value) >= 0.0:
        offset = math.pow(offset, friction[1])
    else:
        offset = 1.0 - math.pow(1.0 - offset, friction[0])

    low, high = bounds
    if high != low:
        offset = min(max((offset - low) / (high - low), 0.0), 1.0)
    return offset * 2.0 - 1.0


def shake_texcoord_offset(offset: float, amp: float, flow: np.ndarray) -> np.ndarray:
    return np.asarray(flow, dtype=np.float32) * np.float32(offset * amp * amp)


def preserve_source_alpha(displaced: np.ndarray, source: np.ndarray) -> np.ndarray:
    displaced_pixels = np.asarray(displaced, dtype=np.float32)
    source_pixels = np.asarray(source, dtype=np.float32)
    if displaced_pixels.shape != source_pixels.shape or displaced_pixels.shape[-1] < 4:
        raise ValueError("displaced and source must be matching RGBA arrays")

    out = displaced_pixels.copy()
    source_alpha = source_pixels[..., 3:4]
    displaced_alpha = displaced_pixels[..., 3:4]
    coverage = np.clip(displaced_alpha / np.maximum(source_alpha, np.float32(1.0e-4)), 0.0, 1.0)
    out[..., :3] = source_pixels[..., :3] * (1.0 - coverage) + displaced_pixels[..., :3] * coverage
    out[..., 3:4] = source_alpha
    return out


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


def capture_path_for_stage(manifest: dict, layer_id: int, stage: str) -> str:
    record = layer_record_for_stage(manifest, layer_id, stage)
    path = record.get("path")
    if not path:
        raise KeyError(f"capture path not found for layer {layer_id} stage {stage}")
    return str(path)


def capture_time_seconds(manifest: dict) -> float:
    if manifest.get("shaderTimeSeconds") is not None:
        return float(manifest["shaderTimeSeconds"])
    return float(manifest.get("captureDelayMs", 0)) / 1000.0


def layer_record_for_stage(manifest: dict, layer_id: int, stage: str) -> dict:
    for record in manifest.get("captures", []):
        layer = record.get("layer", {})
        if record.get("stage") == stage and layer.get("layerId", record.get("layerId")) == layer_id:
            return record
    raise KeyError(f"capture not found for layer {layer_id} stage {stage}")


def select_effect_material(manifest: dict, layer_id: int, effect_index: int) -> dict:
    record = layer_record_for_stage(manifest, layer_id, "effect-input")
    for material in record.get("layer", {}).get("effectMaterials", []):
        if material.get("effectIndex") == effect_index:
            return material
    raise KeyError(f"effect material {effect_index} not found for layer {layer_id}")


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
    padding: int = 64,
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


def uv_grid(width: int, height: int) -> np.ndarray:
    xs = (np.arange(width, dtype=np.float32) + np.float32(0.5)) / np.float32(width)
    ys = (np.arange(height, dtype=np.float32) + np.float32(0.5)) / np.float32(height)
    u, v = np.meshgrid(xs, ys)
    return np.stack((u, v), axis=2)


def apply_shake_pass(source: np.ndarray, flow_texture: np.ndarray, offset: float, amp: float) -> np.ndarray:
    height, width = source.shape[:2]
    uv = uv_grid(width, height)
    sampled_flow = sample_bilinear_rgba(flow_texture, uv)[..., :2]
    flow = flow_mask_from_rg(sampled_flow)
    texcoord_offset = shake_texcoord_offset(offset, amp, flow)
    return sample_bilinear_rgba(source, uv + texcoord_offset)


def apply_shake_pass_crop(
    source: np.ndarray,
    flow_texture: np.ndarray,
    crop: tuple[int, int, int, int],
    offset: float,
    amp: float,
) -> np.ndarray:
    height, width = source.shape[:2]
    x0, y0, x1, y1 = crop
    xs = (np.arange(x0, x1, dtype=np.float32) + np.float32(0.5)) / np.float32(width)
    ys = (np.arange(y0, y1, dtype=np.float32) + np.float32(0.5)) / np.float32(height)
    u, v = np.meshgrid(xs, ys)
    uv = np.stack((u, v), axis=2)
    sampled_flow = sample_bilinear_rgba(flow_texture, uv)[..., :2]
    flow = flow_mask_from_rg(sampled_flow)
    texcoord_offset = shake_texcoord_offset(offset, amp, flow)
    return sample_bilinear_rgba(source, uv + texcoord_offset)


def offset_sweep(
    source: np.ndarray,
    gpu: np.ndarray,
    flow_texture: np.ndarray,
    crop: tuple[int, int, int, int],
    amp: float,
    offsets: Iterable[float],
) -> list[dict]:
    x0, y0, x1, y1 = crop
    source_crop = source[y0:y1, x0:x1]
    gpu_crop = gpu[y0:y1, x0:x1]
    results: list[dict] = []
    for offset in offsets:
        displaced = apply_shake_pass_crop(source, flow_texture, crop, float(offset), amp)
        patched = preserve_source_alpha(displaced, source_crop)
        metrics = image_metrics(patched, gpu_crop)
        results.append({
            "offset": round(float(offset), 6),
            "rmse": metrics["rmse"],
            "rgbRmse": metrics["rgbRmse"],
            "alphaRmse": metrics["alphaRmse"],
            "visibleIou": metrics["visibleIou"],
        })
    return sorted(results, key=lambda item: item["rmse"])


def load_rgba(path: Path) -> np.ndarray:
    with Image.open(path).convert("RGBA") as image:
        return np.asarray(image, dtype=np.float32) / np.float32(255.0)


def save_rgba(path: Path, pixels: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    out = (np.clip(pixels, 0.0, 1.0) * 255.0).round().astype(np.uint8)
    Image.fromarray(out, "RGBA").save(path)


def texture_binding(material: dict, slot: int) -> str:
    for binding in material.get("textureBindings", []):
        if binding.get("slot") == slot:
            resolved = binding.get("resolved")
            if resolved:
                return str(resolved)
    raise KeyError(f"texture slot {slot} not found for effect {material.get('effectIndex')}")


def first_float(values: dict, key: str, default: float) -> float:
    raw = values.get(key)
    if isinstance(raw, list) and raw:
        return float(raw[0])
    if raw is not None:
        return float(raw)
    return default


def first_vec(values: dict, key: str, default: tuple[float, ...]) -> tuple[float, ...]:
    raw = values.get(key)
    if isinstance(raw, list) and raw:
        return tuple(float(item) for item in raw)
    return default


def material_parameters(material: dict, time_seconds: float) -> dict:
    values = material.get("materialValues", {})
    constants = material.get("resolvedConstValues", {})
    combos = {key: str(value) for key, value in material.get("resolvedCombos", {}).items()}

    unsupported = [
        name
        for name in ("AUDIOPROCESSING", "MASK", "NOISE", "TIMEOFFSET")
        if combos.get(name, "0") not in ("0", "")
    ]
    if combos.get("DIRECTION", "0") != "0":
        unsupported.append("DIRECTION")
    if unsupported:
        raise NotImplementedError(f"unsupported shake combos for CPU oracle: {unsupported}")

    amp = first_float(constants, "g_Amp", first_float(values, "strength", 0.0))
    speed = first_float(constants, "g_Speed", first_float(values, "speed", 1.0))
    bounds = first_vec(constants, "g_Bounds", first_vec(values, "bounds", (0.0, 1.0)))
    friction = first_vec(constants, "g_Friction", first_vec(values, "friction", (1.0, 1.0)))
    if len(bounds) < 2 or len(friction) < 2:
        raise ValueError("shake material bounds/friction must have two components")
    offset = shake_offset_at_time(
        time_seconds=time_seconds,
        speed=speed,
        bounds=(bounds[0], bounds[1]),
        friction=(friction[0], friction[1]),
    )
    return {
        "amp": amp,
        "speed": speed,
        "bounds": [bounds[0], bounds[1]],
        "friction": [friction[0], friction[1]],
        "offset": offset,
        "combos": combos,
    }


def image_metrics(expected: np.ndarray, actual: np.ndarray, crop: tuple[int, int, int, int] | None = None) -> dict:
    left = expected
    right = actual
    if crop is not None:
        x0, y0, x1, y1 = crop
        left = left[y0:y1, x0:x1]
        right = right[y0:y1, x0:x1]
    if left.shape != right.shape:
        raise ValueError(f"image shapes differ: {left.shape} vs {right.shape}")
    diff = left - right
    alpha_left = left[..., 3] > (1.0 / 255.0)
    alpha_right = right[..., 3] > (1.0 / 255.0)
    union = np.logical_or(alpha_left, alpha_right)
    intersection = np.logical_and(alpha_left, alpha_right)
    return {
        "rmse": round(float(np.sqrt(np.mean(diff * diff))), 8),
        "rgbRmse": round(float(np.sqrt(np.mean(diff[..., :3] * diff[..., :3]))), 8),
        "alphaRmse": round(float(np.sqrt(np.mean(diff[..., 3] * diff[..., 3]))), 8),
        "meanAbs": round(float(np.mean(np.abs(diff))), 8),
        "maxAbs": round(float(np.max(np.abs(diff))), 8),
        "visibleIou": round(float(np.count_nonzero(intersection) / max(np.count_nonzero(union), 1)), 8),
        "shape": list(left.shape),
    }


def diff_preview(expected: np.ndarray, actual: np.ndarray, gain: float = 8.0) -> np.ndarray:
    diff = np.abs(expected - actual)
    out = np.clip(diff * gain, 0.0, 1.0)
    out[..., 3] = 1.0
    return out


def alpha_composite_preview(pixels: np.ndarray, background: tuple[int, int, int] = (32, 36, 42)) -> Image.Image:
    rgba = (np.clip(pixels, 0.0, 1.0) * 255.0).round().astype(np.uint8)
    image = Image.fromarray(rgba, "RGBA")
    bg = Image.new("RGBA", image.size, (*background, 255))
    return Image.alpha_composite(bg, image).convert("RGB")


def write_contact_sheet(
    entries: list[tuple[str, np.ndarray]],
    crop: tuple[int, int, int, int],
    output_path: Path,
    thumb_width: int = 420,
) -> None:
    x0, y0, x1, y1 = crop
    crops: list[tuple[str, Image.Image]] = []
    for label, pixels in entries:
        preview = alpha_composite_preview(pixels[y0:y1, x0:x1])
        ratio = thumb_width / preview.width
        preview = preview.resize((thumb_width, max(1, round(preview.height * ratio))), Image.Resampling.LANCZOS)
        crops.append((label, preview))

    label_height = 24
    gutter = 10
    width = thumb_width * len(crops) + gutter * (len(crops) + 1)
    height = max(image.height for _, image in crops) + label_height + gutter * 2
    sheet = Image.new("RGB", (width, height), (18, 22, 28))
    draw = ImageDraw.Draw(sheet)
    for index, (label, image) in enumerate(crops):
        x = gutter + index * (thumb_width + gutter)
        sheet.paste(image, (x, gutter))
        draw.text((x, gutter + image.height + 4), label, fill=(235, 240, 245))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output_path)


def classify_report(
    full_metrics: dict,
    crop_metrics: dict,
    offset_sweep_results: list[dict] | None = None,
    modeled_offset: float | None = None,
) -> tuple[str, str]:
    patched = full_metrics["cpuPatchedVsGpu"]
    unpatched = full_metrics["cpuUnpatchedVsGpu"]
    patched_crop = crop_metrics["cpuPatchedVsGpu"]
    unpatched_crop = crop_metrics["cpuUnpatchedVsGpu"]
    if offset_sweep_results and modeled_offset is not None:
        best = offset_sweep_results[0]
        if best["rmse"] + 0.01 < patched_crop["rmse"] and abs(best["offset"] - modeled_offset) > 0.05:
            return (
                "effect-time-mismatch",
                "GPU output matches a best-fit shake offset "
                f"`{best['offset']}` far better than the manifest-delay-derived offset "
                f"`{round(modeled_offset, 6)}`; the debug manifest does not record actual shader g_Time.",
            )
    if patched["rmse"] <= 0.01 and patched_crop["rmse"] <= 0.015:
        return (
            "windows-oracle-insufficient",
            "CPU Yakkai-patched shake output matches the GPU boundary closely; remaining parity needs a Windows per-effect oracle.",
        )
    if unpatched["rmse"] + 0.002 < patched["rmse"] or unpatched_crop["rmse"] + 0.002 < patched_crop["rmse"]:
        return (
            "source-alpha-patch-mismatch",
            "GPU output is closer to the unpatched WE alpha behavior than the modeled Yakkai source-alpha patch.",
        )
    if patched["alphaRmse"] > 0.01 or patched_crop["alphaRmse"] > 0.015:
        return (
            "source-alpha-patch-mismatch",
            "Patched CPU output still disagrees with GPU alpha, so the source-alpha behavior needs focused inspection.",
        )
    return (
        "sampler-coordinate-mismatch",
        "Patched CPU alpha is close but RGB differs enough to suspect sampling, coordinates, or shader math.",
    )


def write_markdown(report: dict, path: Path) -> None:
    lines = [
        "# Arona Shake Output Oracle",
        "",
        f"- Classification: `{report['classification']}`",
        f"- Reason: {report['classificationReason']}",
        f"- Layer: `{report['layerId']}`",
        f"- Effect: `{report['effectIndex']}` `{report['material']['shader']}`",
        f"- Capture time: `{report['captureTimeSeconds']}` seconds",
        f"- Ribbon crop: `{report['ribbonCropPixels']}`",
        "",
        "## Parameters",
        "",
        f"- Amp: `{report['parameters']['amp']}`",
        f"- Speed: `{report['parameters']['speed']}`",
        f"- Offset: `{report['parameters']['offset']}`",
        f"- Bounds: `{report['parameters']['bounds']}`",
        f"- Friction: `{report['parameters']['friction']}`",
        f"- Flow texture: `{report['flowTexture']}`",
        "",
        "## Metrics",
        "",
    ]
    for scope, metrics in (("Full layer", report["metrics"]), ("Ribbon crop", report["cropMetrics"])):
        lines.append(f"### {scope}")
        lines.append("")
        for name, values in metrics.items():
            lines.append(
                f"- `{name}`: rmse `{values['rmse']}`, rgb `{values['rgbRmse']}`, "
                f"alpha `{values['alphaRmse']}`, visible IoU `{values['visibleIou']}`"
            )
        lines.append("")
    lines.extend(["## Outputs", ""])
    for label, output in report["outputs"].items():
        lines.append(f"- `{label}`: `{output}`")
    if report.get("offsetSweep"):
        lines.extend(["", "## Offset Sweep", ""])
        for item in report["offsetSweep"][:8]:
            lines.append(
                f"- Offset `{item['offset']}`: rmse `{item['rmse']}`, "
                f"rgb `{item['rgbRmse']}`, alpha `{item['alphaRmse']}`"
            )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_oracle_report(
    prefix_manifest: dict,
    full_manifest: dict,
    scene_pkg: Path,
    layer_id: int,
    effect_index: int,
    output_dir: Path,
) -> dict:
    from arona_mask_effect_parity import decode_tex_image, texture_package_path
    from arona_lut_sampling_lab import PkgIndex

    output_dir.mkdir(parents=True, exist_ok=True)
    prefix_output_path = Path(capture_path_for_stage(prefix_manifest, layer_id, "effect-output"))
    full_output_path = Path(capture_path_for_stage(full_manifest, layer_id, "effect-output"))
    full_record = layer_record_for_stage(full_manifest, layer_id, "effect-input")
    material = select_effect_material(full_manifest, layer_id, effect_index)
    time_seconds = capture_time_seconds(full_manifest)
    params = material_parameters(material, time_seconds)

    package = PkgIndex.from_path(scene_pkg)
    flow_texture = texture_binding(material, 1)
    flow_tex = decode_tex_image(package.read(texture_package_path(flow_texture)))

    source = load_rgba(prefix_output_path)
    gpu = load_rgba(full_output_path)
    if source.shape != gpu.shape:
        raise ValueError(f"prefix/full output shapes differ: {source.shape} vs {gpu.shape}")

    unpatched = apply_shake_pass(source, flow_tex.pixels, offset=params["offset"], amp=params["amp"])
    patched = preserve_source_alpha(unpatched, source)

    image_size = (source.shape[1], source.shape[0])
    coverage = full_record.get("layer", {}).get("publish", {}).get("puppetCutoutSlotCoverage", [])
    crop = slot_union_pixels(coverage, image_size, slots=(3, 13), padding=80)

    outputs = {
        "cpu-unpatched": str(output_dir / "cpu-unpatched.png"),
        "cpu-yakkai-patched": str(output_dir / "cpu-yakkai-patched.png"),
        "gpu-full": str(output_dir / "gpu-full.png"),
        "diff-cpu-patched-vs-gpu": str(output_dir / "diff-cpu-patched-vs-gpu.png"),
        "ribbon-contact-sheet": str(output_dir / "ribbon-contact-sheet.png"),
    }
    save_rgba(Path(outputs["cpu-unpatched"]), unpatched)
    save_rgba(Path(outputs["cpu-yakkai-patched"]), patched)
    save_rgba(Path(outputs["gpu-full"]), gpu)
    save_rgba(Path(outputs["diff-cpu-patched-vs-gpu"]), diff_preview(patched, gpu))
    write_contact_sheet(
        [
            ("prefix10 source", source),
            ("cpu WE unpatched", unpatched),
            ("cpu Yakkai patched", patched),
            ("GPU full11", gpu),
            ("patched diff x8", diff_preview(patched, gpu)),
        ],
        crop,
        Path(outputs["ribbon-contact-sheet"]),
    )

    metrics = {
        "cpuUnpatchedVsGpu": image_metrics(unpatched, gpu),
        "cpuPatchedVsGpu": image_metrics(patched, gpu),
        "cpuUnpatchedVsPatched": image_metrics(unpatched, patched),
        "prefixSourceVsGpu": image_metrics(source, gpu),
    }
    crop_metrics = {
        "cpuUnpatchedVsGpu": image_metrics(unpatched, gpu, crop),
        "cpuPatchedVsGpu": image_metrics(patched, gpu, crop),
        "cpuUnpatchedVsPatched": image_metrics(unpatched, patched, crop),
        "prefixSourceVsGpu": image_metrics(source, gpu, crop),
    }
    sweep = offset_sweep(
        source,
        gpu,
        flow_tex.pixels,
        crop,
        amp=params["amp"],
        offsets=np.linspace(-1.0, 1.0, 81),
    )
    classification, reason = classify_report(metrics, crop_metrics, sweep, params["offset"])
    report = {
        "classification": classification,
        "classificationReason": reason,
        "layerId": layer_id,
        "effectIndex": effect_index,
        "captureTimeSeconds": time_seconds,
        "prefixOutput": str(prefix_output_path),
        "gpuOutput": str(full_output_path),
        "flowTexture": flow_texture,
        "flowTextureFormatId": flow_tex.format_id,
        "flowTextureFilterMode": flow_tex.filter_mode,
        "flowTextureWrapMode": flow_tex.wrap_mode,
        "ribbonCropPixels": list(crop),
        "parameters": params,
        "material": {
            "shader": material.get("shader"),
            "resolvedCombos": material.get("resolvedCombos", {}),
            "authoredCombos": material.get("authoredCombos", {}),
            "materialValues": material.get("materialValues", {}),
            "resolvedConstValues": {
                key: material.get("resolvedConstValues", {}).get(key)
                for key in ("g_Amp", "g_Speed", "g_Bounds", "g_Friction", "g_Texture1Resolution")
            },
            "textureBindings": material.get("textureBindings", []),
        },
        "metrics": metrics,
        "cropMetrics": crop_metrics,
        "offsetSweep": sweep[:12],
        "outputs": outputs,
    }
    json_path = output_dir / "shake-output-oracle.json"
    md_path = output_dir / "shake-output-oracle.md"
    json_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_markdown(report, md_path)
    print(f"wrote {json_path}")
    print(f"wrote {md_path}")
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compare Arona layer 405 final shake pass against a CPU oracle.")
    parser.add_argument("--prefix-manifest", type=Path, required=True)
    parser.add_argument("--full-manifest", type=Path, required=True)
    parser.add_argument("--scene-pkg", type=Path, required=True)
    parser.add_argument("--layer-id", type=int, default=405)
    parser.add_argument("--effect-index", type=int, default=11)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    prefix_manifest = json.loads(args.prefix_manifest.read_text(encoding="utf-8"))
    full_manifest = json.loads(args.full_manifest.read_text(encoding="utf-8"))
    build_oracle_report(
        prefix_manifest=prefix_manifest,
        full_manifest=full_manifest,
        scene_pkg=args.scene_pkg,
        layer_id=args.layer_id,
        effect_index=args.effect_index,
        output_dir=args.output_dir,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
