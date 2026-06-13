#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path


PRIORITY_ORDER = {"review": 0, "defer": 1, "ignore": 2}
TIME_OF_DAY_SIGNALS = ("shared.showday", "shared.showsunset", "shared.shownight")
WEATHER_USERS = {"weather"}
MEDIA_USERS = {"mediaintegration"}
UI_USERS = {"clock", "mousecursor", "loadingintro"}


def _as_dict(value):
    return value if isinstance(value, dict) else {}


def _as_list(value):
    return value if isinstance(value, list) else []


def _text(value):
    return str(value or "")


def _lower_blob(*values):
    return " ".join(_text(value).lower() for value in values)


def _has_token(value, token):
    return re.search(rf"(^|[^a-z0-9]){re.escape(token.lower())}([^a-z0-9]|$)", _text(value).lower()) is not None


def _visible_script(visible):
    return _text(visible.get("script")) if isinstance(visible, dict) else ""


def _visible_user(visible):
    if not isinstance(visible, dict):
        return ""
    user = visible.get("user")
    if isinstance(user, dict):
        return _text(user.get("name"))
    return _text(user)


def _visibility_kind(visible, fallback="script-gated"):
    if visible is False:
        return "authored-off"
    if isinstance(visible, dict):
        if _visible_user(visible):
            return "user-gated"
        if _visible_script(visible):
            return "script-gated"
    return fallback


def _priority_key(priority):
    return PRIORITY_ORDER.get(priority, 99)


def _stronger_priority(left, right):
    return left if _priority_key(left) <= _priority_key(right) else right


def _has_weighted_motion(animation):
    if animation.get("visibleAndWeighted") is True:
        return True
    try:
        if int(animation.get("activeBoneSlotCount", 0)) > 0:
            return True
    except (TypeError, ValueError):
        pass
    for key in ("weightedMotion", "weightedMotions", "motionWeights", "weightedMotionSlots"):
        value = animation.get(key)
        if isinstance(value, dict) and value:
            return True
        if isinstance(value, list) and value:
            return True
        if isinstance(value, bool) and value:
            return True
    return False


def classify_scene_object(obj):
    visible = obj.get("visible")
    name = _text(obj.get("name"))
    image = _text(obj.get("image"))
    script = _visible_script(visible)
    user = _visible_user(visible)
    blob = _lower_blob(name, image)
    script_signals = [signal for signal in TIME_OF_DAY_SIGNALS if signal in script]

    record = {
        "layerId": obj.get("id"),
        "name": name,
        "visibilityKind": "",
        "candidateBucket": "",
        "reviewPriority": "",
        "reason": "",
        "scriptSignals": script_signals,
        "userBinding": user,
        "effectsCount": len(_as_list(obj.get("effects"))),
        "image": image,
    }

    if "solid==day night script==" in name.lower():
        record.update({
            "visibilityKind": "authored-off",
            "candidateBucket": "scene-script-utility",
            "reviewPriority": "ignore",
            "reason": "scene script utility layer",
        })
    elif script_signals:
        record.update({
            "visibilityKind": "script-gated",
            "candidateBucket": "time-of-day",
            "reviewPriority": "review",
            "reason": "time-of-day visibility script",
        })
    elif user.lower() in MEDIA_USERS or "mediaplaybackevent" in script.lower() or "media" in blob or "audio" in blob:
        record.update({
            "visibilityKind": _visibility_kind(visible),
            "candidateBucket": "media-runtime",
            "reviewPriority": "defer",
            "reason": "media runtime layer",
        })
    elif user.lower() in WEATHER_USERS or _has_token(name, "rain"):
        record.update({
            "visibilityKind": "user-gated",
            "candidateBucket": "weather-rain",
            "reviewPriority": "defer",
            "reason": "weather or rain user-gated layer",
        })
    elif user.lower() in UI_USERS or "clock" in name.lower() or "mouse" in name.lower():
        record.update({
            "visibilityKind": "user-gated",
            "candidateBucket": "ui-user-setting",
            "reviewPriority": "defer",
            "reason": "ui user setting layer",
        })
    elif visible is False:
        record.update({
            "visibilityKind": "authored-off",
            "candidateBucket": "unknown-visual",
            "reviewPriority": "review",
            "reason": "literal false visual object",
        })
    elif isinstance(visible, dict) and (script or user):
        record.update({
            "visibilityKind": _visibility_kind(visible),
            "candidateBucket": "unknown-gated",
            "reviewPriority": "review",
            "reason": "unclassified gated visibility",
        })

    return record


def load_scene(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def load_manifest(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def _manifest_layer_entries(manifest):
    entries = []
    for record in _as_list(manifest.get("strippedCandidates")):
        layer_id = record.get("layerId")
        if layer_id is not None:
            entries.append((layer_id, "strippedCandidates", record))
    for record in _as_list(manifest.get("protectedPuppetDiagnostics")):
        layer_id = record.get("layerId")
        if layer_id is not None:
            entries.append((layer_id, "protectedPuppetDiagnostics", record))
    for capture in _as_list(manifest.get("captures")):
        layer = _as_dict(capture.get("layer"))
        layer_id = layer.get("layerId")
        if layer_id is not None:
            entries.append((layer_id, "captures", capture))
    for record in _as_list(manifest.get("puppetAnimationLayerInventory")):
        if not isinstance(record, dict):
            continue
        layer_id = record.get("layerId")
        if layer_id is not None:
            entries.append((layer_id, "puppetAnimationLayerInventory", record))
    return entries


def _classify_puppet_animation(animation):
    visible = animation.get("visible")
    active_slots = _as_list(animation.get("activeBoneSlots"))
    has_weighted_motion = _has_weighted_motion(animation)
    priority = "review" if active_slots or has_weighted_motion else "defer"
    visibility_kind = "inactive-puppet-animation" if visible is False else "active-puppet-animation"
    return {
        "animationId": animation.get("animationId"),
        "animationName": _text(animation.get("animationName")),
        "visible": visible,
        "visibilityKind": visibility_kind,
        "candidateBucket": "puppet-animation",
        "reviewPriority": priority if visible is False else "ignore",
        "activeBoneSlots": active_slots,
        "hasWeightedMotion": has_weighted_motion,
    }


def _manifest_summary_for_layer(manifest, layer_id):
    result = {}
    stripped = []
    protected = []
    puppet_animations = []
    layer_name = ""

    for entry_layer_id, entry_kind, entry in _manifest_layer_entries(manifest):
        if entry_layer_id != layer_id:
            continue
        if entry_kind == "strippedCandidates":
            stripped.append(entry)
        elif entry_kind == "protectedPuppetDiagnostics":
            protected.append(entry)
        elif entry_kind == "captures":
            layer = _as_dict(entry.get("layer"))
            layer_name = layer_name or _text(layer.get("layerName"))
            for animation in _as_list(layer.get("puppetAnimationLayerInventory")):
                if isinstance(animation, dict):
                    puppet_animations.append(_classify_puppet_animation(animation))
        elif entry_kind == "puppetAnimationLayerInventory":
            layer_name = layer_name or _text(entry.get("layerName"))
            for animation in _as_list(entry.get("puppetAnimationLayers")):
                if isinstance(animation, dict):
                    puppet_animations.append(_classify_puppet_animation(animation))

    if layer_name and (stripped or protected or puppet_animations):
        result["layerName"] = layer_name
    if stripped:
        result["strippedCandidates"] = stripped
    if protected:
        result["protectedPuppetDiagnostics"] = protected
    if puppet_animations:
        result["puppetAnimations"] = sorted(
            puppet_animations,
            key=lambda item: (item.get("animationId") is None, item.get("animationId") or 0),
        )
    return result


def _objects_by_id(scene):
    return {
        obj.get("id"): obj
        for obj in _as_list(scene.get("objects"))
        if isinstance(obj, dict) and obj.get("id") is not None
    }


def _manifest_layer_ids(manifests):
    layer_ids = set()
    for manifest in manifests.values():
        for layer_id, _entry_kind, _entry in _manifest_layer_entries(manifest):
            layer_ids.add(layer_id)
    return layer_ids


def _manifest_priority(manifest_record):
    priority = "ignore"
    for variant in manifest_record.values():
        for animation in _as_list(_as_dict(variant).get("puppetAnimations")):
            if animation.get("visible") is False:
                priority = _stronger_priority(priority, animation.get("reviewPriority", "ignore"))
        if _as_dict(variant).get("strippedCandidates"):
            priority = _stronger_priority(priority, "review")
        if _as_dict(variant).get("protectedPuppetDiagnostics"):
            priority = _stronger_priority(priority, "defer")
    return priority


def build_report(scene, manifests):
    objects = _objects_by_id(scene)
    layer_ids = set(objects)
    layer_ids.update(_manifest_layer_ids(manifests))

    summary = {
        "review": 0,
        "defer": 0,
        "ignore": 0,
        "strippedCandidates": sum(len(_as_list(manifest.get("strippedCandidates"))) for manifest in manifests.values()),
        "protectedPuppetDiagnostics": sum(
            len(_as_list(manifest.get("protectedPuppetDiagnostics"))) for manifest in manifests.values()
        ),
    }
    layers = []

    for layer_id in layer_ids:
        obj = objects.get(layer_id, {})
        object_record = classify_scene_object(obj) if obj else {
            "layerId": layer_id,
            "name": "",
            "visibilityKind": "",
            "candidateBucket": "",
            "reviewPriority": "",
            "reason": "",
            "scriptSignals": [],
            "userBinding": "",
            "effectsCount": 0,
            "image": "",
        }

        manifest_record = {
            variant: summary_for_layer
            for variant, manifest in sorted(manifests.items())
            for summary_for_layer in [_manifest_summary_for_layer(manifest, layer_id)]
            if summary_for_layer
        }

        object_priority = object_record.get("reviewPriority") or "ignore"
        manifest_priority = _manifest_priority(manifest_record)
        priority = _stronger_priority(object_priority, manifest_priority)
        include = bool(object_record.get("visibilityKind")) or bool(manifest_record)
        if not include:
            continue

        layer = {
            "layerId": object_record.get("layerId", layer_id),
            "name": object_record.get("name") or _layer_name_from_manifest(manifest_record),
            "visibilityKind": object_record.get("visibilityKind"),
            "candidateBucket": object_record.get("candidateBucket") or _manifest_bucket(manifest_record),
            "reviewPriority": priority,
            "reason": object_record.get("reason") or _manifest_reason(manifest_record),
            "scriptSignals": object_record.get("scriptSignals", []),
            "userBinding": object_record.get("userBinding", ""),
            "effectsCount": object_record.get("effectsCount", 0),
            "image": object_record.get("image", ""),
            "manifest": manifest_record,
        }
        layers.append(layer)
        summary[priority] += 1

    layers.sort(key=lambda item: (_priority_key(item["reviewPriority"]), item.get("layerId") or 0))
    return {"summary": summary, "layers": layers}


def _layer_name_from_manifest(manifest_record):
    for variant in manifest_record.values():
        if variant.get("layerName"):
            return _text(variant.get("layerName"))
        for collection_name in ("strippedCandidates", "protectedPuppetDiagnostics"):
            for record in _as_list(variant.get(collection_name)):
                if record.get("layerName"):
                    return _text(record.get("layerName"))
    return ""


def _manifest_bucket(manifest_record):
    for variant in manifest_record.values():
        if variant.get("puppetAnimations"):
            return "puppet-animation"
        if variant.get("strippedCandidates"):
            return "stripped-candidate"
        if variant.get("protectedPuppetDiagnostics"):
            return "protected-puppet"
    return ""


def _manifest_reason(manifest_record):
    for variant in manifest_record.values():
        if variant.get("puppetAnimations"):
            return "manifest puppet animation inventory"
        if variant.get("strippedCandidates"):
            return "manifest stripped candidate"
        if variant.get("protectedPuppetDiagnostics"):
            return "manifest protected puppet diagnostic"
    return ""


def _markdown_row(layer):
    image = layer.get("image") or "(none)"
    return (
        f"- layer {layer.get('layerId')}: {layer.get('name') or '(unnamed)'} "
        f"[{layer.get('candidateBucket')}; {layer.get('visibilityKind') or 'manifest'}] "
        f"- priority={layer.get('reviewPriority')} effects={layer.get('effectsCount')} "
        f"image={image} - {layer.get('reason')}"
    )


def render_markdown(report):
    lines = [
        "# Arona Disabled/Gated Layer Inventory",
        "",
        "## Summary",
    ]
    summary = report.get("summary", {})
    for key in ("review", "defer", "ignore", "strippedCandidates", "protectedPuppetDiagnostics"):
        if key in summary:
            lines.append(f"- {key}: {summary.get(key, 0)}")

    sections = [
        ("review", "## Review Candidates"),
        ("defer", "## Deferred / Intentional"),
        ("ignore", "## Ignored Utilities"),
    ]
    for priority, title in sections:
        lines.extend(["", title])
        matching = [layer for layer in report.get("layers", []) if layer.get("reviewPriority") == priority]
        if not matching:
            lines.append("- none")
            continue
        for layer in matching:
            lines.append(_markdown_row(layer))

    return "\n".join(lines) + "\n"


def _parse_manifest_args(items):
    manifests = {}
    for item in items:
        if "=" not in item:
            raise ValueError(f"manifest must be variant=path: {item}")
        variant, path = item.split("=", 1)
        variant = variant.strip()
        if not variant:
            raise ValueError(f"manifest variant is empty: {item}")
        manifests[variant] = load_manifest(Path(path))
    return manifests


def _write_json(path, payload):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _write_text(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="Inventory disabled and gated Arona scene layers")
    parser.add_argument("--scene", default="yakkai_arona/scene/scene.json", help="scene.json path")
    parser.add_argument(
        "--manifest",
        action="append",
        default=[],
        metavar="VARIANT=PATH",
        help="effect-capture manifest path, repeatable",
    )
    parser.add_argument("--output-json", help="write JSON report to path")
    parser.add_argument("--output-md", help="write Markdown report to path")
    args = parser.parse_args()

    scene = load_scene(Path(args.scene))
    manifests = _parse_manifest_args(args.manifest)
    report = build_report(scene, manifests)

    if args.output_json:
        _write_json(Path(args.output_json), report)
    if args.output_md:
        _write_text(Path(args.output_md), render_markdown(report))
    if not args.output_json and not args.output_md:
        print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
