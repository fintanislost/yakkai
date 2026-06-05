#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any

import arona_protected_puppet_lab as base_lab


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    if not isinstance(payload, dict):
        raise ValueError(f"{path} is not a JSON object")
    return payload


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def merge_layer_metadata(records: list[dict[str, Any]]) -> dict[str, Any]:
    merged: dict[str, Any] = {}
    for record in records:
        layer = as_dict(record.get("layer"))
        for key, value in layer.items():
            if key not in merged or merged[key] in (None, "", [], {}):
                merged[key] = value
                continue
            if isinstance(value, dict) and isinstance(merged.get(key), dict):
                nested = dict(merged[key])
                for nested_key, nested_value in value.items():
                    if nested_key not in nested or nested[nested_key] in (None, "", [], {}):
                        nested[nested_key] = nested_value
                merged[key] = nested
    return merged


def capture_layer_id(record: dict[str, Any]) -> int | None:
    return base_lab.layer_id(record)


def sorted_layer_ids(records: list[dict[str, Any]]) -> list[int]:
    ids: set[int] = set()
    for record in records:
        value = capture_layer_id(record)
        if value is not None:
            ids.add(value)
    return sorted(ids)


def selected_layer_ids(records: list[dict[str, Any]], layer_ids: list[int] | None) -> list[int]:
    if layer_ids:
        return layer_ids
    return sorted_layer_ids(records)


def first_string(values: Any) -> str:
    for value in as_list(values):
        if isinstance(value, str) and value:
            return value
    return ""


def build_layer_report(records: list[dict[str, Any]], layer_id: int) -> dict[str, Any]:
    layer_records = [
        record
        for record in records
        if isinstance(record, dict) and capture_layer_id(record) == layer_id
    ]
    layer = merge_layer_metadata(layer_records)
    captures = [base_lab.capture_record(record) for record in layer_records]
    boundary_comparisons = base_lab.boundary_comparisons(captures)
    final_coverage = base_lab.final_coverage_diagnostics(captures, layer, None)
    route = base_lab.route_diagnostics(layer, None)
    material_shaders = as_list(layer.get("materialShaders"))
    effect_names = as_list(layer.get("effectNames"))

    return {
        "layerId": layer_id,
        "layerName": str(layer.get("layerName", "")),
        "candidateChainShape": str(layer.get("candidateChainShape", "")),
        "candidateEffectClass": str(layer.get("candidateEffectClass", "")),
        "candidateFamilies": as_list(layer.get("candidateFamilies")),
        "candidateMixFamilies": as_list(layer.get("candidateMixFamilies")),
        "firstShader": first_string(material_shaders),
        "firstEffectName": first_string(effect_names),
        "materialShaders": material_shaders,
        "effectNames": effect_names,
        "debugProbe": as_dict(layer.get("debugProbe")),
        "route": route,
        "captures": captures,
        "boundaryComparisons": boundary_comparisons,
        "finalCoverage": final_coverage,
    }


def build_report(manifest_path: Path, layer_ids: list[int] | None = None) -> dict[str, Any]:
    manifest = load_json(manifest_path)
    records = [
        record
        for record in as_list(manifest.get("captures"))
        if isinstance(record, dict)
    ]
    ids = selected_layer_ids(records, layer_ids)
    layers = [build_layer_report(records, layer_id) for layer_id in ids]
    return {
        "manifest": str(manifest_path),
        "probeMaxEffects": manifest.get("probeMaxEffects"),
        "layers": layers,
    }


def parse_layer_ids(value: str) -> list[int]:
    ids: list[int] = []
    for part in value.split(","):
        text = part.strip()
        if not text:
            continue
        try:
            ids.append(int(text))
        except ValueError as exc:
            raise argparse.ArgumentTypeError(f"invalid layer id: {text}") from exc
    return ids


def render_text(report: dict[str, Any]) -> str:
    lines = [f"manifest: {report['manifest']}"]
    if report.get("probeMaxEffects") is not None:
        lines.append(f"probeMaxEffects: {report['probeMaxEffects']}")
    for layer in report["layers"]:
        lines.append("")
        lines.append(
            f"layer {layer['layerId']} {layer['layerName']} "
            f"shape={layer['candidateChainShape']} firstShader={layer['firstShader']}"
        )
        lines.append(
            f"  route: {layer['route'].get('classification')} "
            f"inputMesh={layer['route'].get('effectInputMeshKind')} "
            f"finalMesh={layer['route'].get('standaloneFinalMeshKind') or layer['route'].get('effectFinalMeshKind')} "
            f"risk={layer['route'].get('routeRisk')}"
        )
        for comparison in layer["boundaryComparisons"]:
            lines.append(
                f"  boundary {comparison['boundary']}: "
                f"{comparison['classification']} rmse={comparison.get('rmse')} "
                f"alphaRmse={comparison.get('alphaRmse')} "
                f"alphaIou={comparison.get('alphaBoundsIou')}"
            )
        final_coverage = layer["finalCoverage"]
        lines.append(
            f"  finalCoverage: {final_coverage.get('classification')} "
            f"source={final_coverage.get('source')}"
        )
        alignment = final_coverage.get("finalDisplayAlignment")
        if isinstance(alignment, dict):
            lines.append(
                f"  finalDisplayAlignment: {alignment.get('classification')} "
                f"ratio={alignment.get('deltaToOutputVisibleRatio')} "
                f"iou={alignment.get('boundsIou')} "
                f"centroidDrift={alignment.get('centroidDrift')}"
            )
        projection = final_coverage.get("finalDisplayScreenSpaceProjection")
        if isinstance(projection, dict):
            lines.append(
                f"  finalDisplayProjection: {projection.get('classification')} "
                f"drift={projection.get('projectedCentroidDrift')}"
            )
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Summarize generic puppet-layer effect probe boundaries."
    )
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--layers", type=parse_layer_ids, default=None)
    parser.add_argument("--json", action="store_true", dest="json_output")
    args = parser.parse_args(argv)

    report = build_report(args.manifest, layer_ids=args.layers)
    if args.json_output:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(render_text(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
