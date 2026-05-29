#!/usr/bin/env python3
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path


def as_list(value):
    return value if isinstance(value, list) else []


def get_stage(record):
    return str(record.get("stage", "unknown"))


def capture_layer(record):
    layer = record.get("layer")
    return layer if isinstance(layer, dict) else {}


def candidate_layer(record):
    if not isinstance(record, dict):
        return {}

    layer = record.get("layer")
    return layer if isinstance(layer, dict) else record


def capture_key(record):
    layer = capture_layer(record)
    layer_id = layer.get("layerId")
    layer_name = layer.get("layerName")
    if layer_id is not None or layer_name:
        return f"{layer_id if layer_id is not None else 'unknown'}:{layer_name or 'unnamed'}"

    if record.get("layerKey"):
        return str(record["layerKey"])
    if record.get("key"):
        return str(record["key"])
    if record.get("label"):
        return str(record["label"])

    return "unknown"


def unique_layers(manifest_layers, captures):
    if manifest_layers:
        return manifest_layers

    layers = {}
    for record in captures:
        layer = capture_layer(record)
        if not layer:
            continue
        layers.setdefault(capture_key(record), layer)
    return list(layers.values())


def capture_failures(top_level_failures, captures):
    failures = list(top_level_failures)
    for record in captures:
        if record.get("failed") or record.get("completed") is False:
            failures.append({
                "stage": get_stage(record),
                "path": record.get("path", "unknown"),
                "reason": record.get("failureReason") or record.get("reason", "unknown"),
            })
    return failures


def decision_for_layer(layer):
    if layer.get("effectDecision") or layer.get("decision"):
        decision = str(layer.get("effectDecision", layer.get("decision", "unknown")))
        reason = str(layer.get("effectDecisionReason", layer.get("reason", "unknown")))
        return decision, reason

    policy = layer.get("policy")
    if not isinstance(policy, dict):
        return "unknown", "unknown"

    if policy.get("keepLayer") is False:
        decision = "skip-layer"
    elif policy.get("strippedEffects"):
        decision = "strip-effects"
    elif policy.get("keepEffects") is True:
        decision = "keep-effects"
    elif policy.get("keepEffects") is False:
        decision = "drop-effects"
    else:
        decision = "unknown"

    return decision, str(policy.get("reason", "unknown"))


def candidate_reason(candidate):
    policy = candidate_layer(candidate).get("policy")
    if isinstance(policy, dict):
        return str(policy.get("reason", "unknown"))
    return "unknown"


def candidate_risk(candidate):
    risk = candidate_layer(candidate).get("candidateRisk")
    return str(risk) if risk else "unknown"


def candidate_families(candidate):
    families = candidate_layer(candidate).get("candidateFamilies")
    return [str(family) for family in as_list(families)]


def candidate_mix_families(candidate):
    families = candidate_layer(candidate).get("candidateMixFamilies")
    return [str(family) for family in as_list(families)]


def candidate_chain_shape(candidate):
    shape = candidate_layer(candidate).get("candidateChainShape")
    return str(shape) if shape else "unknown"


def is_allowed_simple_water_layer(layer):
    if not isinstance(layer, dict):
        return False
    if layer.get("candidateRisk") != "simple-water":
        return False
    policy = layer.get("policy")
    if not isinstance(policy, dict):
        return False
    return policy.get("keepEffects") is True and policy.get("strippedEffects") is not True


def main():
    if len(sys.argv) != 2:
        print("usage: tools/effect-capture-summary.py /path/to/manifest.json", file=sys.stderr)
        return 2

    manifest_path = Path(sys.argv[1])
    with manifest_path.open("r", encoding="utf-8") as handle:
        manifest = json.load(handle)

    captures = as_list(manifest.get("captures"))
    pass_states = as_list(manifest.get("passStates"))
    stripped_candidates = as_list(manifest.get("strippedCandidates"))
    layers = unique_layers(as_list(manifest.get("layers")), captures)
    failures = capture_failures(as_list(manifest.get("failures")), captures)

    stage_counts = Counter(get_stage(record) for record in captures)
    layer_stage_counts = defaultdict(Counter)
    for record in captures:
        layer_stage_counts[capture_key(record)][get_stage(record)] += 1

    print(f"manifest={manifest_path}")
    print(f"scene={manifest.get('sceneId', 'unknown')}")
    print(f"wallpaper={manifest.get('wallpaperPath', 'unknown')}")
    print(f"captures={len(captures)} stages={dict(sorted(stage_counts.items()))}")
    print(f"layers={len(layers)} passStates={len(pass_states)} failures={len(failures)} strippedCandidates={len(stripped_candidates)}")

    if failures:
        print("failures:")
        for failure in failures:
            print(f"  - stage={failure.get('stage', 'unknown')} path={failure.get('path', 'unknown')} reason={failure.get('reason', 'unknown')}")

    decisions = Counter()
    for layer in layers:
        decisions[decision_for_layer(layer)] += 1

    if decisions:
        print("decisions:")
        for (decision, reason), count in sorted(decisions.items()):
            print(f"  - {decision} reason={reason} count={count}")

    allowed_simple_water_layers = [
        layer for layer in layers
        if is_allowed_simple_water_layer(layer)
    ]
    if allowed_simple_water_layers:
        print(f"allowed-simple-water-candidates={len(allowed_simple_water_layers)}")

        family_counts = Counter()
        for layer in allowed_simple_water_layers:
            for family in candidate_families(layer):
                family_counts[family] += 1
        if family_counts:
            print("allowed-simple-water-families:")
            for family, count in sorted(family_counts.items()):
                print(f"  - {family}: {count}")

        print("allowed-simple-water-layers:")
        for layer in allowed_simple_water_layers[:10]:
            policy = layer.get("policy") if isinstance(layer.get("policy"), dict) else {}
            layer_id = layer.get("layerId", "unknown")
            layer_name = layer.get("layerName") or "unnamed"
            reason = str(policy.get("reason", "unknown"))
            families = ",".join(candidate_families(layer)) or "none"
            effects = len(as_list(layer.get("effectNames")))
            shaders = len(as_list(layer.get("materialShaders")))
            print(f"  - {layer_id}:{layer_name} reason={reason} families={families} effects={effects} shaders={shaders}")
        if len(allowed_simple_water_layers) > 10:
            print(f"  ... {len(allowed_simple_water_layers) - 10} more")

    if stripped_candidates:
        reasons = Counter(candidate_reason(candidate) for candidate in stripped_candidates)
        print("stripped-candidate-reasons:")
        for reason, count in sorted(reasons.items()):
            print(f"  - {reason}: {count}")

        risks = Counter(candidate_risk(candidate) for candidate in stripped_candidates)
        print("stripped-candidate-risks:")
        for risk, count in sorted(risks.items()):
            print(f"  - {risk}: {count}")

        shapes = Counter(candidate_chain_shape(candidate) for candidate in stripped_candidates)
        print("stripped-candidate-chain-shapes:")
        for shape, count in sorted(shapes.items()):
            print(f"  - {shape}: {count}")

        family_counts = Counter()
        for candidate in stripped_candidates:
            for family in candidate_families(candidate):
                family_counts[family] += 1
        if family_counts:
            print("stripped-candidate-families:")
            for family, count in sorted(family_counts.items()):
                print(f"  - {family}: {count}")

        mix_counts = Counter()
        for candidate in stripped_candidates:
            for family in candidate_mix_families(candidate):
                mix_counts[family] += 1
        if mix_counts:
            print("stripped-candidate-mix-families:")
            for family, count in sorted(mix_counts.items()):
                print(f"  - {family}: {count}")

        print("stripped-candidate-layers:")
        for candidate in stripped_candidates[:10]:
            layer = candidate_layer(candidate)
            policy = layer.get("policy") if isinstance(layer.get("policy"), dict) else {}
            layer_id = layer.get("layerId", "unknown")
            layer_name = layer.get("layerName") or "unnamed"
            reason = str(policy.get("reason", "unknown"))
            risk = candidate_risk(candidate)
            shape = candidate_chain_shape(candidate)
            families = ",".join(candidate_families(candidate)) or "none"
            mix = ",".join(candidate_mix_families(candidate)) or "none"
            effects = len(as_list(layer.get("effectNames")))
            shaders = len(as_list(layer.get("materialShaders")))
            print(f"  - {layer_id}:{layer_name} reason={reason} risk={risk} shape={shape} families={families} mix={mix} effects={effects} shaders={shaders}")
        if len(stripped_candidates) > 10:
            print(f"  ... {len(stripped_candidates) - 10} more")

    if layer_stage_counts:
        print("layer-stage-counts:")
        for key, counts in sorted(layer_stage_counts.items()):
            print(f"  - {key}: {dict(sorted(counts.items()))}")

    if pass_states:
        print("pass-states:")
        for state in pass_states:
            name = state.get("passName", state.get("name", state.get("output", "unknown")))
            src = state.get("sourceTexture", state.get("src", "unknown"))
            dst = state.get("targetTexture", state.get("dst", state.get("output", "unknown")))
            load = state.get("colorLoadOp", state.get("loadOp", "unknown"))
            blend = state.get("blendMode", "unknown")
            preserve = state.get("preserveOutput", state.get("preserve", "unknown"))
            color_mask = state.get("colorMask", "unknown")
            depth_load = state.get("depthLoadOp", "unknown")
            blend_enabled = state.get("blendEnabled", "unknown")
            uses_depth = state.get("usesDepth", "unknown")
            print(f"  - name={name} src={src} dst={dst} load={load} blend={blend} preserve={preserve} colorMask={color_mask} depthLoad={depth_load} blendEnabled={blend_enabled} usesDepth={uses_depth}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
