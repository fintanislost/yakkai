#!/usr/bin/env python3
import argparse
import json
import math
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def _scene_ortho(manifest: dict[str, Any]) -> tuple[float, float]:
    value = manifest.get("sceneOrtho")
    if not isinstance(value, list) or len(value) < 2:
        raise ValueError("manifest does not contain sceneOrtho")
    width = float(value[0])
    height = float(value[1])
    if width <= 0.0 or height <= 0.0:
        raise ValueError("manifest sceneOrtho dimensions must be positive")
    return width, height


def _world_bounds(value: Any) -> tuple[float, float, float, float] | None:
    if not isinstance(value, list) or len(value) < 4:
        return None
    try:
        bounds = (float(value[0]), float(value[1]), float(value[2]), float(value[3]))
    except (TypeError, ValueError):
        return None
    if not all(math.isfinite(component) for component in bounds):
        return None
    return bounds


def world_bounds_to_crop_rect(
    world_bounds: tuple[float, float, float, float],
    image_size: tuple[int, int],
    scene_ortho: tuple[float, float],
    *,
    padding: int = 8,
) -> list[int] | None:
    min_x, min_y, max_x, max_y = world_bounds
    image_width, image_height = image_size
    ortho_width, ortho_height = scene_ortho
    if max_x <= min_x or max_y <= min_y:
        return None

    x1 = min_x / ortho_width * image_width
    x2 = max_x / ortho_width * image_width
    y1 = (ortho_height - max_y) / ortho_height * image_height
    y2 = (ortho_height - min_y) / ortho_height * image_height

    rect = [
        max(0, int(math.floor(x1)) - padding),
        max(0, int(math.floor(y1)) - padding),
        min(image_width, int(math.ceil(x2)) + padding),
        min(image_height, int(math.ceil(y2)) + padding),
    ]
    if rect[2] <= rect[0] or rect[3] <= rect[1]:
        return None
    return rect


def _safe_name(layer: dict[str, Any]) -> str:
    raw = f"{layer.get('layerId', 'unknown')}_{layer.get('layerName', 'unnamed')}"
    safe = "".join(ch if ch.isalnum() else "_" for ch in raw)
    while "__" in safe:
        safe = safe.replace("__", "_")
    return safe.strip("_") or "unnamed"


def build_report(manifest_path: Path, capture_path: Path, output_dir: Path) -> dict[str, Any]:
    manifest = load_json(manifest_path)
    scene_ortho = _scene_ortho(manifest)
    with Image.open(capture_path) as image:
        image_size = image.size

    texts: list[dict[str, Any]] = []
    for item in as_list(manifest.get("generatedTextDiagnostics")):
        if not isinstance(item, dict):
            continue
        bounds = _world_bounds(item.get("worldBounds"))
        crop_rect = None
        status = "not-visible"
        if item.get("visibility") == "visible-in-frame" and bounds is not None:
            crop_rect = world_bounds_to_crop_rect(bounds, image_size, scene_ortho)
            status = "crop-pending" if crop_rect else "outside-capture"

        crop_path = output_dir / "crops" / f"{_safe_name(item)}.png"
        texts.append({
            "layerId": item.get("layerId"),
            "layerName": item.get("layerName", ""),
            "text": item.get("text", ""),
            "textureName": item.get("textureName", ""),
            "font": item.get("font", ""),
            "rasterizer": item.get("rasterizer", ""),
            "fontLoaded": bool(item.get("fontLoaded", False)),
            "fontFamily": item.get("fontFamily", ""),
            "fontLoadStatus": item.get("fontLoadStatus", ""),
            "horizontalAlign": item.get("horizontalAlign", ""),
            "verticalAlign": item.get("verticalAlign", ""),
            "pointSize": item.get("pointSize"),
            "effectivePixelSize": item.get("effectivePixelSize"),
            "parentId": item.get("parentId"),
            "parentChain": as_list(item.get("parentChain")),
            "cardSize": item.get("cardSize", item.get("textCardSize", [])),
            "textureSize": item.get("textureSize", []),
            "visibility": item.get("visibility", ""),
            "classificationReason": item.get("classificationReason", ""),
            "worldBounds": item.get("worldBounds", []),
            "alphaBounds": item.get("alphaBounds", []),
            "cropRect": crop_rect,
            "cropPath": str(crop_path),
            "status": status,
        })

    return {
        "manifest": str(manifest_path),
        "capture": str(capture_path),
        "outputDir": str(output_dir),
        "sceneId": manifest.get("sceneId", ""),
        "sceneOrtho": list(scene_ortho),
        "captureSize": list(image_size),
        "texts": texts,
    }


def _draw_label(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str) -> None:
    draw.text(xy, text[:96], fill=(235, 238, 242))


def write_contact_sheet(report: dict[str, Any], output: Path) -> bool:
    crop_items = [
        item for item in report.get("texts", [])
        if isinstance(item, dict) and item.get("status") == "crop-written"
    ]
    if not crop_items:
        return False

    thumb_width = 240
    label_height = 42
    pad = 12
    rows = []
    for item in crop_items:
        path = Path(str(item["cropPath"]))
        with Image.open(path) as crop:
            thumb = crop.convert("RGBA")
            thumb.thumbnail((thumb_width, 160), Image.Resampling.LANCZOS)
            rows.append((item, thumb.copy()))

    row_height = max(60, max(thumb.height for _, thumb in rows) + label_height + pad)
    sheet = Image.new(
        "RGBA",
        (thumb_width + pad * 2, row_height * len(rows) + pad),
        (18, 22, 27, 255),
    )
    draw = ImageDraw.Draw(sheet)
    y = pad
    for item, thumb in rows:
        layer_id = item.get("layerId", "")
        layer_name = item.get("layerName", "")
        label = f"{layer_id} {layer_name}"
        _draw_label(draw, (pad, y), label)
        _draw_label(draw, (pad, y + 18), str(item.get("text", "")))
        sheet.alpha_composite(thumb, (pad, y + label_height))
        y += row_height

    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)
    return True


def _write_markdown(report: dict[str, Any], output: Path) -> None:
    lines = [
        "# Media Text Diagnostics",
        "",
        f"- Scene: `{report.get('sceneId', '')}`",
        f"- Manifest: `{report.get('manifest', '')}`",
        f"- Capture: `{report.get('capture', '')}`",
        f"- Scene ortho: `{report.get('sceneOrtho', [])}`",
        f"- Capture size: `{report.get('captureSize', [])}`",
        "",
        "| layer | status | visibility | renderer | font | align | size px | card | texture | crop | text |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for item in report.get("texts", []):
        if not isinstance(item, dict):
            continue
        layer = f"{item.get('layerId', '')} {item.get('layerName', '')}".strip()
        crop = item.get("cropPath", "") if item.get("status") == "crop-written" else ""
        renderer = f"{item.get('rasterizer', '')}/{item.get('fontLoadStatus', '')}"
        font = str(item.get("fontFamily", "") or item.get("font", "")).replace("|", "\\|")
        align = f"{item.get('horizontalAlign', '')}/{item.get('verticalAlign', '')}"
        size = f"{item.get('pointSize', '')}/{item.get('effectivePixelSize', '')}"
        card = str(item.get("cardSize", []))
        texture = str(item.get("textureSize", []))
        text = str(item.get("text", "")).replace("|", "\\|")
        lines.append(
            f"| `{layer}` | `{item.get('status', '')}` | "
            f"`{item.get('visibility', '')}` | `{renderer}` | {font} | "
            f"`{align}` | `{size}` | `{card}` | `{texture}` | `{crop}` | {text} |"
        )
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_outputs(report: dict[str, Any], output_dir: Path) -> dict[str, str]:
    output_dir.mkdir(parents=True, exist_ok=True)
    capture_path = Path(str(report["capture"]))
    with Image.open(capture_path) as image:
        source = image.convert("RGBA")
        for item in report.get("texts", []):
            if not isinstance(item, dict) or not item.get("cropRect"):
                continue
            crop_path = Path(str(item["cropPath"]))
            crop_path.parent.mkdir(parents=True, exist_ok=True)
            crop = source.crop(tuple(item["cropRect"]))
            crop.save(crop_path)
            item["status"] = "crop-written"

    outputs = {
        "json": str(output_dir / "media-text-diagnostics.json"),
        "markdown": str(output_dir / "media-text-diagnostics.md"),
    }
    contact_sheet = output_dir / "media-text-contact-sheet.png"
    if write_contact_sheet(report, contact_sheet):
        outputs["contactSheet"] = str(contact_sheet)

    Path(outputs["json"]).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    _write_markdown(report, Path(outputs["markdown"]))
    return outputs


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create crops and a contact sheet for generated media text diagnostics."
    )
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    report = build_report(args.manifest, args.capture, args.output_dir)
    outputs = write_outputs(report, args.output_dir)
    print(f"Report: {outputs['json']}")
    print(f"Markdown: {outputs['markdown']}")
    if "contactSheet" in outputs:
        print(f"Contact sheet: {outputs['contactSheet']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
