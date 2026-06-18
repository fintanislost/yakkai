#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path

MEDIA_NAME_RE = re.compile(
    r"(media|artist|song|album|clock|thumbnail|cover|text container|holder|rounded|background)",
    re.I,
)

API_PATTERNS = {
    "engine.setTimeout": r"\bengine\.setTimeout\b",
    "engine.registerAudioBuffers": r"\bengine\.registerAudioBuffers\b",
    "thisLayer.getParent": r"\bthisLayer\.getParent\b",
    "thisLayer.getTransformMatrix": r"\bthisLayer\.getTransformMatrix\b",
    "thisScene.getLayer": r"\bthisScene\.getLayer\b",
    "getTransformMatrix": r"\.getTransformMatrix\b",
    "thisObject.getAnimation": r"\bthisObject\.getAnimation\b",
    "$mediaThumbnail": r"\$mediaThumbnail\b",
    "$mediaPreviousThumbnail": r"\$mediaPreviousThumbnail\b",
}

CALLBACK_PATTERNS = {
    "mediaPlaybackChanged": r"\bmediaPlaybackChanged\b",
    "mediaPropertiesChanged": r"\bmediaPropertiesChanged\b",
    "mediaThumbnailChanged": r"\bmediaThumbnailChanged\b",
    "mediaTimelineChanged": r"\bmediaTimelineChanged\b",
    "init": r"\bfunction\s+init\b",
    "update": r"\bfunction\s+update\b",
}

SIDE_EFFECT_PATTERNS = {
    "thisLayer.visible": r"\bthisLayer\.visible\s*=",
    "thisLayer.origin": r"\bthisLayer\.origin\s*=",
    "thisLayer.scale": r"\bthisLayer\.scale\s*=",
    "thisLayer.color": r"\bthisLayer\.color\s*=",
    "thisLayer.alpha": r"\bthisLayer\.alpha\s*=",
    "thisObject.getAnimation.play": r"\bthisObject\.getAnimation\(\)\.play\(",
}


def classify_script(script):
    return {
        "callbacks": sorted(
            name for name, pattern in CALLBACK_PATTERNS.items() if re.search(pattern, script)
        ),
        "apis": sorted(
            name for name, pattern in API_PATTERNS.items() if re.search(pattern, script)
        ),
        "sideEffects": sorted(
            name
            for name, pattern in SIDE_EFFECT_PATTERNS.items()
            if re.search(pattern, script)
        ),
        "lineCount": len(script.splitlines()),
    }


def iter_script_fields(value, prefix=""):
    if isinstance(value, dict):
        if isinstance(value.get("script"), str):
            yield prefix.rstrip("."), value["script"]
        for key, child in value.items():
            if key == "script":
                continue
            yield from iter_script_fields(child, f"{prefix}{key}.")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from iter_script_fields(child, f"{prefix}[{index}].")


def normalize_effect_field(field, obj):
    field = field.replace(".[", "[")
    effects = obj.get("effects") or []
    for effect_index, effect in enumerate(effects):
        effect_id = effect.get("id", effect_index)
        field = field.replace(f"effects[{effect_index}]", f"effects[{effect_id}]")
        for pass_index, pass_obj in enumerate(effect.get("passes") or []):
            pass_id = pass_obj.get("id", pass_index)
            field = field.replace(f"passes[{pass_index}]", f"passes[{pass_id}]")
    return field.rstrip(".")


def audit_scene(scene_path):
    scene = json.loads(Path(scene_path).read_text(encoding="utf-8"))
    scripts = []
    for obj in scene.get("objects", []):
        name = str(obj.get("name", ""))
        layer_id = obj.get("id")
        layer_has_media_name = bool(MEDIA_NAME_RE.search(name))
        for field, script in iter_script_fields(obj):
            usage = classify_script(script)
            is_media_script = layer_has_media_name or bool(
                set(usage["callbacks"])
                & {
                    "mediaPlaybackChanged",
                    "mediaPropertiesChanged",
                    "mediaThumbnailChanged",
                    "mediaTimelineChanged",
                }
            )
            if not is_media_script:
                continue
            scripts.append(
                {
                    "layerId": layer_id,
                    "layerName": name,
                    "parent": obj.get("parent"),
                    "field": normalize_effect_field(field, obj),
                    "usage": usage,
                }
            )
    return {"scene": str(scene_path), "scriptCount": len(scripts), "scripts": scripts}


def write_markdown(result, path):
    lines = [
        "# Media Widget Script Audit",
        "",
        f"- Scene: `{result['scene']}`",
        f"- Scripts: `{result['scriptCount']}`",
        "",
        "| layer | field | callbacks | APIs | side effects |",
        "| --- | --- | --- | --- | --- |",
    ]
    for entry in result["scripts"]:
        usage = entry["usage"]
        layer = f"`{entry['layerId']} {entry['layerName']}`"
        lines.append(
            f"| {layer} | `{entry['field']}` | "
            f"`{', '.join(usage['callbacks'])}` | "
            f"`{', '.join(usage['apis'])}` | "
            f"`{', '.join(usage['sideEffects'])}` |"
        )
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("scene_json", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    result = audit_scene(args.scene_json)
    if args.json:
        args.json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.markdown:
        write_markdown(result, args.markdown)
    if not args.json and not args.markdown:
        print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
