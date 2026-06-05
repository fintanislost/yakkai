#!/usr/bin/env python3
import argparse
import json
import sys
from collections import Counter
from pathlib import Path


def as_list(value):
    return value if isinstance(value, list) else []


def layer_from(record):
    if not isinstance(record, dict):
        return {}
    layer = record.get("layer")
    return layer if isinstance(layer, dict) else record


def policy_from(layer):
    policy = layer.get("policy")
    return policy if isinstance(policy, dict) else {}


def checks_from(layer):
    checks = layer.get("candidateChecks")
    return checks if isinstance(checks, dict) else {}


def debug_probe_from(layer):
    probe = layer.get("debugProbe")
    return probe if isinstance(probe, dict) else {}


def publish_from(layer):
    publish = layer.get("publish")
    return publish if isinstance(publish, dict) else {}


def effect_materials_from(layer):
    return [item for item in as_list(layer.get("effectMaterials")) if isinstance(item, dict)]


def material_shader_names(layer):
    names = []
    for material in effect_materials_from(layer):
        shader = str(material.get("shader") or "")
        if shader:
            names.append(shader)
    return names


def effect_class_for(layer):
    explicit = layer.get("candidateEffectClass")
    if explicit and str(explicit) != "none":
        return str(explicit)

    risk = str(layer.get("candidateRisk") or "")
    shape = str(layer.get("candidateChainShape") or "")
    policy = policy_from(layer)
    reason = str(policy.get("reason") or layer.get("policyReason") or "")
    checks = checks_from(layer)

    if checks.get("isProtectedPuppetPath") is True or shape.startswith("protected-puppet"):
        if checks.get("hasBlurFamily") is True:
            return "protected-puppet-blur"
        if checks.get("hasColorGradingFamily") is True:
            return "protected-puppet-color-grade"
        if checks.get("hasLutFamily") is True:
            return "protected-puppet-lut"
        return "protected-puppet-safe-family"
    if risk == "simple-water" or (shape == "simple-water" and checks.get("waterOnly") is True):
        return "simple-water"
    if shape == "water-composelayer":
        return "composelayer-water-only"
    if shape == "water-utility":
        return "utility-water-only"
    if shape == "water-fullscreen":
        return "fullscreen-water-only"
    if shape == "audio-utility":
        return "audio-utility"
    if shape == "puppet-mixed":
        return "puppet-mixed"
    if shape == "carrier-mixed":
        return "carrier-mixed"
    if shape == "unknown-mixed" and reason == "essential-effect":
        return "essential-effect"
    return "none"


def visual_gate_audit_for(layer):
    effect_class = effect_class_for(layer)
    if effect_class not in ("composelayer-color-grade", "utility-blur", "composelayer-water-only"):
        return {
            "classification": "not-audited",
            "reason": "visual gate audit currently applies to composelayer-color-grade, utility-blur, and composelayer-water-only",
        }

    publish = publish_from(layer)
    checks = checks_from(layer)
    debug_probe = debug_probe_from(layer)
    policy = policy_from(layer)
    materials = effect_materials_from(layer)
    shader_names = material_shader_names(layer)
    has_color_grade_material = any("color" in shader and "grad" in shader for shader in shader_names)
    has_blur_material = any("blur" in shader for shader in shader_names)
    has_water_material = any("water" in shader for shader in shader_names)
    production_allowed = (
        policy.get("keepEffects") is True and policy.get("strippedEffects") is not True
    )

    mix_families = set(str(value) for value in as_list(layer.get("candidateMixFamilies")))
    missing = []
    if effect_class == "composelayer-color-grade":
        if checks.get("isComposelayer") is not True:
            missing.append("composelayer-check")
        if checks.get("hasColorGradingFamily") is not True and "color-grade" not in mix_families:
            missing.append("color-grade-family")
        if publish.get("finalDisplayRoute") != "effect-layer-composite-final-publish":
            missing.append("composite-final-publish-route")
        if materials and not has_color_grade_material:
            missing.append("color-grade-material")
    elif effect_class == "utility-blur":
        if checks.get("isUtilityCarrier") is not True:
            missing.append("utility-carrier-check")
        if checks.get("isFullscreen") is not True:
            missing.append("fullscreen-check")
        if checks.get("hasBlurFamily") is not True and "blur" not in mix_families:
            missing.append("blur-family")
        if publish.get("finalDisplayRoute") != "effect-layer-fullscreen-final-publish":
            missing.append("fullscreen-final-publish-route")
        if materials and not has_blur_material:
            missing.append("blur-material")
    elif effect_class == "composelayer-water-only":
        if checks.get("isComposelayer") is not True:
            missing.append("composelayer-check")
        if checks.get("hasWaterFamily") is not True and checks.get("waterOnly") is not True:
            missing.append("water-family")
        if checks.get("waterOnly") is not True:
            missing.append("water-only")
        if publish.get("finalDisplayRoute") != "effect-layer-composite-final-publish":
            missing.append("composite-final-publish-route")
        if materials and not has_water_material:
            missing.append("water-material")
    if not production_allowed:
        if debug_probe.get("overrodePolicy") is not True:
            missing.append("high-risk-probe-override")
        if publish.get("enabled") is not True:
            missing.append("active-probe-publish-route")
    if publish.get("publishFinalOutput") is not True:
        missing.append("publish-final-output")
    if publish.get("effectFinalMeshKind") != "fullscreen-card":
        missing.append("fullscreen-card-final-mesh")
    if publish.get("effectInputMeshKind") != "card":
        missing.append("card-effect-input-mesh")
    if not materials:
        missing.append("effect-materials")

    if production_allowed and not missing:
        classification = "production-allowed"
    elif policy.get("strippedEffects") is True and debug_probe.get("overrodePolicy") is not True:
        classification = "needs-high-risk-probe-route"
    elif not missing:
        classification = "human-visual-review-required"
    else:
        classification = "incomplete-visual-gate-evidence"

    return {
        "classification": classification,
        "missing": missing,
        "requiresHumanReview": classification == "human-visual-review-required",
        "finalDisplayRoute": str(publish.get("finalDisplayRoute") or ""),
        "publishFinalOutput": publish.get("publishFinalOutput"),
        "effectInputNodeReset": publish.get("effectInputNodeReset"),
        "effectInputMeshKind": str(publish.get("effectInputMeshKind") or ""),
        "effectFinalMeshKind": str(publish.get("effectFinalMeshKind") or ""),
        "probeOverrodePolicy": debug_probe.get("overrodePolicy") is True,
        "materialCount": len(materials),
        "hasColorGradeMaterial": has_color_grade_material,
        "hasBlurMaterial": has_blur_material,
        "hasWaterMaterial": has_water_material,
    }


def route_audit_for(layer):
    effect_class = effect_class_for(layer)
    if effect_class != "regular-lut-only":
        return {
            "classification": "not-audited",
            "reason": "route audit currently applies to regular-lut-only",
        }

    publish = publish_from(layer)
    materials = effect_materials_from(layer)
    final_materials = [item for item in materials if item.get("finalPublishedMaterial") is True]
    local_material_outputs = [
        item for item in materials
        if str(item.get("localMaterialOutputCaptureStage") or "")
    ]
    material_outputs = [
        item for item in materials
        if str(item.get("materialOutputCaptureStage") or "")
    ]

    missing = []
    if publish.get("routeRisk"):
        missing.append("empty-route-risk")
    if publish.get("finalDisplayRoute") != "effect-layer-node-final-publish":
        missing.append("effect-layer-node-final-publish-route")
    if publish.get("publishFinalOutput") is not True:
        missing.append("publish-final-output")
    if publish.get("finalNodeUsesOriginalParent") is not True:
        missing.append("original-parent-final-node")
    if publish.get("effectInputNodeReset") is not True:
        missing.append("layer-local-effect-input-reset")
    if publish.get("effectInputMeshKind") != "card":
        missing.append("card-effect-input-mesh")
    if publish.get("effectFinalMeshKind") != "card":
        missing.append("card-effect-final-mesh")
    if not materials:
        missing.append("effect-materials")
    if not final_materials:
        missing.append("final-published-material")
    if not material_outputs:
        missing.append("material-output-capture")
    if not local_material_outputs:
        missing.append("local-material-output-capture")

    if not missing:
        classification = "route-complete"
    elif missing == ["local-material-output-capture"]:
        classification = "missing-local-material-output-capture"
    else:
        classification = "route-incomplete"

    return {
        "classification": classification,
        "missing": missing,
        "finalDisplayRoute": str(publish.get("finalDisplayRoute") or ""),
        "publishFinalOutput": publish.get("publishFinalOutput"),
        "finalNodeUsesOriginalParent": publish.get("finalNodeUsesOriginalParent"),
        "effectInputNodeReset": publish.get("effectInputNodeReset"),
        "effectInputMeshKind": str(publish.get("effectInputMeshKind") or ""),
        "effectFinalMeshKind": str(publish.get("effectFinalMeshKind") or ""),
        "routeRisk": str(publish.get("routeRisk") or ""),
        "materialCount": len(materials),
        "finalPublishedMaterialCount": len(final_materials),
        "materialOutputCaptureCount": len(material_outputs),
        "localMaterialOutputCaptureCount": len(local_material_outputs),
    }


def disposition_for(source, layer):
    policy = policy_from(layer)
    effect_class = effect_class_for(layer)
    debug_probe = debug_probe_from(layer)
    if source == "protected" or effect_class.startswith("protected-puppet-"):
        return "protected"
    if debug_probe.get("overrodePolicy") is True:
        return "probe-only"
    if source == "capture" and policy.get("keepEffects") is True and policy.get("strippedEffects") is not True:
        return "allowed"
    if source == "stripped" or policy.get("strippedEffects") is True:
        return "stripped"
    return "unknown"


def active_animation_ids(layer):
    ids = []
    for animation in as_list(layer.get("puppetAnimationLayers")):
        if animation.get("visibleAndWeighted") is True:
            ids.append(str(animation.get("animationId", "unknown")))
    return ids


def active_bone_slots(layer):
    slots = []
    for animation in as_list(layer.get("puppetAnimationLayers")):
        if animation.get("visibleAndWeighted") is not True:
            continue
        for slot in as_list(animation.get("activeBoneSlots")):
            text = str(slot)
            if text not in slots:
                slots.append(text)
    return slots


def normalize_record(manifest_path, manifest, source, raw):
    layer = layer_from(raw)
    policy = policy_from(layer)
    publish = publish_from(layer)
    debug_probe = debug_probe_from(layer)
    route_audit = route_audit_for(layer)
    visual_gate_audit = visual_gate_audit_for(layer)
    return {
        "sourceManifest": str(manifest_path),
        "sceneId": str(manifest.get("sceneId") or manifest.get("id") or "unknown"),
        "sceneName": str(manifest.get("sceneName") or manifest.get("name") or "unknown"),
        "sceneType": str(manifest.get("sceneType") or "unknown"),
        "source": source,
        "disposition": disposition_for(source, layer),
        "layerId": str(layer.get("layerId", "unknown")),
        "layerName": str(layer.get("layerName") or "unnamed"),
        "effectClass": effect_class_for(layer),
        "chainShape": str(layer.get("candidateChainShape") or "unknown"),
        "candidateRisk": str(layer.get("candidateRisk") or "unknown"),
        "families": [str(value) for value in as_list(layer.get("candidateFamilies"))],
        "mixFamilies": [str(value) for value in as_list(layer.get("candidateMixFamilies"))],
        "checks": checks_from(layer),
        "policyReason": str(policy.get("reason") or layer.get("policyReason") or "unknown"),
        "keepEffects": policy.get("keepEffects"),
        "strippedEffects": policy.get("strippedEffects"),
        "debugProbeReason": str(debug_probe.get("reason", "")),
        "probeOverrodePolicy": debug_probe.get("overrodePolicy") is True,
        "puppetActiveAnimations": active_animation_ids(layer),
        "puppetActiveSlots": active_bone_slots(layer),
        "puppetCutoutSlotCoverage": as_list(publish.get("puppetCutoutSlotCoverage")),
        "routeRisk": str(publish.get("routeRisk") or ""),
        "finalDisplayRoute": str(publish.get("finalDisplayRoute") or ""),
        "finalMeshKind": str(publish.get("standaloneFinalMeshKind") or publish.get("effectFinalMeshKind") or ""),
        "finalBlend": publish.get("standaloneFinalMaterialBlendMode", publish.get("finalBlend")),
        "parentId": publish.get("standaloneDisplayParentId", publish.get("parentId")),
        "routeAudit": route_audit,
        "visualGateAudit": visual_gate_audit,
    }


def has_candidate_signal(layer):
    return any(
        key in layer
        for key in (
            "candidateEffectClass",
            "candidateChainShape",
            "candidateRisk",
            "candidateFamilies",
            "candidateMixFamilies",
            "candidateChecks",
        )
    )


def records_from_manifest(path):
    manifest_path = Path(path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    records = []

    seen_capture_layers = set()
    for layer in as_list(manifest.get("layers")):
        key = (str(layer.get("layerId", "unknown")), str(layer.get("candidateChainShape", "unknown")))
        seen_capture_layers.add(key)
        records.append(normalize_record(manifest_path, manifest, "capture", layer))

    for capture in as_list(manifest.get("captures")):
        layer = layer_from(capture)
        if not layer or not has_candidate_signal(layer):
            continue
        key = (str(layer.get("layerId", "unknown")), str(layer.get("candidateChainShape", "unknown")))
        if key in seen_capture_layers:
            continue
        seen_capture_layers.add(key)
        records.append(normalize_record(manifest_path, manifest, "capture", capture))

    for candidate in as_list(manifest.get("strippedCandidates")):
        key = (str(candidate.get("layerId", "unknown")), str(candidate.get("candidateChainShape", "unknown")))
        if key in seen_capture_layers:
            continue
        records.append(normalize_record(manifest_path, manifest, "stripped", candidate))

    for candidate in as_list(manifest.get("protectedPuppetDiagnostics")):
        records.append(normalize_record(manifest_path, manifest, "protected", candidate))

    return records


def load_records(paths):
    records = []
    for path in paths:
        records.extend(records_from_manifest(path))
    return records


def print_text(records):
    print(f"candidate-records={len(records)}")
    for label, key in (
        ("effect-classes", "effectClass"),
        ("chain-shapes", "chainShape"),
        ("dispositions", "disposition"),
        ("route-audits", "routeAudit.classification"),
        ("visual-gate-audits", "visualGateAudit.classification"),
    ):
        print(f"{label}:")
        if "." in key:
            first, second = key.split(".", 1)
            values = [
                record[first].get(second)
                for record in records
                if isinstance(record.get(first), dict)
            ]
        else:
            values = [record[key] for record in records]
        for value, count in sorted(Counter(values).items()):
            print(f"  - {value}: {count}")

    print("records:")
    for record in records[:40]:
        active_slots = ",".join(record["puppetActiveSlots"]) or "none"
        print(
            f"  - {record['effectClass']} {record['disposition']} {record['chainShape']} "
            f"layer={record['layerId']} activeSlots={active_slots} "
            f"scene={record['sceneId']} reason={record['policyReason']}"
        )
    if len(records) > 40:
        print(f"  ... {len(records) - 40} more")


def main():
    parser = argparse.ArgumentParser(description="Summarize Yakkai effect candidate manifests")
    parser.add_argument("manifests", nargs="+", help="effect-capture manifest paths")
    parser.add_argument("--json", action="store_true", help="emit JSON instead of text")
    args = parser.parse_args()

    records = load_records(args.manifests)
    if args.json:
        print(json.dumps({
            "summary": {
                "candidateRecords": len(records),
                "effectClasses": Counter(record["effectClass"] for record in records),
                "dispositions": Counter(record["disposition"] for record in records),
                "routeAudits": Counter(
                    record["routeAudit"]["classification"]
                    for record in records
                    if isinstance(record.get("routeAudit"), dict)
                ),
                "visualGateAudits": Counter(
                    record["visualGateAudit"]["classification"]
                    for record in records
                    if isinstance(record.get("visualGateAudit"), dict)
                ),
            },
            "records": records,
        }, indent=2, sort_keys=True))
    else:
        print_text(records)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
