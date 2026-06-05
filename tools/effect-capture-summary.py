#!/usr/bin/env python3
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path


HIGH_RISK_ALIASES = (
    ("blur", "blur"),
    ("lut_loader", "lut"),
    ("lut", "lut"),
    ("color_grading", "color-grade"),
    ("color grading", "color-grade"),
    ("colorgrading", "color-grade"),
    ("colorgrade", "color-grade"),
    ("colorcorrection", "color-grade"),
)


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


def candidate_identity(candidate):
    layer = candidate_layer(candidate)
    return (
        str(layer.get("layerId", "unknown")),
        str(layer.get("layerName") or "unnamed"),
        candidate_chain_shape(candidate),
    )


def candidate_effect_class(candidate):
    layer = candidate_layer(candidate)
    explicit = layer.get("candidateEffectClass")
    if explicit:
        return str(explicit)

    shape = candidate_chain_shape(candidate)
    families = candidate_high_risk_families(candidate)
    has_blur = "blur" in families
    has_lut = "lut" in families
    has_color = "color-grade" in families
    if not has_blur and not has_lut and not has_color:
        return "none"
    if has_blur and not has_lut and not has_color:
        if shape == "protected-puppet-mixed":
            return "protected-puppet-blur"
        if shape == "puppet-mixed":
            return "mixed-puppet-blur"
        if "composelayer" in shape:
            return "composelayer-blur"
        if "utility" in shape:
            return "utility-blur"
        if "fullscreen" in shape:
            return "fullscreen-blur"
        if shape == "blur-only":
            return "regular-blur-only"
        return "mixed-blur"
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
    return "mixed-color-grade"


def is_lut_color_effect_class(effect_class):
    return "lut" in effect_class or "color" in effect_class or "color-grade" in effect_class


def candidate_debug_probe(candidate):
    layer = candidate_layer(candidate)
    probe = layer.get("debugProbe")
    if isinstance(probe, dict):
        return probe
    probe = candidate.get("debugProbe") if isinstance(candidate, dict) else {}
    return probe if isinstance(probe, dict) else {}


def candidate_disposition(candidate, source):
    effect_class = candidate_effect_class(candidate)
    policy = candidate_layer(candidate).get("policy")
    if not isinstance(policy, dict):
        policy = {}
    if effect_class.startswith("protected-puppet-"):
        return "protected"
    if candidate_debug_probe(candidate).get("overrodePolicy") is True:
        return "probe-only"
    if source == "capture" and policy.get("keepEffects") is not False:
        return "allowed"
    if policy.get("strippedEffects") is True or source == "stripped":
        return "stripped"
    return "unknown"


def append_unique(values, value):
    if value not in values:
        values.append(value)


def high_risk_family_from_text(value):
    lowered = str(value).lower()
    for alias, family in HIGH_RISK_ALIASES:
        if alias in lowered:
            return family
    return None


def canonical_high_risk_family(value):
    text = str(value).lower()
    if text in {"blur", "lut", "color-grade"}:
        return text
    return high_risk_family_from_text(text)


def candidate_high_risk_families(candidate):
    families = []
    for family in candidate_mix_families(candidate):
        canonical = canonical_high_risk_family(family)
        if canonical:
            append_unique(families, canonical)

    if families:
        return families

    layer = candidate_layer(candidate)
    for field in ("effectNames", "materialShaders"):
        for value in as_list(layer.get(field)):
            family = high_risk_family_from_text(value)
            if family:
                append_unique(families, family)
    return families


def is_allowed_simple_water_layer(layer):
    if not isinstance(layer, dict):
        return False
    if layer.get("candidateRisk") != "simple-water":
        return False
    policy = layer.get("policy")
    if not isinstance(policy, dict):
        return False
    return policy.get("keepEffects") is True and policy.get("strippedEffects") is not True


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


def slot_coverage_text(layer):
    publish = layer.get("publish")
    if not isinstance(publish, dict):
        return "none"

    def count(slot, field, legacy_field=None):
        value = slot.get(field)
        if value is None and legacy_field:
            value = slot.get(legacy_field)
        return value if value is not None else 0

    def format_number(value):
        try:
            number = float(value)
        except (TypeError, ValueError):
            return str(value)
        if number.is_integer():
            return str(int(number))
        return f"{number:.6g}"

    def simulation_text(slot):
        if slot.get("simulationMetadataPresent") is not True:
            return ":sim=no"

        parts = [":sim=yes"]
        if "simulationMetadataValid" in slot:
            parts.append(
                ":simValid="
                + ("yes" if slot.get("simulationMetadataValid") is True else "no")
            )
        if "simulationPhysicsActive" in slot:
            parts.append(
                ":simPhysics="
                + ("yes" if slot.get("simulationPhysicsActive") is True else "no")
            )
        target_point = slot.get("simulationTargetPoint")
        if slot.get("simulationTargetPointPresent") is True and isinstance(target_point, list):
            parts.append(":simTp=" + "/".join(format_number(value) for value in target_point))
        if slot.get("simulationTargetMassPresent") is True:
            parts.append(":simTm=" + format_number(slot.get("simulationTargetMass")))
        if slot.get("simulatedInactive") is True:
            parts.append(":simInactive=yes")
        return "".join(parts)

    def slot_text(slot):
        slot_id = slot.get("slot", "unknown")
        marker = "*" if slot.get("active") is True else ""
        name = str(slot.get("boneName") or "unnamed")
        parent_name = str(slot.get("parentBoneName") or "none")
        parent_slot = slot.get("parentSlot", "unknown")
        primary_vertices = count(slot, "primaryVertexCount", "vertexCount")
        primary_triangles = count(slot, "primaryTriangleCount", "triangleCount")
        weighted_vertices = count(slot, "weightedVertexCount", "vertexCount")
        weighted_triangles = count(slot, "weightedTriangleCount", "triangleCount")
        secondary = ":secondary-only" if slot.get("secondaryOnly") is True else ""
        return (
            f"{slot_id}{marker}:{name}[{parent_name}#{parent_slot}]"
            f":primary={primary_vertices}v/{primary_triangles}t"
            f":weighted={weighted_vertices}v/{weighted_triangles}t"
            f"{secondary}{simulation_text(slot)}"
        )

    parts = []
    for slot in as_list(publish.get("puppetCutoutSlotCoverage")):
        if isinstance(slot, dict):
            parts.append(slot_text(slot))
    return ",".join(parts) if parts else "none"


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

        high_risk_candidates = [
            (candidate, candidate_high_risk_families(candidate))
            for candidate in stripped_candidates
        ]
        high_risk_candidates = [
            (candidate, families)
            for candidate, families in high_risk_candidates
            if families
        ]
        if high_risk_candidates:
            print(f"stripped-high-risk-candidates={len(high_risk_candidates)}")
            high_risk_counts = Counter()
            for _, families in high_risk_candidates:
                for family in families:
                    high_risk_counts[family] += 1
            print("stripped-high-risk-families:")
            for family, count in sorted(high_risk_counts.items()):
                print(f"  - {family}: {count}")

            print("stripped-high-risk-layers:")
            for candidate, families in high_risk_candidates[:10]:
                layer = candidate_layer(candidate)
                layer_id = layer.get("layerId", "unknown")
                layer_name = layer.get("layerName") or "unnamed"
                risk = candidate_risk(candidate)
                shape = candidate_chain_shape(candidate)
                effects = len(as_list(layer.get("effectNames")))
                shaders = len(as_list(layer.get("materialShaders")))
                joined = ",".join(families) or "none"
                print(f"  - {layer_id}:{layer_name} risk={risk} shape={shape} families={joined} effects={effects} shaders={shaders}")
            if len(high_risk_candidates) > 10:
                print(f"  ... {len(high_risk_candidates) - 10} more")

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

    lut_color_entries = []
    for layer in layers:
        effect_class = candidate_effect_class(layer)
        if is_lut_color_effect_class(effect_class):
            lut_color_entries.append(("capture", layer, effect_class))
    for candidate in stripped_candidates:
        effect_class = candidate_effect_class(candidate)
        if is_lut_color_effect_class(effect_class):
            lut_color_entries.append(("stripped", candidate, effect_class))

    if lut_color_entries:
        class_counts = Counter(effect_class for _, _, effect_class in lut_color_entries)
        disposition_counts = Counter(
            candidate_disposition(candidate, source)
            for source, candidate, _ in lut_color_entries
        )
        print("lut-color-class-counts:")
        for effect_class, count in sorted(class_counts.items()):
            print(f"  - {effect_class}: {count}")
        print("lut-color-disposition-counts:")
        for disposition, count in sorted(disposition_counts.items()):
            print(f"  - {disposition}: {count}")
        print("lut-color-layers:")
        for source, candidate, effect_class in lut_color_entries[:12]:
            layer = candidate_layer(candidate)
            policy = layer.get("policy") if isinstance(layer.get("policy"), dict) else {}
            layer_id = layer.get("layerId", "unknown")
            layer_name = layer.get("layerName") or "unnamed"
            disposition = candidate_disposition(candidate, source)
            reason = str(policy.get("reason", layer.get("policyReason", "unknown")))
            shape = candidate_chain_shape(candidate)
            blocked = layer.get("candidateBlockedReason") or "none"
            print(
                f"  - {layer_id}:{layer_name} class={effect_class} "
                f"disposition={disposition} reason={reason} shape={shape} blocked={blocked}"
            )
        if len(lut_color_entries) > 12:
            print(f"  ... {len(lut_color_entries) - 12} more")

    high_risk_entries = []
    high_risk_seen = set()
    for layer in layers:
        families = candidate_high_risk_families(layer)
        if families:
            high_risk_seen.add(candidate_identity(layer))
            high_risk_entries.append(("capture", layer, families, candidate_effect_class(layer)))
    for candidate in stripped_candidates:
        families = candidate_high_risk_families(candidate)
        if families:
            identity = candidate_identity(candidate)
            if identity in high_risk_seen:
                continue
            high_risk_seen.add(identity)
            high_risk_entries.append(("stripped", candidate, families, candidate_effect_class(candidate)))

    if high_risk_entries:
        disposition_counts = Counter(
            candidate_disposition(candidate, source)
            for source, candidate, _, _ in high_risk_entries
        )
        shape_counts = Counter(candidate_chain_shape(candidate)
                               for _, candidate, _, _ in high_risk_entries)
        print("high-risk-disposition-counts:")
        for disposition, count in sorted(disposition_counts.items()):
            print(f"  - {disposition}: {count}")
        print("high-risk-shape-counts:")
        for shape, count in sorted(shape_counts.items()):
            print(f"  - {shape}: {count}")
        print("high-risk-probe-layers:")
        for source, candidate, families, effect_class in high_risk_entries[:12]:
            layer = candidate_layer(candidate)
            policy = layer.get("policy") if isinstance(layer.get("policy"), dict) else {}
            layer_id = layer.get("layerId", "unknown")
            layer_name = layer.get("layerName") or "unnamed"
            disposition = candidate_disposition(candidate, source)
            reason = str(policy.get("reason", layer.get("policyReason", "unknown")))
            shape = candidate_chain_shape(candidate)
            blocked = layer.get("candidateBlockedReason") or "none"
            joined = ",".join(families) or "none"
            print(
                f"  - {layer_id}:{layer_name} families={joined} class={effect_class} "
                f"disposition={disposition} reason={reason} shape={shape} blocked={blocked}"
            )
        if len(high_risk_entries) > 12:
            print(f"  ... {len(high_risk_entries) - 12} more")

    protected_rows = []
    protected_seen = set()
    for source, candidates in (
        ("capture", layers),
        ("stripped", stripped_candidates),
        ("protected", as_list(manifest.get("protectedPuppetDiagnostics"))),
    ):
        for candidate in candidates:
            layer = candidate_layer(candidate)
            checks = layer.get("candidateChecks")
            is_protected = isinstance(checks, dict) and checks.get("isProtectedPuppetPath") is True
            if not is_protected and not str(layer.get("candidateChainShape", "")).startswith("protected-puppet"):
                continue
            key = (
                source,
                str(layer.get("layerId", "unknown")),
                str(layer.get("candidateChainShape", "unknown")),
            )
            if key in protected_seen:
                continue
            protected_seen.add(key)
            protected_rows.append((source, layer))

    if protected_rows:
        print(f"protected-puppet-cutout-inventory={len(protected_rows)}")
        for source, layer in protected_rows[:20]:
            print(
                "  "
                f"source={source} "
                f"layer={layer.get('layerId', 'unknown')} "
                f"name={str(layer.get('layerName') or 'unnamed')!r} "
                f"policy={candidate_reason(layer)} "
                f"class={candidate_effect_class(layer)} "
                f"shape={candidate_chain_shape(layer)} "
                f"activeAnimations={','.join(active_animation_ids(layer)) or 'none'} "
                f"activeSlots={','.join(active_bone_slots(layer)) or 'none'} "
                f"slotCoverage={slot_coverage_text(layer)}"
            )
        if len(protected_rows) > 20:
            print(f"  ... {len(protected_rows) - 20} more")

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
