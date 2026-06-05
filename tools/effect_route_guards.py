#!/usr/bin/env python3
"""Structural guards for effect-route manifests."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import sys
from typing import Any


@dataclass(frozen=True)
class EffectRouteGuardResult:
    passed: bool
    detail: str
    checked_count: int = 0
    expanded_count: int = 0


def _as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def _as_float_pair(value: Any) -> tuple[float, float] | None:
    if not isinstance(value, list) or len(value) < 2:
        return None
    try:
        return float(value[0]), float(value[1])
    except (TypeError, ValueError):
        return None


def _layer_identity(layer: dict[str, Any]) -> str:
    layer_id = layer.get("layerId", "unknown")
    layer_name = layer.get("layerName") or "unnamed"
    return f"{layer_id}:{layer_name}"


def _puppet_effect_publish_records(
    manifest: dict[str, Any],
) -> list[tuple[str, dict[str, Any]]]:
    records: list[tuple[str, dict[str, Any]]] = []
    seen: set[tuple[str, str]] = set()
    captures = manifest.get("captures")
    if not isinstance(captures, list):
        return records

    for capture in captures:
        if not isinstance(capture, dict):
            continue
        layer = _as_dict(capture.get("layer"))
        publish = _as_dict(layer.get("publish"))
        if publish.get("puppetLayer") is not True:
            continue
        mesh_kind = str(publish.get("effectInputMeshKind") or "")
        if "puppet" not in mesh_kind:
            continue
        identity = _layer_identity(layer)
        render_target = str(publish.get("effectInputRenderTarget") or "")
        key = (identity, render_target)
        if key in seen:
            continue
        seen.add(key)
        records.append((identity, publish))
    return records


def _viewport_overflow(
    viewport: tuple[float, float],
    position_min: tuple[float, float],
    position_max: tuple[float, float],
) -> tuple[bool, str]:
    meaningful_overflow_ratio = 0.03
    width, height = viewport
    min_x, min_y = position_min
    max_x, max_y = position_max
    half_width = width / 2.0
    half_height = height / 2.0
    tolerance_x = max(0.5, width * meaningful_overflow_ratio)
    tolerance_y = max(0.5, height * meaningful_overflow_ratio)

    failures: list[str] = []
    if min_x < -half_width - tolerance_x:
        failures.append(f"minX={min_x:.3f}<-{half_width:.3f}")
    if max_x > half_width + tolerance_x:
        failures.append(f"maxX={max_x:.3f}>{half_width:.3f}")
    if min_y < -half_height - tolerance_y:
        failures.append(f"minY={min_y:.3f}<-{half_height:.3f}")
    if max_y > half_height + tolerance_y:
        failures.append(f"maxY={max_y:.3f}>{half_height:.3f}")
    return bool(failures), ",".join(failures)


def evaluate_manifest(manifest: dict[str, Any]) -> EffectRouteGuardResult:
    records = _puppet_effect_publish_records(manifest)
    if not records:
        return EffectRouteGuardResult(
            passed=True,
            detail="no puppet effect viewports to check",
        )

    failures: list[str] = []
    expanded_count = 0
    checked_count = 0
    for identity, publish in records:
        viewport = _as_float_pair(publish.get("effectInputViewportSize"))
        bounds = _as_dict(publish.get("effectInputMeshBounds"))
        position_min = _as_float_pair(bounds.get("positionMin"))
        position_max = _as_float_pair(bounds.get("positionMax"))
        if viewport is None or position_min is None or position_max is None:
            failures.append(f"{identity} missing viewport or mesh bounds metadata")
            continue

        checked_count += 1
        if publish.get("effectInputViewportExpanded") is True:
            expanded_count += 1
        overflowed, overflow_detail = _viewport_overflow(viewport, position_min, position_max)
        if overflowed:
            failures.append(f"{identity} mesh bounds exceed viewport ({overflow_detail})")

    if failures:
        return EffectRouteGuardResult(
            passed=False,
            detail="; ".join(failures),
            checked_count=checked_count,
            expanded_count=expanded_count,
        )

    plural = "viewports" if checked_count != 1 else "viewport"
    detail = f"{checked_count} puppet effect {plural} checked"
    if expanded_count:
        plural_expanded = "viewports" if expanded_count != 1 else "viewport"
        detail += f"; {expanded_count} puppet effect {plural_expanded} expanded to mesh bounds"
    return EffectRouteGuardResult(
        passed=True,
        detail=detail,
        checked_count=checked_count,
        expanded_count=expanded_count,
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args(argv)

    try:
        with args.manifest.open("r", encoding="utf-8") as handle:
            manifest = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"could not read manifest: {exc}", file=sys.stderr)
        return 1
    if not isinstance(manifest, dict):
        print("manifest root is not an object", file=sys.stderr)
        return 1

    result = evaluate_manifest(manifest)
    print(result.detail)
    return 0 if result.passed else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
