#!/usr/bin/env python3
import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

from PIL import Image, ImageChops, ImageDraw, ImageStat


BOX_COLORS = [
    (255, 99, 71, 255),
    (89, 205, 144, 255),
    (84, 160, 255, 255),
    (255, 207, 86, 255),
    (201, 120, 255, 255),
]


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def normalized_rect_to_pixels(rect: list[float], image_size: tuple[int, int]) -> tuple[int, int, int, int]:
    if len(rect) != 4:
        raise ValueError("rect must contain four values")
    width, height = image_size
    x1 = int(round(float(rect[0]) * width))
    y1 = int(round(float(rect[1]) * height))
    x2 = int(round(float(rect[2]) * width))
    y2 = int(round(float(rect[3]) * height))
    x1 = max(0, min(width, x1))
    y1 = max(0, min(height, y1))
    x2 = max(0, min(width, x2))
    y2 = max(0, min(height, y2))
    if x2 <= x1 or y2 <= y1:
        raise ValueError(f"rect does not cover a positive area: {rect}")
    return (x1, y1, x2, y2)


def _as_rgba(image: Image.Image) -> Image.Image:
    return image if image.mode == "RGBA" else image.convert("RGBA")


def rmse(left: Image.Image, right: Image.Image) -> float:
    if left.size != right.size:
        raise ValueError(f"image sizes differ: {left.size} != {right.size}")
    diff = ImageChops.difference(_as_rgba(left), _as_rgba(right))
    stat = ImageStat.Stat(diff)
    square_sum = sum(value * value for value in stat.rms)
    return math.sqrt(square_sum / len(stat.rms))


def _mean_rgba(image: Image.Image) -> list[float]:
    stat = ImageStat.Stat(_as_rgba(image))
    return [round(float(value), 4) for value in stat.mean]


def _overlap_crops(
    left: Image.Image,
    right: Image.Image,
    offset: tuple[int, int],
) -> tuple[Image.Image, Image.Image]:
    if left.size != right.size:
        raise ValueError(f"image sizes differ: {left.size} != {right.size}")
    dx, dy = offset
    width, height = left.size
    left_x1 = max(0, dx)
    left_y1 = max(0, dy)
    left_x2 = min(width, width + dx)
    left_y2 = min(height, height + dy)
    right_x1 = left_x1 - dx
    right_y1 = left_y1 - dy
    right_x2 = left_x2 - dx
    right_y2 = left_y2 - dy
    if left_x2 <= left_x1 or left_y2 <= left_y1:
        raise ValueError(f"offset leaves no overlapping pixels: {offset}")
    return (
        left.crop((left_x1, left_y1, left_x2, left_y2)),
        right.crop((right_x1, right_y1, right_x2, right_y2)),
    )


def _overlap_rmse(
    left: Image.Image,
    right: Image.Image,
    offset: tuple[int, int],
) -> tuple[float, tuple[int, int]]:
    left_crop, right_crop = _overlap_crops(left, right, offset)
    return rmse(left_crop, right_crop), left_crop.size


def translate_image(
    image: Image.Image,
    offset: tuple[int, int],
    *,
    fill: tuple[int, int, int, int] = (0, 0, 0, 0),
) -> Image.Image:
    source = _as_rgba(image)
    dx, dy = offset
    width, height = source.size
    output = Image.new("RGBA", source.size, fill)

    source_x1 = max(0, -dx)
    source_y1 = max(0, -dy)
    dest_x1 = max(0, dx)
    dest_y1 = max(0, dy)
    copy_width = min(width - source_x1, width - dest_x1)
    copy_height = min(height - source_y1, height - dest_y1)
    if copy_width <= 0 or copy_height <= 0:
        return output

    crop = source.crop((source_x1, source_y1, source_x1 + copy_width, source_y1 + copy_height))
    output.alpha_composite(crop, (dest_x1, dest_y1))
    return output


def find_best_integer_alignment(
    windows_image: Image.Image,
    yakkai_image: Image.Image,
    *,
    max_offset: int,
    min_overlap_ratio: float = 0.75,
) -> dict[str, Any]:
    if max_offset < 0:
        raise ValueError("max_offset must be non-negative")
    if windows_image.size != yakkai_image.size:
        raise ValueError(f"image sizes differ: {windows_image.size} != {yakkai_image.size}")

    windows_gray = _as_rgba(windows_image).convert("L")
    yakkai_gray = _as_rgba(yakkai_image).convert("L")
    width, height = windows_gray.size
    min_overlap_area = width * height * min_overlap_ratio
    original_score, original_size = _overlap_rmse(windows_gray, yakkai_gray, (0, 0))
    best_offset = (0, 0)
    best_score = original_score
    best_size = original_size
    searched = 0

    for dy in range(-max_offset, max_offset + 1):
        for dx in range(-max_offset, max_offset + 1):
            overlap_width = width - abs(dx)
            overlap_height = height - abs(dy)
            if overlap_width <= 0 or overlap_height <= 0:
                continue
            if overlap_width * overlap_height < min_overlap_area:
                continue
            searched += 1
            score, overlap_size = _overlap_rmse(windows_gray, yakkai_gray, (dx, dy))
            if score < best_score:
                best_offset = (dx, dy)
                best_score = score
                best_size = overlap_size

    return {
        "enabled": True,
        "maxOffset": max_offset,
        "minOverlapRatio": min_overlap_ratio,
        "searchedOffsets": searched,
        "offset": [best_offset[0], best_offset[1]],
        "originalRmse": round(original_score, 6),
        "alignedRmse": round(best_score, 6),
        "overlapSize": [best_size[0], best_size[1]],
    }


def detect_bright_feature_bounds(
    image: Image.Image,
    *,
    min_luma: float = 190.0,
    min_alpha: int = 1,
) -> dict[str, Any]:
    source = _as_rgba(image)
    width, height = source.size
    min_x = width
    min_y = height
    max_x = -1
    max_y = -1
    count = 0
    pixels = source.load()
    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
            if a >= min_alpha and luma >= min_luma:
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)
                count += 1

    if count == 0:
        return {
            "pixelRect": [],
            "size": [0, 0],
            "center": [],
            "coverage": 0.0,
        }

    rect = [min_x, min_y, max_x + 1, max_y + 1]
    feature_width = rect[2] - rect[0]
    feature_height = rect[3] - rect[1]
    return {
        "pixelRect": rect,
        "size": [feature_width, feature_height],
        "center": [
            round(rect[0] + feature_width / 2.0, 4),
            round(rect[1] + feature_height / 2.0, 4),
        ],
        "coverage": round(count / float(width * height), 6) if width and height else 0.0,
    }


def _rect_from_region(
    region: dict[str, Any],
    key: str,
    image_size: tuple[int, int],
) -> tuple[int, int, int, int]:
    rect = region.get(key)
    if not isinstance(rect, list):
        raise ValueError(f"region {region.get('name', '<unnamed>')} missing {key}")
    return normalized_rect_to_pixels([float(value) for value in rect], image_size)


def compare_region(
    windows_image: Image.Image,
    yakkai_image: Image.Image,
    region: dict[str, Any],
) -> dict[str, Any]:
    windows_rect = _rect_from_region(region, "windowsRect", windows_image.size)
    yakkai_rect = _rect_from_region(region, "yakkaiRect", yakkai_image.size)
    windows_crop = _as_rgba(windows_image).crop(windows_rect)
    yakkai_crop = _as_rgba(yakkai_image).crop(yakkai_rect)
    if windows_crop.size != yakkai_crop.size:
        yakkai_crop = yakkai_crop.resize(windows_crop.size, Image.Resampling.BICUBIC)

    result = {
        "name": str(region.get("name", "unnamed")),
        "purpose": str(region.get("purpose", "")),
        "windowsPixelRect": list(windows_rect),
        "yakkaiPixelRect": list(yakkai_rect),
        "size": list(windows_crop.size),
        "rmse": round(rmse(windows_crop, yakkai_crop), 6),
        "windowsMeanRgba": _mean_rgba(windows_crop),
        "yakkaiMeanRgba": _mean_rgba(yakkai_crop),
    }
    samples = []
    for sample in region.get("sampleRects", []):
        if not isinstance(sample, dict):
            continue
        sample_windows_rect = _rect_from_region(sample, "windowsRect", windows_image.size)
        sample_yakkai_rect = _rect_from_region(sample, "yakkaiRect", yakkai_image.size)
        sample_windows = _as_rgba(windows_image).crop(sample_windows_rect)
        sample_yakkai = _as_rgba(yakkai_image).crop(sample_yakkai_rect)
        if sample_windows.size != sample_yakkai.size:
            sample_yakkai = sample_yakkai.resize(sample_windows.size, Image.Resampling.BICUBIC)
        windows_mean = _mean_rgba(sample_windows)
        yakkai_mean = _mean_rgba(sample_yakkai)
        samples.append(
            {
                "name": str(sample.get("name", "sample")),
                "purpose": str(sample.get("purpose", "")),
                "windowsPixelRect": list(sample_windows_rect),
                "yakkaiPixelRect": list(sample_yakkai_rect),
                "size": list(sample_windows.size),
                "rmse": round(rmse(sample_windows, sample_yakkai), 6),
                "windowsMeanRgba": windows_mean,
                "yakkaiMeanRgba": yakkai_mean,
                "meanDeltaRgba": [
                    round(yakkai_mean[index] - windows_mean[index], 4)
                    for index in range(min(len(windows_mean), len(yakkai_mean)))
                ],
            }
        )
    if samples:
        result["samples"] = samples
    feature_config = region.get("featureDetection")
    if isinstance(feature_config, dict) and feature_config.get("kind") == "bright":
        min_luma = float(feature_config.get("minLuma", 190.0))
        min_alpha = int(feature_config.get("minAlpha", 1))
        windows_bounds = detect_bright_feature_bounds(windows_crop, min_luma=min_luma, min_alpha=min_alpha)
        yakkai_bounds = detect_bright_feature_bounds(yakkai_crop, min_luma=min_luma, min_alpha=min_alpha)
        result["windowsFeatureBounds"] = windows_bounds
        result["yakkaiFeatureBounds"] = yakkai_bounds
        if windows_bounds["center"] and yakkai_bounds["center"]:
            result["featureDelta"] = {
                "center": [
                    round(yakkai_bounds["center"][0] - windows_bounds["center"][0], 4),
                    round(yakkai_bounds["center"][1] - windows_bounds["center"][1], 4),
                ],
                "size": [
                    round(yakkai_bounds["size"][0] - windows_bounds["size"][0], 4),
                    round(yakkai_bounds["size"][1] - windows_bounds["size"][1], 4),
                ],
                "coverageRatio": round(
                    (yakkai_bounds["coverage"] / windows_bounds["coverage"])
                    if windows_bounds["coverage"] > 0
                    else 0.0,
                    6,
                ),
            }
        else:
            result["featureDelta"] = {"center": [], "size": [], "coverageRatio": 0.0}
    return result


def load_text_diagnostics(path: Path | None) -> list[dict[str, Any]]:
    if path is None:
        return []
    data = load_json(path)
    rows: list[dict[str, Any]] = []
    for item in data.get("texts", []):
        if not isinstance(item, dict):
            continue
        text = str(item.get("text", ""))
        if not text.strip():
            continue
        if item.get("status") != "crop-written":
            continue
        rows.append(
            {
                "layerId": item.get("layerId"),
                "layerName": item.get("layerName", ""),
                "text": text,
                "cropRect": item.get("cropRect", []),
                "worldBounds": item.get("worldBounds", []),
                "cardSize": item.get("cardSize", []),
            }
        )
    rows.sort(key=lambda row: (int(row.get("layerId") or 0), str(row.get("layerName", ""))))
    return rows


def _difference_image(left: Image.Image, right: Image.Image) -> Image.Image:
    diff = ImageChops.difference(_as_rgba(left).convert("RGB"), _as_rgba(right).convert("RGB"))
    boosted = diff.point(lambda value: min(255, value * 3))
    return boosted.convert("RGBA")


def _draw_label(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str) -> None:
    x, y = xy
    label = text[:90]
    draw.rectangle((x - 2, y - 1, x + max(80, len(label) * 7), y + 12), fill=(10, 14, 18, 210))
    draw.text((x, y), label, fill=(240, 244, 248, 255))


def _overlay_regions(
    image: Image.Image,
    regions: list[dict[str, Any]],
    rect_key: str,
    title: str,
) -> Image.Image:
    output = _as_rgba(image).copy()
    draw = ImageDraw.Draw(output, "RGBA")
    _draw_label(draw, (8, 8), title)
    for index, region in enumerate(regions):
        color = BOX_COLORS[index % len(BOX_COLORS)]
        rect = _rect_from_region(region, rect_key, output.size)
        draw.rectangle(rect, outline=color, width=2)
        _draw_label(draw, (rect[0] + 4, rect[1] + 4), str(region.get("name", "region")))
    return output


def _make_panel(image: Image.Image, title: str, width: int) -> Image.Image:
    source = _as_rgba(image)
    scale = min(1.0, width / source.width)
    resized = source
    if scale < 1.0:
        resized = source.resize((int(source.width * scale), int(source.height * scale)), Image.Resampling.LANCZOS)
    pad = 12
    label_height = 24
    panel = Image.new("RGBA", (width + pad * 2, resized.height + label_height + pad * 2), (18, 22, 27, 255))
    draw = ImageDraw.Draw(panel, "RGBA")
    _draw_label(draw, (pad, pad), title)
    panel.alpha_composite(resized, (pad, pad + label_height))
    return panel


def write_contact_sheet(
    windows_image: Image.Image,
    yakkai_image: Image.Image,
    regions: list[dict[str, Any]],
    output: Path,
) -> None:
    panels = [
        _make_panel(windows_image, "Windows reference", 680),
        _make_panel(yakkai_image, "Yakkai candidate", 680),
        _make_panel(_difference_image(windows_image, yakkai_image), "Absolute difference x3", 680),
        _make_panel(_overlay_regions(windows_image, regions, "windowsRect", "Windows regions"), "Windows regions", 680),
        _make_panel(_overlay_regions(yakkai_image, regions, "yakkaiRect", "Yakkai regions"), "Yakkai regions", 680),
    ]
    width = max(panel.width for panel in panels)
    height = sum(panel.height for panel in panels)
    sheet = Image.new("RGBA", (width, height), (12, 16, 20, 255))
    y = 0
    for panel in panels:
        sheet.alpha_composite(panel, (0, y))
        y += panel.height
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)


def build_report(
    *,
    title: str,
    windows_reference: Path,
    yakkai_candidate: Path,
    regions_json: Path,
    yakkai_diagnostics_json: Path | None = None,
    dominant_mismatch: str = "",
    review_note: str = "",
    align_search_radius: int = 0,
) -> dict[str, Any]:
    with Image.open(windows_reference) as windows_source:
        windows_image = _as_rgba(windows_source).copy()
    with Image.open(yakkai_candidate) as yakkai_source:
        yakkai_image = _as_rgba(yakkai_source).copy()
    if windows_image.size != yakkai_image.size:
        raise ValueError(f"image sizes differ: {windows_image.size} != {yakkai_image.size}")
    alignment: dict[str, Any] = {"enabled": False}
    comparison_image = yakkai_image
    if align_search_radius > 0:
        alignment = find_best_integer_alignment(
            windows_image,
            yakkai_image,
            max_offset=align_search_radius,
        )
        offset = alignment.get("offset", [0, 0])
        comparison_image = translate_image(yakkai_image, (int(offset[0]), int(offset[1])))
    whole_canvas_rmse = round(rmse(windows_image, comparison_image), 6)
    whole_rmse = (
        float(alignment.get("alignedRmse", whole_canvas_rmse))
        if alignment.get("enabled")
        else whole_canvas_rmse
    )

    region_config = load_json(regions_json)
    regions = region_config.get("regions", [])
    if not isinstance(regions, list) or not regions:
        raise ValueError("regions-json must contain a non-empty regions array")

    region_metrics = [compare_region(windows_image, comparison_image, region) for region in regions if isinstance(region, dict)]
    return {
        "title": title,
        "windowsReference": str(windows_reference),
        "yakkaiCandidate": str(yakkai_candidate),
        "comparisonCandidate": "aligned-yakkai" if alignment.get("enabled") else "original-yakkai",
        "regionsJson": str(regions_json),
        "yakkaiDiagnosticsJson": str(yakkai_diagnostics_json) if yakkai_diagnostics_json else "",
        "imageSize": list(windows_image.size),
        "wholeRmse": round(whole_rmse, 6),
        "wholeCanvasRmse": whole_canvas_rmse,
        "alignment": alignment,
        "regions": region_metrics,
        "textDiagnostics": load_text_diagnostics(yakkai_diagnostics_json),
        "dominantMismatch": dominant_mismatch,
        "reviewNote": review_note,
    }


def _write_markdown(report: dict[str, Any], output: Path) -> None:
    lines = [
        f"# {report.get('title', 'Media Widget Parity')}",
        "",
        f"- Windows reference: `{report.get('windowsReference', '')}`",
        f"- Yakkai candidate: `{report.get('yakkaiCandidate', '')}`",
        f"- Comparison candidate: `{report.get('comparisonCandidate', '')}`",
        f"- Regions: `{report.get('regionsJson', '')}`",
        f"- Yakkai diagnostics: `{report.get('yakkaiDiagnosticsJson', '')}`",
        f"- Image size: `{report.get('imageSize', [])}`",
        f"- Whole-crop RMSE: `{report.get('wholeRmse', 0.0)}`",
        f"- Whole-canvas RMSE: `{report.get('wholeCanvasRmse', report.get('wholeRmse', 0.0))}`",
        "",
        "## Region Metrics",
        "",
        "| region | RMSE | Windows mean RGBA | Yakkai mean RGBA | Windows rect | Yakkai rect | purpose |",
        "| --- | ---: | --- | --- | --- | --- | --- |",
    ]
    for region in report.get("regions", []):
        lines.append(
            f"| `{region.get('name', '')}` | `{region.get('rmse', '')}` | "
            f"`{region.get('windowsMeanRgba', [])}` | `{region.get('yakkaiMeanRgba', [])}` | "
            f"`{region.get('windowsPixelRect', [])}` | `{region.get('yakkaiPixelRect', [])}` | "
            f"{str(region.get('purpose', '')).replace('|', '\\|')} |"
        )
    sample_rows: list[tuple[str, dict[str, Any]]] = []
    for region in report.get("regions", []):
        for sample in region.get("samples", []):
            if isinstance(sample, dict):
                sample_rows.append((str(region.get("name", "")), sample))
    if sample_rows:
        lines.extend(
            [
                "",
                "## Sample Metrics",
                "",
                "| region | sample | RMSE | mean delta RGBA | Windows rect | Yakkai rect | purpose |",
                "| --- | --- | ---: | --- | --- | --- | --- |",
            ]
        )
        for region_name, sample in sample_rows:
            lines.append(
                f"| `{region_name}` | `{sample.get('name', '')}` | `{sample.get('rmse', '')}` | "
                f"`{sample.get('meanDeltaRgba', [])}` | `{sample.get('windowsPixelRect', [])}` | "
                f"`{sample.get('yakkaiPixelRect', [])}` | "
                f"{str(sample.get('purpose', '')).replace('|', '\\|')} |"
            )

    alignment = report.get("alignment", {})
    if isinstance(alignment, dict) and alignment.get("enabled"):
        lines.extend(
            [
                "",
                "## Alignment",
                "",
                f"- Applied Yakkai offset: `{alignment.get('offset', [])}`",
                f"- Search radius: `{alignment.get('maxOffset', '')}`",
                f"- Searched offsets: `{alignment.get('searchedOffsets', '')}`",
                f"- Original overlap RMSE: `{alignment.get('originalRmse', '')}`",
                f"- Aligned overlap RMSE: `{alignment.get('alignedRmse', '')}`",
                f"- Overlap size: `{alignment.get('overlapSize', [])}`",
                f"- Aligned artifact: `{report.get('alignedYakkaiCandidate', '')}`",
            ]
        )

    lines.extend(
        [
            "",
            "## Yakkai Generated Text",
            "",
            "| layer | text | crop rect | card size | world bounds |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    diagnostics = report.get("textDiagnostics", [])
    if diagnostics:
        for row in diagnostics:
            layer = f"{row.get('layerId', '')} {row.get('layerName', '')}".strip()
            text = str(row.get("text", "")).replace("|", "\\|")
            lines.append(
                f"| `{layer}` | {text} | `{row.get('cropRect', [])}` | "
                f"`{row.get('cardSize', [])}` | `{row.get('worldBounds', [])}` |"
            )
    else:
        lines.append("|  | No generated-text diagnostics supplied. |  |  |  |")

    if report.get("dominantMismatch") or report.get("reviewNote"):
        lines.extend(
            [
                "",
                "## Review Classification",
                "",
                f"- Dominant mismatch: `{report.get('dominantMismatch', '')}`",
                f"- Review note: {str(report.get('reviewNote', '')).replace('|', '\\|')}",
            ]
        )

    lines.extend(
        [
            "",
            "## Interpretation Checklist",
            "",
            "- High `album-art` drift points at thumbnail scaling, crop mode, color extraction, or blur handling.",
            "- High `text-block` drift with close card/album metrics points at text anchoring, scale, shadow offset, or font metrics.",
            "- High `card-background` drift points at background tint, opacity, rounded-corner mask, or fade state.",
            "- High whole-crop drift with all named regions high can mean the crop window itself or unrelated scene rendering drift is dominating.",
            "- If bounds look close but text still differs, treat Qt text rendering as an approximation and investigate glyph/raster parity last.",
        ]
    )
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_outputs(
    report: dict[str, Any],
    *,
    windows_reference: Path,
    yakkai_candidate: Path,
    regions_json: Path,
    output_dir: Path,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    with Image.open(windows_reference) as windows_source:
        windows_image = _as_rgba(windows_source).copy()
    with Image.open(yakkai_candidate) as yakkai_source:
        yakkai_image = _as_rgba(yakkai_source).copy()
    comparison_image = yakkai_image
    alignment = report.get("alignment", {})
    if isinstance(alignment, dict) and alignment.get("enabled"):
        offset = alignment.get("offset", [0, 0])
        comparison_image = translate_image(yakkai_image, (int(offset[0]), int(offset[1])))
        aligned_path = output_dir / "yakkai-candidate-aligned.png"
        comparison_image.save(aligned_path)
        report["alignedYakkaiCandidate"] = str(aligned_path)
    region_config = load_json(regions_json)
    regions = [region for region in region_config.get("regions", []) if isinstance(region, dict)]

    write_contact_sheet(windows_image, comparison_image, regions, output_dir / "contact-sheet.png")
    (output_dir / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    _write_markdown(report, output_dir / "summary.md")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compare Windows WE and Yakkai media-widget crops.")
    parser.add_argument("--windows-reference", type=Path, required=True)
    parser.add_argument("--yakkai-candidate", type=Path, required=True)
    parser.add_argument("--regions-json", type=Path, required=True)
    parser.add_argument("--yakkai-diagnostics-json", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--title", default="Media widget parity")
    parser.add_argument("--dominant-mismatch", default="")
    parser.add_argument("--review-note", default="")
    parser.add_argument(
        "--align-search-radius",
        type=int,
        default=0,
        help="Search +/-N pixels for the best integer Yakkai translation before reporting metrics.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        report = build_report(
            title=args.title,
            windows_reference=args.windows_reference,
            yakkai_candidate=args.yakkai_candidate,
            regions_json=args.regions_json,
            yakkai_diagnostics_json=args.yakkai_diagnostics_json,
            dominant_mismatch=args.dominant_mismatch,
            review_note=args.review_note,
            align_search_radius=args.align_search_radius,
        )
        write_outputs(
            report,
            windows_reference=args.windows_reference,
            yakkai_candidate=args.yakkai_candidate,
            regions_json=args.regions_json,
            output_dir=args.output_dir,
        )
    except Exception as exc:
        print(f"media_widget_parity_compare: {exc}", file=sys.stderr)
        return 2

    print(f"Wrote {args.output_dir / 'summary.json'}")
    print(f"Wrote {args.output_dir / 'summary.md'}")
    print(f"Wrote {args.output_dir / 'contact-sheet.png'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
