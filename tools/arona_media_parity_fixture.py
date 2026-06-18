#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path
from typing import Any

from PIL import Image

import media_widget_parity_compare as parity_compare


DEFAULT_FIXTURE = Path("native/scene_harness/tests/fixtures/media/arona_media_parity.json")
DEFAULT_REFERENCE_ROOT = Path("yakkai-reference/forMedia")


def load_fixture(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("fixture root must be an object")
    if not str(data.get("sceneId", "")).strip():
        raise ValueError("fixture missing sceneId")
    _int_pair(data.get("renderSize"), "renderSize")
    references = data.get("references")
    if not isinstance(references, list) or not references:
        raise ValueError("fixture references must be a non-empty array")
    for index, reference in enumerate(references):
        if not isinstance(reference, dict):
            raise ValueError(f"reference {index} must be an object")
        if not str(reference.get("name", "")).strip():
            raise ValueError(f"reference {index} missing name")
        if not str(reference.get("reference", "")).strip():
            raise ValueError(f"reference {index} missing reference path")
        _rect(reference.get("candidateCropRect"), f"reference {reference.get('name')} candidateCropRect")
        regions = reference.get("regions")
        if not isinstance(regions, list) or not regions:
            raise ValueError(f"reference {reference.get('name')} regions must be a non-empty array")
        for region_index, region in enumerate(regions):
            if not isinstance(region, dict):
                raise ValueError(f"reference {reference.get('name')} region {region_index} must be an object")
            if not str(region.get("name", "")).strip():
                raise ValueError(f"reference {reference.get('name')} region {region_index} missing name")
            _rect(region.get("windowsPixelRect"), f"region {region.get('name')} windowsPixelRect")
            _rect(region.get("yakkaiPixelRect"), f"region {region.get('name')} yakkaiPixelRect")
            sample_rects = region.get("sampleRects", [])
            if not isinstance(sample_rects, list):
                raise ValueError(f"region {region.get('name')} sampleRects must be an array")
            for sample_index, sample in enumerate(sample_rects):
                if not isinstance(sample, dict):
                    raise ValueError(f"region {region.get('name')} sample {sample_index} must be an object")
                sample_name = str(sample.get("name", sample_index)).strip() or str(sample_index)
                _rect(sample.get("windowsPixelRect"), f"sample {sample_name} windowsPixelRect")
                _rect(sample.get("yakkaiPixelRect"), f"sample {sample_name} yakkaiPixelRect")
    return data


def _int_pair(value: Any, name: str) -> tuple[int, int]:
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError(f"{name} must contain two integers")
    result = (int(value[0]), int(value[1]))
    if result[0] <= 0 or result[1] <= 0:
        raise ValueError(f"{name} must be positive")
    return result


def _rect(value: Any, name: str) -> tuple[int, int, int, int]:
    if not isinstance(value, list) or len(value) != 4:
        raise ValueError(f"{name} must contain four integers")
    result = (int(value[0]), int(value[1]), int(value[2]), int(value[3]))
    if result[2] <= result[0] or result[3] <= result[1]:
        raise ValueError(f"{name} must cover a positive area")
    return result


def _normalize_rect(rect: tuple[int, int, int, int], image_size: tuple[int, int]) -> list[float]:
    width, height = image_size
    return [
        round(rect[0] / width, 6),
        round(rect[1] / height, 6),
        round(rect[2] / width, 6),
        round(rect[3] / height, 6),
    ]


def _relative(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def _region_config(reference: dict[str, Any], image_size: tuple[int, int]) -> dict[str, Any]:
    regions: list[dict[str, Any]] = []
    for region in reference["regions"]:
        row = {
            "name": str(region["name"]),
            "purpose": str(region.get("purpose", "")),
            "windowsRect": _normalize_rect(_rect(region["windowsPixelRect"], "windowsPixelRect"), image_size),
            "yakkaiRect": _normalize_rect(_rect(region["yakkaiPixelRect"], "yakkaiPixelRect"), image_size),
        }
        feature = region.get("featureDetection")
        if isinstance(feature, dict):
            row["featureDetection"] = feature
        sample_rects: list[dict[str, Any]] = []
        for sample in region.get("sampleRects", []):
            sample_rects.append(
                {
                    "name": str(sample.get("name", "sample")),
                    "purpose": str(sample.get("purpose", "")),
                    "windowsRect": _normalize_rect(
                        _rect(sample["windowsPixelRect"], "sample windowsPixelRect"),
                        image_size,
                    ),
                    "yakkaiRect": _normalize_rect(
                        _rect(sample["yakkaiPixelRect"], "sample yakkaiPixelRect"),
                        image_size,
                    ),
                }
            )
        if sample_rects:
            row["sampleRects"] = sample_rects
        regions.append(row)
    return {"regions": regions}


def _write_summary_markdown(summary: dict[str, Any], output: Path) -> None:
    lines = [
        "# Arona Media Parity Fixture",
        "",
        f"- Scene: `{summary.get('sceneId', '')}`",
        f"- Render size: `{summary.get('renderSize', [])}`",
        f"- Reference root: `{summary.get('referenceRoot', '')}`",
        f"- Candidate frame: `{summary.get('candidateFrame', '')}`",
        "",
        "| reference | size | crop | regions | candidate crop | comparison |",
        "| --- | ---: | --- | --- | --- | --- |",
    ]
    for row in summary.get("references", []):
        lines.append(
            f"| `{row.get('name', '')}` | `{row.get('referenceSize', [])}` | "
            f"`{row.get('candidateCropRect', [])}` | `{row.get('regions', '')}` | "
            f"`{row.get('candidateCrop', '')}` | `{row.get('comparisonSummary', '')}` |"
        )
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_fixture_outputs(
    spec: dict[str, Any],
    *,
    reference_root: Path,
    output_dir: Path,
    candidate_frame: Path | None = None,
    run_comparisons: bool = False,
    align_search_radius: int = 0,
    reference_names: set[str] | None = None,
) -> dict[str, Any]:
    output_dir.mkdir(parents=True, exist_ok=True)
    regions_dir = output_dir / "regions"
    crops_dir = output_dir / "crops"
    comparisons_dir = output_dir / "comparisons"
    regions_dir.mkdir(exist_ok=True)

    render_size = _int_pair(spec["renderSize"], "renderSize")
    candidate_image: Image.Image | None = None
    if candidate_frame is not None:
        with Image.open(candidate_frame) as image:
            candidate_image = image.convert("RGBA")
        if candidate_image.size != render_size:
            raise ValueError(
                f"candidate frame size {candidate_image.size} does not match fixture renderSize {render_size}"
            )
        crops_dir.mkdir(exist_ok=True)
    if run_comparisons and candidate_image is None:
        raise ValueError("--compare requires --candidate-frame")

    summary: dict[str, Any] = {
        "sceneId": spec["sceneId"],
        "renderSize": list(render_size),
        "referenceRoot": str(reference_root),
        "candidateFrame": str(candidate_frame) if candidate_frame else "",
        "references": [],
    }
    selected_names = set(reference_names) if reference_names else None
    available_names = {str(reference["name"]) for reference in spec["references"]}
    if selected_names is not None:
        unknown_names = sorted(selected_names - available_names)
        if unknown_names:
            raise ValueError(f"unknown reference filter(s): {', '.join(unknown_names)}")

    for reference in spec["references"]:
        name = str(reference["name"])
        if selected_names is not None and name not in selected_names:
            continue
        reference_path = reference_root / str(reference["reference"])
        with Image.open(reference_path) as reference_image:
            reference_size = reference_image.size

        crop_rect = _rect(reference["candidateCropRect"], f"{name} candidateCropRect")
        crop_size = (crop_rect[2] - crop_rect[0], crop_rect[3] - crop_rect[1])
        if crop_size != reference_size:
            raise ValueError(
                f"{name} candidate crop size {crop_size} does not match reference size {reference_size}"
            )

        regions_path = regions_dir / f"{name}.json"
        regions_path.write_text(
            json.dumps(_region_config(reference, reference_size), indent=2) + "\n",
            encoding="utf-8",
        )

        row: dict[str, Any] = {
            "name": name,
            "reference": str(reference_path),
            "referenceSize": list(reference_size),
            "candidateCropRect": list(crop_rect),
            "regions": _relative(regions_path, output_dir),
        }

        crop_path: Path | None = None
        if candidate_image is not None:
            crop_path = crops_dir / f"{name}.png"
            candidate_image.crop(crop_rect).save(crop_path)
            row["candidateCrop"] = _relative(crop_path, output_dir)

        if run_comparisons and crop_path is not None:
            comparison_output = comparisons_dir / name
            report = parity_compare.build_report(
                title=f"Arona media parity: {name}",
                windows_reference=reference_path,
                yakkai_candidate=crop_path,
                regions_json=regions_path,
                dominant_mismatch=str(reference.get("dominantMismatch", "")),
                review_note=str(reference.get("reviewNote", "")),
                align_search_radius=align_search_radius,
            )
            parity_compare.write_outputs(
                report,
                windows_reference=reference_path,
                yakkai_candidate=crop_path,
                regions_json=regions_path,
                output_dir=comparison_output,
            )
            row["comparisonSummary"] = _relative(comparison_output / "summary.md", output_dir)

        summary["references"].append(row)

    (output_dir / "fixture-summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    _write_summary_markdown(summary, output_dir / "fixture-summary.md")
    return summary


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate exact-size Arona media-widget parity crops and comparator region files."
    )
    parser.add_argument("--fixture", type=Path, default=DEFAULT_FIXTURE)
    parser.add_argument("--reference-root", type=Path, default=DEFAULT_REFERENCE_ROOT)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--candidate-frame", type=Path)
    parser.add_argument("--compare", action="store_true")
    parser.add_argument("--align-search-radius", type=int, default=0)
    parser.add_argument(
        "--reference",
        dest="references",
        action="append",
        help="Only emit one fixture reference by name. Repeat to include multiple references.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        spec = load_fixture(args.fixture)
        summary = write_fixture_outputs(
            spec,
            reference_root=args.reference_root,
            output_dir=args.output_dir,
            candidate_frame=args.candidate_frame,
            run_comparisons=args.compare,
            align_search_radius=args.align_search_radius,
            reference_names=set(args.references) if args.references else None,
        )
    except Exception as exc:
        print(f"arona_media_parity_fixture: {exc}", file=sys.stderr)
        return 2

    print(f"Wrote {args.output_dir / 'fixture-summary.json'}")
    print(f"Wrote {args.output_dir / 'fixture-summary.md'}")
    print(f"References: {len(summary.get('references', []))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
