#!/usr/bin/env python3
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import re
import sys


SCRIPT_GAP_RE = re.compile(
    r"SceneScript gap:\s+layer=(?P<layer>-?\d+)\s+class=(?P<class>\S+)\s+"
    r"api=(?P<api>\S+)\s+reason=(?P<reason>\S+)\s+message=(?P<message>.*)$"
)
MEDIA_LAYER_RE = re.compile(
    r"suppressing unsupported media integration image layer:\s+"
    r"name=(?P<name>.*?)\s+id=(?P<layer>\d+)\s+image=(?P<image>.*)$"
)
BINDING_RE = re.compile(r"QuickJS binding:\s+id=(?P<layer>-?\d+)\s+(?P<property>\w+)=")


@dataclass(frozen=True)
class ScriptGap:
    layer_id: str
    kind: str
    api: str
    reason: str
    message: str


def parse_log(path: Path) -> tuple[list[ScriptGap], int, Counter[str], int]:
    raw_script_gaps: list[ScriptGap] = []
    media_layer_gaps: list[ScriptGap] = []
    media_layer_ids: set[str] = set()
    binding_layers: set[str] = set()
    binding_properties: Counter[str] = Counter()
    media_layer_count = 0

    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if match := SCRIPT_GAP_RE.search(line):
            raw_script_gaps.append(
                ScriptGap(
                    layer_id=match.group("layer"),
                    kind=match.group("class"),
                    api=match.group("api"),
                    reason=match.group("reason"),
                    message=match.group("message"),
                )
            )
            continue
        if match := MEDIA_LAYER_RE.search(line):
            media_layer_count += 1
            media_layer_ids.add(match.group("layer"))
            media_layer_gaps.append(
                ScriptGap(
                    layer_id=match.group("layer"),
                    kind="media-runtime-only",
                    api="media-integration-layer",
                    reason="unsupported-media-integration-layer",
                    message=match.group("name"),
                )
            )
            continue
        if match := BINDING_RE.search(line):
            binding_layers.add(match.group("layer"))
            binding_properties[match.group("property")] += 1

    gaps: list[ScriptGap] = []
    for gap in raw_script_gaps:
        if gap.layer_id in media_layer_ids and gap.kind != "media-runtime-only":
            gaps.append(
                ScriptGap(
                    layer_id=gap.layer_id,
                    kind="media-runtime-only",
                    api=gap.api,
                    reason="media-integration-layer-script-gap",
                    message=gap.message,
                )
            )
        else:
            gaps.append(gap)
    gaps.extend(media_layer_gaps)

    return gaps, len(binding_layers), binding_properties, media_layer_count


def print_summary(path: Path) -> int:
    gaps, binding_count, binding_properties, media_layer_count = parse_log(path)
    print(f"scene-script-bindings={binding_count}")
    if binding_properties:
        print("scene-script-binding-properties:")
        for property_name, count in sorted(binding_properties.items()):
            print(f"  - {property_name}: {count}")
    print(f"unsupported-media-integration-layers={media_layer_count}")
    print(f"scene-script-gaps-total={len(gaps)}")

    if not gaps:
        return 0

    kind_counts = Counter(gap.kind for gap in gaps)
    print("scene-script-gap-counts:")
    for kind, count in sorted(kind_counts.items()):
        print(f"  - {kind}: {count}")

    api_counts = Counter(gap.api for gap in gaps)
    print("scene-script-gap-apis:")
    for api, count in sorted(api_counts.items()):
        print(f"  - {api}: {count}")

    layer_counts = Counter((gap.layer_id, gap.kind, gap.api) for gap in gaps)
    print("scene-script-gap-layers:")
    for (layer_id, kind, api), count in sorted(layer_counts.items()):
        print(f"  - {layer_id} class={kind} api={api} count={count}")

    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Summarize SceneScript runtime gaps from a validator log.")
    parser.add_argument("log", type=Path)
    args = parser.parse_args(argv)

    if not args.log.exists():
        print(f"log not found: {args.log}", file=sys.stderr)
        return 2
    return print_summary(args.log)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
