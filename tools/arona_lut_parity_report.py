#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any


LUT_COLOR_TOKENS = (
    "lut",
    "lut_loader",
    "color-grade",
    "color_grade",
    "color grading",
    "color_grading",
    "colorgrading",
    "colorgrade",
    "colorcorrection",
)


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{path} is not a JSON object")
    return data


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def unique_strings(value: Any) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for item in as_list(value):
        text = str(item)
        if text not in seen:
            seen.add(text)
            result.append(text)
    return result


def entry_layer(entry: dict[str, Any]) -> dict[str, Any]:
    layer = entry.get("layer")
    return layer if isinstance(layer, dict) else entry


def entry_policy(entry: dict[str, Any]) -> dict[str, Any]:
    policy = entry.get("policy")
    if isinstance(policy, dict):
        return policy
    layer_policy = entry_layer(entry).get("policy")
    return layer_policy if isinstance(layer_policy, dict) else {}


def first_value(entry: dict[str, Any], keys: tuple[str, ...]) -> Any:
    layer = entry_layer(entry)
    policy = entry_policy(entry)
    for source in (entry, layer, policy):
        for key in keys:
            value = source.get(key)
            if value not in (None, ""):
                return value
    return None


def has_family(entry: dict[str, Any], family: str) -> bool:
    layer = entry_layer(entry)
    for source in (entry, layer):
        for key in ("candidateMixFamilies", "candidateFamilies"):
            if family in [str(item) for item in as_list(source.get(key))]:
                return True
    return family in text_fields(entry)


def infer_candidate_effect_class(entry: dict[str, Any]) -> str:
    explicit = first_value(entry, ("candidateEffectClass",))
    if explicit:
        return str(explicit)

    shape = str(first_value(entry, ("candidateChainShape",)) or "")
    text = text_fields(entry)
    has_lut = has_family(entry, "lut")
    has_color = has_family(entry, "color-grade")
    if not has_lut and not has_color:
        return "none"
    if shape == "protected-puppet-mixed":
        return "protected-puppet-lut" if has_lut else "protected-puppet-color-grade"
    if shape == "puppet-mixed":
        return "mixed-puppet-lut" if has_lut else "mixed-puppet-color-grade"
    if "composelayer" in shape:
        return "composelayer-color-grade" if has_color else "composelayer-lut"
    if shape == "lut-only":
        return "regular-lut-only"
    if shape in {"color-grade-only", "color_grading-only"}:
        return "regular-color-grade-only"
    if has_lut and has_color:
        return "mixed-lut-color-grade"
    if has_lut:
        return "mixed-lut"
    if "color" in text:
        return "mixed-color-grade"
    return "none"


def disposition_for_entry(entry: dict[str, Any], source: str, effect_class: str, state: str) -> str:
    policy = entry_policy(entry)
    layer = entry_layer(entry)
    debug_probe = layer.get("debugProbe")
    if not isinstance(debug_probe, dict):
        debug_probe = entry.get("debugProbe") if isinstance(entry.get("debugProbe"), dict) else {}
    if effect_class.startswith("protected-puppet-"):
        return "protected"
    if debug_probe.get("overrodePolicy") is True:
        return "probe-only"
    if state == "kept" and policy.get("keepEffects") is not False:
        return "allowed"
    if source == "strippedCandidates" or policy.get("strippedEffects") is True:
        return "stripped"
    return "unknown"


def text_fields(entry: dict[str, Any]) -> str:
    values: list[str] = []
    layer = entry_layer(entry)
    policy = entry_policy(entry)
    text_sources = (entry, layer, policy)
    for source in text_sources:
        for key in ("layerName", "policyReason", "reason", "candidateChainShape", "candidateRisk"):
            value = source.get(key)
            if isinstance(value, str):
                values.append(value)
        for key in ("candidateMixFamilies", "candidateFamilies", "effectNames", "materialShaders"):
            values.extend(str(item) for item in as_list(source.get(key)))
    return " ".join(values).lower()


def is_lut_or_color_entry(entry: dict[str, Any]) -> bool:
    text = text_fields(entry)
    return any(token in text for token in LUT_COLOR_TOKENS)


def capture_stage(record: dict[str, Any]) -> dict[str, Any]:
    info = record.get("renderTargetInfo")
    if not isinstance(info, dict):
        info = {}
    return {
        "stage": str(record.get("stage", "")),
        "path": str(record.get("path", "")),
        "renderTarget": str(record.get("renderTarget", "")),
        "width": int(info.get("width", 0) or 0),
        "height": int(info.get("height", 0) or 0),
        "format": str(info.get("format", "")),
    }


def material_entries(layer: dict[str, Any]) -> list[dict[str, Any]]:
    return [entry for entry in as_list(layer.get("effectMaterials")) if isinstance(entry, dict)]


def missing_layer_evidence(layer: dict[str, Any]) -> list[str]:
    materials = material_entries(layer)
    missing: list[str] = []
    if not any(as_list(material.get("textureBindings")) for material in materials):
        missing.append("textureBindings")
    if not any(isinstance(material.get("resolvedConstValues"), dict) and material["resolvedConstValues"] for material in materials):
        missing.append("uniforms")
    if not any(isinstance(material.get("materialValues"), dict) and material["materialValues"] for material in materials):
        missing.append("materialValues")
    return missing


def normalized_layer(entry: dict[str, Any], source: str) -> dict[str, Any]:
    policy = entry_policy(entry)
    layer = entry_layer(entry)
    stripped = first_value(entry, ("strippedEffects",))
    reason = (
        first_value(entry, ("policyReason", "reason", "effectDecisionReason"))
        or policy.get("reason")
        or ""
    )
    state = "stripped" if stripped is True or source == "strippedCandidates" else "kept"
    effect_class = infer_candidate_effect_class(entry)
    result = {
        "id": first_value(entry, ("layerId",)),
        "name": first_value(entry, ("layerName",)) or "",
        "state": state,
        "class": effect_class,
        "disposition": disposition_for_entry(entry, source, effect_class, state),
        "reason": reason,
        "shape": first_value(entry, ("candidateChainShape",)) or "",
        "blockedReason": first_value(entry, ("candidateBlockedReason",)) or "",
        "families": first_value(entry, ("candidateMixFamilies", "candidateFamilies")) or [],
        "source": source,
    }
    if source == "captures":
        captures = as_list(entry.get("captureRecords"))
        result.update(
            {
                "layerImage": first_value(entry, ("layerImage",)) or "",
                "visibleEffectCount": first_value(entry, ("visibleEffectCount",)) or 0,
                "effectNames": unique_strings(layer.get("effectNames")),
                "materialShaders": unique_strings(layer.get("materialShaders")),
                "effectMaterials": material_entries(layer),
                "captureStages": [capture_stage(record) for record in captures if isinstance(record, dict)],
                "missingEvidence": missing_layer_evidence(layer),
            }
        )
    return result


def layer_entries(manifest: dict[str, Any]) -> list[tuple[str, dict[str, Any]]]:
    entries: list[tuple[str, dict[str, Any]]] = []
    seen_kept: dict[tuple[str, str], dict[str, Any]] = {}
    for source, values in (("captures", as_list(manifest.get("captures"))),):
        for value in values:
            if not isinstance(value, dict):
                continue
            layer = value.get("layer")
            if not isinstance(layer, dict):
                continue
            if not layer:
                continue
            key = (
                str(layer.get("layerId", "")),
                str(layer.get("layerName", "")),
            )
            existing = seen_kept.get(key)
            if existing is None:
                record = dict(value)
                record["captureRecords"] = [value]
                seen_kept[key] = record
                entries.append((source, record))
            else:
                existing.setdefault("captureRecords", []).append(value)

    for source, values in (
        ("strippedCandidates", as_list(manifest.get("strippedCandidates"))),
    ):
        for value in values:
            if isinstance(value, dict):
                entries.append((source, value))
    return entries


def layer_sort_key(layer: dict[str, Any]) -> tuple[str, int, str]:
    layer_id = layer.get("id")
    try:
        numeric_id = int(layer_id)
    except (TypeError, ValueError):
        numeric_id = -1
    return (str(layer.get("state", "")), numeric_id, str(layer.get("name", "")))


def lut_layers(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    layers: list[dict[str, Any]] = []
    for source, entry in layer_entries(manifest):
        if is_lut_or_color_entry(entry):
            layers.append(normalized_layer(entry, source))
    layers.sort(key=layer_sort_key)
    return layers


def number(value: Any) -> float | None:
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        try:
            return float(value)
        except ValueError:
            return None
    return None


def rgb_delta(metrics: dict[str, Any]) -> list[float] | None:
    ref = metrics.get("referenceMeanRgb")
    yakkai = metrics.get("yakkaiMeanRgb")
    if len(as_list(ref)) != 3 or len(as_list(yakkai)) != 3:
        return None

    delta: list[float] = []
    for index in range(3):
        ref_part = number(ref[index])
        yakkai_part = number(yakkai[index])
        if ref_part is None or yakkai_part is None:
            return None
        delta.append(yakkai_part - ref_part)
    return delta


def manifest_path_for_variant(summary_path: Path, summary: dict[str, Any], variant: dict[str, Any]) -> Path:
    explicit = variant.get("effectManifest")
    if isinstance(explicit, str) and explicit:
        return Path(explicit)
    output_dir = Path(str(summary.get("outputDir") or summary_path.parent))
    return output_dir / str(variant.get("name", "")) / "effect-captures" / "manifest.json"


def build_report(summary_path: Path) -> dict[str, Any]:
    summary = load_json(summary_path)
    variants: list[dict[str, Any]] = []
    for variant in as_list(summary.get("variants")):
        if not isinstance(variant, dict):
            continue
        metrics = variant.get("metrics", {})
        if not isinstance(metrics, dict):
            metrics = {}

        manifest_path = manifest_path_for_variant(summary_path, summary, variant)
        manifest_status = "missing"
        layers: list[dict[str, Any]] = []
        if manifest_path.is_file():
            manifest = load_json(manifest_path)
            manifest_status = str(manifest.get("status", "unknown"))
            layers = lut_layers(manifest)

        class_counts: dict[str, int] = {}
        disposition_counts: dict[str, int] = {}
        for layer in layers:
            effect_class = str(layer.get("class") or "none")
            disposition = str(layer.get("disposition") or "unknown")
            class_counts[effect_class] = class_counts.get(effect_class, 0) + 1
            disposition_counts[disposition] = disposition_counts.get(disposition, 0) + 1

        variants.append(
            {
                "name": str(variant.get("name", "")),
                "status": str(variant.get("status", "")),
                "rmse": number(metrics.get("rmse")),
                "rgbDelta": rgb_delta(metrics),
                "manifest": str(manifest_path),
                "manifestStatus": manifest_status,
                "lutLayerCount": len(layers),
                "lutClassCounts": dict(sorted(class_counts.items())),
                "lutDispositionCounts": dict(sorted(disposition_counts.items())),
                "lutLayers": layers,
            }
        )

    comparable = [item for item in variants if isinstance(item.get("rmse"), (int, float))]
    highest = max(comparable, key=lambda item: float(item["rmse"])) if comparable else None
    return {
        "summary": str(summary_path),
        "status": summary.get("status"),
        "outputDir": summary.get("outputDir"),
        "highestDrift": highest,
        "variants": variants,
    }


def format_float(value: Any) -> str:
    numeric = number(value)
    if numeric is None:
        return "n/a"
    return f"{numeric:.6f}".rstrip("0").rstrip(".")


def format_delta(value: Any) -> str:
    if not isinstance(value, list) or len(value) != 3:
        return "n/a"
    return "[" + ", ".join(f"{float(part):+.4f}" for part in value) + "]"


def format_value_map(value: Any) -> str:
    if not isinstance(value, dict) or not value:
        return ""
    parts: list[str] = []
    for key in sorted(value):
        parts.append(f"{key}={json.dumps(value[key])}")
    return ", ".join(parts)


def format_counts(value: Any) -> str:
    if not isinstance(value, dict) or not value:
        return "none"
    return ", ".join(f"{key}={value[key]}" for key in sorted(value))


def format_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# Arona LUT WE Parity Report",
        "",
        f"summary: `{report['summary']}`",
        f"status: `{report.get('status')}`",
    ]
    highest = report.get("highestDrift")
    if isinstance(highest, dict):
        lines.append(
            "highest drift: "
            f"{highest['name']} rmse={format_float(highest.get('rmse'))} "
            f"rgbDelta={format_delta(highest.get('rgbDelta'))}"
        )
    lines.append("")

    for variant in report["variants"]:
        lines.append(f"## {variant['name']}")
        lines.append(f"- status: `{variant['status']}`")
        lines.append(f"- rmse: `{format_float(variant.get('rmse'))}`")
        lines.append(f"- rgbDelta: `{format_delta(variant.get('rgbDelta'))}`")
        lines.append(f"- manifest: `{variant['manifest']}` ({variant['manifestStatus']})")
        lines.append(f"- lut/color layers: `{variant['lutLayerCount']}`")
        lines.append(f"- class counts: `{format_counts(variant.get('lutClassCounts'))}`")
        lines.append(f"- disposition counts: `{format_counts(variant.get('lutDispositionCounts'))}`")
        for layer in variant["lutLayers"]:
            lines.append(
                f"  - {layer['id']} {layer['name']} {layer['state']} "
                f"{layer['disposition']} {layer['class']} {layer['reason']} {layer['shape']}"
            )
            if layer.get("state") == "kept":
                if layer.get("layerImage"):
                    lines.append(f"    image={layer['layerImage']}")
                if layer.get("effectNames"):
                    lines.append(f"    effects={', '.join(layer['effectNames'])}")
                if layer.get("materialShaders"):
                    lines.append(f"    shaders={', '.join(layer['materialShaders'])}")
                for stage in as_list(layer.get("captureStages")):
                    if not isinstance(stage, dict):
                        continue
                    lines.append(
                        "    capture="
                        f"{stage.get('stage')} {stage.get('width')}x{stage.get('height')} "
                        f"{stage.get('format')} {stage.get('path')}"
                    )
                for material in as_list(layer.get("effectMaterials")):
                    if not isinstance(material, dict):
                        continue
                    lines.append(
                        "    material"
                        f"[{material.get('effectIndex')}:{material.get('materialIndex')}] "
                        f"shader={material.get('shader', '')}"
                    )
                    for binding in as_list(material.get("textureBindings")):
                        if not isinstance(binding, dict):
                            continue
                        lines.append(
                            "      textures"
                            f"[{binding.get('slot')}] "
                            f"authored={binding.get('authored', '')} "
                            f"resolved={binding.get('resolved', '')}"
                        )
                    material_values = format_value_map(material.get("materialValues"))
                    if material_values:
                        lines.append(f"      materialValues={material_values}")
                    const_values = format_value_map(material.get("resolvedConstValues"))
                    if const_values:
                        lines.append(f"      constValues={const_values}")
                    if material.get("defines"):
                        lines.append(f"      defines={', '.join(str(item) for item in as_list(material.get('defines')))}")
                if layer.get("missingEvidence"):
                    lines.append(f"    missingEvidence={', '.join(layer['missingEvidence'])}")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Summarize Arona LUT/color-grade evidence from comparator artifacts."
    )
    parser.add_argument("summary", help="Path to comparator summary.json")
    parser.add_argument(
        "--json",
        action="store_true",
        help="Write machine-readable report JSON instead of Markdown.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    report = build_report(Path(args.summary))
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(format_markdown(report), end="")
    missing = [variant for variant in report["variants"] if variant["manifestStatus"] == "missing"]
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
