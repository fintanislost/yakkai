#!/usr/bin/env python3
"""Scene-specific visual sentinels for renderer validation."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import math
from pathlib import Path
import re
import subprocess
import sys


@dataclass(frozen=True)
class RegionConfig:
    name: str
    x: int
    y: int
    width: int
    height: int

    @property
    def crop_arg(self) -> str:
        return f"{self.width}x{self.height}+{self.x}+{self.y}"


@dataclass(frozen=True)
class RegionStats:
    name: str
    mean_rgb: tuple[float, float, float]
    stddev_rgb: tuple[float, float, float]
    unique_colors: int


@dataclass(frozen=True)
class ColorPresenceConfig:
    name: str
    x: int
    y: int
    width: int
    height: int
    min_rgb: tuple[float, float, float]
    max_rgb: tuple[float, float, float]
    max_blue_red_delta: float
    min_fraction: float

    @property
    def crop_arg(self) -> str:
        return f"{self.width}x{self.height}+{self.x}+{self.y}"


@dataclass(frozen=True)
class ColorPresenceStats:
    name: str
    matching_fraction: float


@dataclass(frozen=True)
class RegionDelta:
    name: str
    mean_rgb_distance: float
    max_stddev_delta: float
    unique_delta: int


@dataclass(frozen=True)
class VisualSentinelConfig:
    scene_id: str
    clear_rgb: tuple[float, float, float]
    regions: tuple[RegionConfig, ...]
    min_leak_regions: int
    required_color_presence: tuple[ColorPresenceConfig, ...] = ()
    max_color_distance: float = 18.0
    max_channel_stddev: float = 12.0
    max_unique_colors: int = 250


@dataclass(frozen=True)
class VisualSentinelResult:
    passed: bool
    detail: str


SCENE_SENTINELS: dict[str, VisualSentinelConfig] = {
    "3476236738": VisualSentinelConfig(
        scene_id="3476236738",
        clear_rgb=(126.0, 136.0, 166.0),
        min_leak_regions=2,
        max_unique_colors=300,
        regions=(
            RegionConfig("mid_wall_between_chars", 715, 150, 185, 180),
            RegionConfig("right_wall_under_window", 1020, 150, 240, 270),
            RegionConfig("gray_band_right", 1200, 320, 260, 200),
        ),
        required_color_presence=(
            ColorPresenceConfig(
                name="left_character_extended_hand",
                x=725,
                y=535,
                width=160,
                height=55,
                min_rgb=(89.25, 96.9, 122.4),
                max_rgb=(183.6, 183.6, 216.75),
                max_blue_red_delta=71.4,
                min_fraction=0.20,
            ),
        ),
    ),
}


_CLEAR_COLOR_RE = re.compile(r"tint-adjusted clear color:\s*\(([^)]+)\)")


def color_distance(left: tuple[float, float, float], right: tuple[float, float, float]) -> float:
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(left, right)))


def compare_region_stats(
    baseline_stats: dict[str, RegionStats],
    probe_stats: dict[str, RegionStats],
) -> dict[str, RegionDelta]:
    deltas: dict[str, RegionDelta] = {}
    for name in sorted(set(baseline_stats) & set(probe_stats)):
        baseline = baseline_stats[name]
        probe = probe_stats[name]
        deltas[name] = RegionDelta(
            name=name,
            mean_rgb_distance=color_distance(baseline.mean_rgb, probe.mean_rgb),
            max_stddev_delta=max(
                abs(left - right)
                for left, right in zip(baseline.stddev_rgb, probe.stddev_rgb)
            ),
            unique_delta=probe.unique_colors - baseline.unique_colors,
        )
    return deltas


def format_region_delta_summary(deltas: dict[str, RegionDelta]) -> str:
    if not deltas:
        return "no comparable sentinel region deltas"
    return "; ".join(
        f"{name} meanRgbDistance={delta.mean_rgb_distance:.3f} "
        f"maxStddevDelta={delta.max_stddev_delta:.3f} "
        f"uniqueDelta={delta.unique_delta}"
        for name, delta in sorted(deltas.items())
    )


def is_clear_color_leak(
    stats: RegionStats,
    clear_rgb: tuple[float, float, float],
    *,
    max_color_distance: float = 18.0,
    max_channel_stddev: float = 12.0,
    max_unique_colors: int = 250,
) -> bool:
    return (
        color_distance(stats.mean_rgb, clear_rgb) <= max_color_distance
        and max(stats.stddev_rgb) <= max_channel_stddev
        and stats.unique_colors <= max_unique_colors
    )


def parse_clear_color_from_log(log_path: Path) -> tuple[float, float, float] | None:
    if not log_path.exists():
        return None
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = _CLEAR_COLOR_RE.search(line)
        if not match:
            continue
        values = [float(part.strip()) for part in match.group(1).split(",")]
        if len(values) != 3:
            return None
        if all(0.0 <= value <= 1.0 for value in values):
            return tuple(value * 255.0 for value in values)
        return tuple(values)
    return None


def _run_imagemagick(args: list[str]) -> str:
    completed = subprocess.run(
        args,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return completed.stdout.strip()


def collect_region_stats(image: Path, region: RegionConfig) -> RegionStats:
    stats_text = _run_imagemagick(
        [
            "convert",
            str(image),
            "-crop",
            region.crop_arg,
            "+repage",
            "-format",
            "%[fx:mean.r*255] %[fx:mean.g*255] %[fx:mean.b*255] "
            "%[fx:standard_deviation.r*255] %[fx:standard_deviation.g*255] "
            "%[fx:standard_deviation.b*255]",
            "info:",
        ]
    )
    values = [float(value) for value in stats_text.split()]
    if len(values) != 6:
        raise RuntimeError(f"unexpected ImageMagick stats output for {region.name}: {stats_text}")

    unique_text = _run_imagemagick(
        [
            "convert",
            str(image),
            "-crop",
            region.crop_arg,
            "+repage",
            "-resize",
            "64x64!",
            "-unique-colors",
            "-format",
            "%k",
            "info:",
        ]
    )
    return RegionStats(
        name=region.name,
        mean_rgb=tuple(values[0:3]),
        stddev_rgb=tuple(values[3:6]),
        unique_colors=int(unique_text),
    )


def collect_scene_stats(scene_id: str, image: Path) -> dict[str, RegionStats]:
    config = SCENE_SENTINELS.get(scene_id)
    if config is None:
        return {}
    return {region.name: collect_region_stats(image, region) for region in config.regions}


def collect_color_presence_stats(
    image: Path,
    region: ColorPresenceConfig,
) -> ColorPresenceStats:
    min_r, min_g, min_b = (value / 255.0 for value in region.min_rgb)
    max_r, max_g, max_b = (value / 255.0 for value in region.max_rgb)
    max_blue_red_delta = region.max_blue_red_delta / 255.0
    expression = (
        f"(u.r>{min_r:.8f} && u.r<{max_r:.8f} && "
        f"u.g>{min_g:.8f} && u.g<{max_g:.8f} && "
        f"u.b>{min_b:.8f} && u.b<{max_b:.8f} && "
        f"abs(u.b-u.r)<{max_blue_red_delta:.8f})?1:0"
    )
    fraction_text = _run_imagemagick(
        [
            "magick",
            str(image),
            "-crop",
            region.crop_arg,
            "+repage",
            "-fx",
            expression,
            "-format",
            "%[fx:mean]",
            "info:",
        ]
    )
    return ColorPresenceStats(
        name=region.name,
        matching_fraction=float(fraction_text),
    )


def collect_scene_color_presence_stats(
    scene_id: str,
    image: Path,
) -> dict[str, ColorPresenceStats]:
    config = SCENE_SENTINELS.get(scene_id)
    if config is None:
        return {}
    return {
        region.name: collect_color_presence_stats(image, region)
        for region in config.required_color_presence
    }


def evaluate_scene_sentinel(
    scene_id: str,
    stats_by_region: dict[str, RegionStats],
    clear_rgb: tuple[float, float, float] | None = None,
    color_presence_by_region: dict[str, ColorPresenceStats] | None = None,
) -> VisualSentinelResult:
    config = SCENE_SENTINELS.get(scene_id)
    if config is None:
        return VisualSentinelResult(True, f"no visual sentinel configured for scene {scene_id}")

    effective_clear = clear_rgb or config.clear_rgb
    missing = [region.name for region in config.regions if region.name not in stats_by_region]
    if missing:
        return VisualSentinelResult(
            False,
            "missing visual sentinel region stats: " + ",".join(missing),
        )

    leaked: list[str] = []
    for region in config.regions:
        stats = stats_by_region[region.name]
        if is_clear_color_leak(
            stats,
            effective_clear,
            max_color_distance=config.max_color_distance,
            max_channel_stddev=config.max_channel_stddev,
            max_unique_colors=config.max_unique_colors,
        ):
            leaked.append(region.name)

    if len(leaked) >= config.min_leak_regions:
        return VisualSentinelResult(
            False,
            f"{len(leaked)} background regions look like clear-color leakage: "
            + ",".join(leaked),
        )

    color_presence_by_region = color_presence_by_region or {}
    missing_presence = [
        region.name
        for region in config.required_color_presence
        if region.name not in color_presence_by_region
    ]
    if missing_presence:
        return VisualSentinelResult(
            False,
            "missing visual sentinel color-presence stats: " + ",".join(missing_presence),
        )

    weak_presence: list[str] = []
    for region in config.required_color_presence:
        stats = color_presence_by_region[region.name]
        if stats.matching_fraction < region.min_fraction:
            weak_presence.append(
                f"{region.name}={stats.matching_fraction:.3f}<min={region.min_fraction:.3f}"
            )
    if weak_presence:
        return VisualSentinelResult(
            False,
            "foreground regions lack expected color presence: " + ",".join(weak_presence),
        )

    plural = "regions" if len(leaked) != 1 else "region"
    presence_detail = ""
    if config.required_color_presence:
        presence_detail = "; color presence " + ",".join(
            f"{region.name}={color_presence_by_region[region.name].matching_fraction:.3f}"
            for region in config.required_color_presence
        )
    return VisualSentinelResult(
        True,
        f"{len(leaked)} clear-color-like {plural}; threshold={config.min_leak_regions}"
        + presence_detail,
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scene_id")
    parser.add_argument("capture", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args(argv)

    if args.scene_id not in SCENE_SENTINELS:
        print(f"no visual sentinel configured for scene {args.scene_id}")
        return 0
    if not args.capture.exists():
        print(f"capture not found: {args.capture}", file=sys.stderr)
        return 1

    config = SCENE_SENTINELS[args.scene_id]
    clear_rgb = config.clear_rgb
    if args.log is not None:
        clear_rgb = parse_clear_color_from_log(args.log) or clear_rgb

    try:
        stats = collect_scene_stats(args.scene_id, args.capture)
        color_presence_stats = collect_scene_color_presence_stats(args.scene_id, args.capture)
    except (subprocess.CalledProcessError, RuntimeError, ValueError) as exc:
        print(f"could not evaluate visual sentinel: {exc}", file=sys.stderr)
        return 1

    result = evaluate_scene_sentinel(args.scene_id, stats, clear_rgb, color_presence_stats)
    print(result.detail)
    if args.baseline is not None:
        if not args.baseline.exists():
            print(f"baseline capture not found: {args.baseline}", file=sys.stderr)
            return 1
        try:
            baseline_stats = collect_scene_stats(args.scene_id, args.baseline)
        except (subprocess.CalledProcessError, RuntimeError, ValueError) as exc:
            print(f"could not evaluate baseline visual sentinel: {exc}", file=sys.stderr)
            return 1
        print(
            "region deltas vs baseline: "
            + format_region_delta_summary(compare_region_stats(baseline_stats, stats))
        )
    return 0 if result.passed else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
