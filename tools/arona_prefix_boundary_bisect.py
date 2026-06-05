#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import json
import re
import shutil
import sys
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image


TIER_ORDER = ["base", "pulse", "pulse_waterwaves", "full_safe_motion"]
TIER_EFFECTS = {
    "base": [],
    "pulse": ["Halo Pulse", "Triangle Pulse"],
    "pulse_waterwaves": [
        "Halo Pulse",
        "Triangle Pulse",
        "Ribbon Wave Top",
        "Ribbon Wave Bot",
        "Ribbon Waves 2",
        "Hair waves",
        "Hair Waves 2",
    ],
    "full_safe_motion": [
        "Halo Pulse",
        "Triangle Pulse",
        "Ribbon Wave Top",
        "Ribbon Wave Bot",
        "Ribbon Waves 2",
        "Hair waves",
        "Hair Waves 2",
        "Halo Shake",
        "Mouth Shake",
        "Arm Shake",
        "Breathe Shake",
    ],
}
BOUNDARY_BY_TIER = {
    "base": "base-puppet-or-skinning",
    "pulse": "pulse-stage",
    "pulse_waterwaves": "waterwaves-stage",
    "full_safe_motion": "shake-stage",
}
REQUIRED_ROI_REGIONS = (
    "whiteMarginCheck",
    "lowerRibbonTip",
    "wallGlowBehindRibbon",
    "bowMotion",
)
EXPECTED_FREQUENCIES = {
    "0.159": 0.159,
    "0.318": 0.318,
    "0.398": 0.398,
}
CONTACT_INDICES = [0, 75, 150, 225, 299]


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"expected JSON object in {path}")
    return data


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, sort_keys=True)
        handle.write("\n")


def validate_roi_config(config: dict) -> dict:
    if config.get("baseSize") != [1280, 720]:
        raise ValueError("roi baseSize must be [1280, 720]")
    regions = config.get("regions")
    if not isinstance(regions, dict):
        raise ValueError("roi config must contain a regions object")

    missing = [name for name in REQUIRED_ROI_REGIONS if name not in regions]
    if missing:
        raise ValueError(f"roi config missing required regions: {', '.join(missing)}")

    for name in REQUIRED_ROI_REGIONS:
        box = regions[name]
        if not isinstance(box, list) or len(box) != 4:
            raise ValueError(f"roi region {name} must be a four-integer box")
        if any(isinstance(value, bool) or not isinstance(value, int) for value in box):
            raise ValueError(f"roi region {name} must contain integer coordinates")
        x0, y0, x1, y1 = box
        if not (0 <= x0 < x1 <= 1280 and 0 <= y0 < y1 <= 720):
            raise ValueError(f"roi region {name} must fit within 1280x720 with x0<x1 and y0<y1")
    return config


def frame_files(directory: Path) -> list[Path]:
    frames = []
    for path in Path(directory).iterdir():
        if not path.is_file():
            continue
        match = re.fullmatch(r"frame[-_](\d+)\.png", path.name)
        if match is None:
            continue
        frames.append((int(match.group(1)), path.name, path))
    return [path for _, _, path in sorted(frames)]


def load_timestamps(path: Path) -> np.ndarray:
    values = []
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            stripped = line.strip()
            if stripped:
                values.append(float(stripped))
    return np.asarray(values, dtype=np.float64)


def load_frames(directory: Path, limit: int | None = None) -> list[np.ndarray]:
    paths = frame_files(directory)
    if limit is not None:
        paths = paths[:limit]
    frames = []
    for path in paths:
        with Image.open(path) as image:
            frames.append(np.asarray(image.convert("RGBA"), dtype=np.uint8).copy())
    return frames


def _as_rgba_float(frame: np.ndarray) -> np.ndarray:
    values = np.asarray(frame)
    if values.ndim != 3 or values.shape[2] < 3:
        raise ValueError("frame must be an RGB or RGBA array")
    if values.shape[2] == 3:
        alpha = np.full(values.shape[:2] + (1,), 255, dtype=np.uint8)
        values = np.concatenate([values.astype(np.uint8, copy=False), alpha], axis=2)
    values = values[..., :4].astype(np.float32)
    if float(np.max(values)) > 1.0:
        values /= np.float32(255.0)
    return values


def _crop(frame: np.ndarray, box: list[int] | tuple[int, int, int, int]) -> np.ndarray:
    x0, y0, x1, y1 = [int(value) for value in box]
    values = _as_rgba_float(frame)
    height, width = values.shape[:2]
    if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
        raise ValueError(f"box {list(box)} does not fit within frame size {width}x{height}")
    return values[y0:y1, x0:x1, :]


def _luma(rgb: np.ndarray) -> np.ndarray:
    return (
        np.float32(0.2126) * rgb[..., 0]
        + np.float32(0.7152) * rgb[..., 1]
        + np.float32(0.0722) * rgb[..., 2]
    )


def _saturation(rgb: np.ndarray) -> np.ndarray:
    high = np.max(rgb, axis=2)
    low = np.min(rgb, axis=2)
    return np.where(high > 1.0e-6, (high - low) / high, 0.0).astype(np.float32)


def compute_white_margin_score(we_frame: np.ndarray, yakkai_frame: np.ndarray, box: list[int]) -> dict:
    we_crop = _crop(we_frame, box)
    yakkai_crop = _crop(yakkai_frame, box)
    if we_crop.shape != yakkai_crop.shape:
        raise ValueError(f"crop shape mismatch: {we_crop.shape} != {yakkai_crop.shape}")

    we_luma = _luma(we_crop[..., :3])
    yakkai_luma = _luma(yakkai_crop[..., :3])
    yakkai_saturation = _saturation(yakkai_crop[..., :3])
    luma_delta = yakkai_luma - we_luma
    positive_delta = luma_delta[luma_delta > 1.0e-6]
    white_candidate = (
        (yakkai_luma >= np.float32(0.86))
        & (yakkai_saturation <= np.float32(0.18))
        & (yakkai_crop[..., 3] >= np.float32(0.05))
    )
    artifact = white_candidate & (luma_delta >= np.float32(0.08))
    artifact_delta = luma_delta[artifact]

    return {
        "artifactPixelRatio": float(np.count_nonzero(artifact) / artifact.size),
        "positiveLumaDeltaMean": float(np.mean(artifact_delta)) if artifact_delta.size else 0.0,
        "positiveLumaDeltaP95": float(np.percentile(positive_delta, 95.0)) if positive_delta.size else 0.0,
        "whiteCandidatePixelRatio": float(np.count_nonzero(white_candidate) / white_candidate.size),
    }


def _gradient_magnitude(values: np.ndarray) -> np.ndarray:
    gx = np.zeros_like(values, dtype=np.float32)
    gy = np.zeros_like(values, dtype=np.float32)
    gx[:, 1:-1] = values[:, 2:] - values[:, :-2]
    gy[1:-1, :] = values[2:, :] - values[:-2, :]
    return np.sqrt(gx * gx + gy * gy)


def _motion_weights(crop: np.ndarray) -> np.ndarray:
    alpha = crop[..., 3]
    alpha_spread = float(np.percentile(alpha, 95.0) - np.percentile(alpha, 5.0))
    if alpha_spread > 0.05:
        threshold = max(float(np.percentile(alpha, 65.0)), 0.05)
        weights = np.where(alpha >= threshold, alpha, 0.0).astype(np.float32)
        if float(np.sum(weights)) > 1.0e-6:
            return weights

    luminance = _luma(crop[..., :3])
    gradient = _gradient_magnitude(luminance)
    visible = alpha > np.float32(0.03)
    candidates = gradient[visible] if np.any(visible) else gradient.reshape(-1)
    if candidates.size:
        threshold = max(float(np.percentile(candidates, 86.0)), 0.012)
        weights = np.where(visible & (gradient >= threshold), gradient, 0.0).astype(np.float32)
        if float(np.sum(weights)) > 1.0e-6:
            return weights

    values = luminance[visible] if np.any(visible) else luminance.reshape(-1)
    if values.size == 0:
        return np.zeros(luminance.shape, dtype=np.float32)
    threshold = max(float(np.percentile(values, 82.0)), 0.06)
    return np.where(visible & (luminance >= threshold), luminance, 0.0).astype(np.float32)


def _vertical_centroid(weights: np.ndarray) -> float | None:
    total = float(np.sum(weights))
    if total <= 1.0e-6:
        return None
    ys = np.indices(weights.shape, dtype=np.float32)[0]
    return float(np.sum(ys * weights) / total)


def _motion_signal(frames: list[np.ndarray], box: list[int]) -> tuple[np.ndarray, int]:
    values: list[float | None] = []
    for frame in frames:
        weights = _motion_weights(_crop(frame, box))
        values.append(_vertical_centroid(weights))

    valid = [value for value in values if value is not None]
    fill = float(np.mean(valid)) if valid else 0.0
    dense = np.asarray([fill if value is None else value for value in values], dtype=np.float64)
    return dense, len(valid)


def compute_motion_peaks(frames: list[np.ndarray], timestamps_ms: np.ndarray, box: list[int]) -> dict:
    timestamps = np.asarray(timestamps_ms, dtype=np.float64)
    if len(frames) != int(timestamps.size):
        raise ValueError(f"frame/timestamp count mismatch: {len(frames)} != {timestamps.size}")
    if len(frames) < 16:
        raise ValueError("at least 16 frames are required for FFT motion analysis")
    if not bool(np.all(np.isfinite(timestamps))):
        raise ValueError("timestamps contain non-finite values")
    if np.any(np.diff(timestamps) <= 0.0):
        raise ValueError("timestamps must be strictly increasing")

    signal, valid_count = _motion_signal(frames, box)
    duration_seconds = float((timestamps[-1] - timestamps[0]) / 1000.0)
    if duration_seconds <= 0.0:
        raise ValueError("timestamp duration must be positive")

    source_t = (timestamps - timestamps[0]) / 1000.0
    uniform_t = np.linspace(0.0, duration_seconds, len(frames), dtype=np.float64)
    uniform_signal = np.interp(uniform_t, source_t, signal).astype(np.float64)
    coeffs = np.polyfit(uniform_t, uniform_signal, 1)
    detrended = uniform_signal - np.polyval(coeffs, uniform_t)
    detrended -= float(np.mean(detrended))

    window = np.hanning(len(detrended))
    sample_spacing = duration_seconds / max(1, len(frames) - 1)
    frequencies = np.fft.rfftfreq(len(detrended), d=sample_spacing)
    spectrum = np.fft.rfft(detrended * window)
    window_scale = max(float(np.sum(window)), 1.0e-6)
    amplitudes = (2.0 * np.abs(spectrum) / window_scale).astype(np.float64)

    nearest = {}
    for label, expected in EXPECTED_FREQUENCIES.items():
        index = int(np.argmin(np.abs(frequencies - expected)))
        nearest[label] = {
            "targetFrequencyHz": float(expected),
            "frequencyHz": float(frequencies[index]),
            "amplitude": float(amplitudes[index]),
        }

    non_dc = np.flatnonzero(frequencies > 0.0)
    if non_dc.size:
        dominant_index = int(non_dc[int(np.argmax(amplitudes[non_dc]))])
        dominant_hz = float(frequencies[dominant_index])
        dominant_amplitude = float(amplitudes[dominant_index])
    else:
        dominant_hz = 0.0
        dominant_amplitude = 0.0

    return {
        "frameCount": len(frames),
        "validFrameCount": valid_count,
        "durationSeconds": duration_seconds,
        "signalP2P": float(np.max(uniform_signal) - np.min(uniform_signal)),
        "dominantHz": dominant_hz,
        "dominantAmplitude": dominant_amplitude,
        "nearestExpected": nearest,
    }


def mutate_scene_for_tier(scene: dict, tier: str) -> dict:
    if tier not in TIER_EFFECTS:
        raise ValueError(f"unknown tier: {tier}")
    mutated = copy.deepcopy(scene)

    general = mutated.setdefault("general", {})
    if not isinstance(general, dict):
        raise ValueError("scene general must be an object")
    _disable_parallax_fields(general)
    nested_camera = general.get("camera")
    if isinstance(nested_camera, dict):
        _disable_parallax_fields(nested_camera)

    target = None
    objects = mutated.get("objects", [])
    if isinstance(objects, list):
        for item in objects:
            if isinstance(item, dict) and item.get("id") == 405:
                target = item
                break
    if target is None:
        raise ValueError("scene object id 405 not found")

    enabled = set(TIER_EFFECTS[tier])
    effects = target.get("effects", [])
    if not isinstance(effects, list):
        raise ValueError("scene object id 405 has no effects list")
    for effect in effects:
        if not isinstance(effect, dict):
            continue
        should_be_visible = effect.get("name") in enabled
        visible = effect.get("visible")
        if isinstance(visible, dict):
            visible["value"] = bool(should_be_visible)
        else:
            effect["visible"] = bool(should_be_visible)
    return mutated


def _disable_parallax_fields(container: dict) -> None:
    container["cameraparallax"] = False
    container["cameraparallaxamount"] = 0
    container["cameraparallaxmouseinfluence"] = 0


def copy_scene_variant(source_scene_dir: Path, out_dir: Path, tier: str) -> Path:
    if tier not in TIER_EFFECTS:
        raise ValueError(f"unknown tier: {tier}")
    source = Path(source_scene_dir)
    destination = Path(out_dir) / tier
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination, dirs_exist_ok=True)
    scene_path = destination / "scene.json"
    scene = load_json(source / "scene.json")
    write_json(scene_path, mutate_scene_for_tier(scene, tier))
    return scene_path


def classify_first_bad_boundary(
    tier_scores: dict,
    threshold: float = 0.03,
    motion_by_tier: dict | None = None,
    motion_we_threshold: float = 0.10,
    motion_ratio_threshold: float = 0.35,
) -> dict:
    best_tier = None
    best_metric = None
    best_value = None
    for tier in TIER_ORDER:
        metric, value = _classification_metric(tier_scores.get(tier, {}))
        if best_value is None or value > best_value:
            best_tier = tier
            best_metric = metric
            best_value = value
        if value >= threshold:
            return {
                "classification": BOUNDARY_BY_TIER[tier],
                "firstBadTier": tier,
                "classificationMetric": metric,
                "classificationMetricValue": value,
                "threshold": float(threshold),
                "artifactPixelRatio": float(tier_scores.get(tier, {}).get("artifactPixelRatio", 0.0)),
                "reason": (
                    f"{tier} crossed threshold {threshold:.6f} via "
                    f"{metric}={value:.6f}"
                ),
            }
    motion_result = _classify_motion_boundary(
        motion_by_tier or {},
        motion_we_threshold=motion_we_threshold,
        motion_ratio_threshold=motion_ratio_threshold,
    )
    if motion_result is not None:
        motion_result["whiteMarginThreshold"] = float(threshold)
        return motion_result
    return {
        "classification": "no-regression-reproduced",
        "firstBadTier": None,
        "classificationMetric": best_metric,
        "classificationMetricValue": best_value,
        "threshold": float(threshold),
        "artifactPixelRatio": None,
        "reason": (
            "no tier crossed threshold "
            f"{threshold:.6f}; highest was {best_tier} "
            f"{best_metric}={best_value:.6f}"
            if best_metric is not None and best_value is not None
            else f"no tier crossed threshold {threshold:.6f}"
        ),
    }


def _classification_metric(score: dict) -> tuple[str, float]:
    candidates = []
    for name in ("artifactPixelRatio", "artifactPixelRatioP95", "artifactPixelRatioMax"):
        value = _finite_float(score.get(name))
        if value is not None:
            candidates.append((name, value))
    if not candidates:
        return ("artifactPixelRatio", 0.0)
    return max(candidates, key=lambda item: item[1])


def _classify_motion_boundary(
    motion_by_tier: dict,
    *,
    motion_we_threshold: float,
    motion_ratio_threshold: float,
) -> dict[str, Any] | None:
    for tier in TIER_ORDER:
        motion = motion_by_tier.get(tier, {})
        candidates = []
        for region_name in ("bowMotion", "lowerRibbonTip"):
            region = motion.get(region_name, {})
            we_expected = region.get("we", {}).get("nearestExpected", {})
            yakkai_expected = region.get("yakkai", {}).get("nearestExpected", {})
            for frequency_label in EXPECTED_FREQUENCIES:
                we_amplitude = _finite_float(we_expected.get(frequency_label, {}).get("amplitude"))
                yakkai_amplitude = _finite_float(yakkai_expected.get(frequency_label, {}).get("amplitude"))
                if we_amplitude is None or yakkai_amplitude is None:
                    continue
                if we_amplitude < motion_we_threshold:
                    continue
                amplitude_ratio = yakkai_amplitude / we_amplitude
                if amplitude_ratio <= motion_ratio_threshold:
                    candidates.append(
                        {
                            "region": region_name,
                            "frequency": frequency_label,
                            "weAmplitude": float(we_amplitude),
                            "yakkaiAmplitude": float(yakkai_amplitude),
                            "amplitudeRatio": float(amplitude_ratio),
                        }
                    )
        if candidates:
            candidate = min(candidates, key=lambda item: (item["amplitudeRatio"], -item["weAmplitude"]))
            return {
                "classification": BOUNDARY_BY_TIER[tier],
                "firstBadTier": tier,
                "classificationMetric": "motionAmplitudeRatio",
                "classificationMetricValue": candidate["amplitudeRatio"],
                "motionRegion": candidate["region"],
                "motionFrequency": candidate["frequency"],
                "weAmplitude": candidate["weAmplitude"],
                "yakkaiAmplitude": candidate["yakkaiAmplitude"],
                "amplitudeRatio": candidate["amplitudeRatio"],
                "threshold": float(motion_ratio_threshold),
                "motionWeAmplitudeThreshold": float(motion_we_threshold),
                "reason": (
                    f"{tier} crossed motion cliff threshold in {candidate['region']} "
                    f"at {candidate['frequency']} Hz: WE amplitude {candidate['weAmplitude']:.6f}, "
                    f"Yakkai amplitude {candidate['yakkaiAmplitude']:.6f}, "
                    f"ratio {candidate['amplitudeRatio']:.6f} <= {motion_ratio_threshold:.6f}"
                ),
            }
    return None


def _tier_target_dir(targets: Path, tier: str) -> Path:
    return Path(targets) / "optional_prefix" / tier


def _frames_dir(sequence_dir: Path) -> Path:
    candidate = sequence_dir / "frames"
    return candidate if candidate.is_dir() else sequence_dir


def _timestamp_path(sequence_dir: Path) -> Path:
    for name in ("timestamps_ms.txt", "timestamps.txt", "frame_timestamps_ms.txt"):
        candidate = sequence_dir / name
        if candidate.is_file():
            return candidate
    return sequence_dir / "timestamps_ms.txt"


def _read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def _finite_float(value: Any) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    if not np.isfinite(number):
        return None
    return number


def inspect_dualwaves_hypothesis(scene_dir: Path) -> dict[str, Any]:
    vert = _read_text(scene_dir / "shaders" / "effects" / "waterwaves.vert")
    frag = _read_text(scene_dir / "shaders" / "effects" / "waterwaves.frag")
    material = _read_text(scene_dir / "materials" / "effects" / "waterwaves.json")
    scene = _read_text(scene_dir / "scene.json")
    shader_text = f"{vert}\n{frag}"
    combo_default_zero = bool(
        re.search(
            r'"combo"\s*:\s*"DUALWAVES"(?:(?!"combo").)*"default"\s*:\s*0',
            shader_text,
            flags=re.DOTALL,
        )
    )
    return {
        "shaderHasDualWavesCombo": "[COMBO]" in shader_text and "DUALWAVES" in shader_text,
        "shaderComboDefaultZero": combo_default_zero,
        "shaderFragHasSpeed2": "g_Speed2" in frag or "speed2" in frag.lower(),
        "materialMentionsDualWaves": "dualwaves" in material.lower(),
        "sceneMentionsDualWaves": "dualwaves" in scene.lower(),
        "acceptedAsFact": False,
    }


def _inventory_tier(targets: Path, tier: str) -> dict[str, Any]:
    tier_dir = _tier_target_dir(targets, tier)
    frames_dir = _frames_dir(tier_dir)
    timestamps_path = _timestamp_path(tier_dir)
    frames = frame_files(frames_dir) if frames_dir.is_dir() else []
    timestamps = load_timestamps(timestamps_path) if timestamps_path.is_file() else np.asarray([], dtype=np.float64)
    enabled_effects = _read_text(tier_dir / "enabled_effects.txt").strip()
    ok = len(frames) > 0 and len(frames) == int(timestamps.size)
    return {
        "tier": tier,
        "boundary": BOUNDARY_BY_TIER[tier],
        "directory": str(tier_dir),
        "frameCount": len(frames),
        "timestampCount": int(timestamps.size),
        "enabledEffectsText": enabled_effects,
        "ok": ok,
    }


def command_validate_targets(args: argparse.Namespace) -> int:
    targets = Path(args.targets)
    output_dir = Path(args.output_dir)
    scene_dir = Path(args.scene_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    roi = validate_roi_config(load_json(targets / "roi.json"))
    tiers = [_inventory_tier(targets, tier) for tier in TIER_ORDER]
    dual_waves_hypothesis = inspect_dualwaves_hypothesis(scene_dir)
    report = {
        "roi": roi,
        "tiers": tiers,
        "dualWavesHypothesis": dual_waves_hypothesis,
        "dualwavesHypothesis": dual_waves_hypothesis,
        "allTiersOk": all(item["ok"] for item in tiers),
    }
    write_json(output_dir / "target-inventory.json", report)
    _write_text(output_dir / "target-inventory.md", _target_inventory_markdown(report))
    return 0 if report["allTiersOk"] else 1


def command_prepare_scenes(args: argparse.Namespace) -> int:
    scene_dir = Path(args.scene_dir)
    output_dir = Path(args.output_dir)
    report_dir = Path(args.report_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    report_dir.mkdir(parents=True, exist_ok=True)

    prepared = []
    for tier in TIER_ORDER:
        scene_path = copy_scene_variant(scene_dir, output_dir, tier)
        prepared.append(
            {
                "tier": tier,
                "boundary": BOUNDARY_BY_TIER[tier],
                "scenePath": str(scene_path),
                "enabledEffects": list(TIER_EFFECTS[tier]),
            }
        )
    write_json(
        report_dir / "prepared-scenes.json",
        {
            "sourceSceneDir": str(scene_dir),
            "outputDir": str(output_dir),
            "tiers": prepared,
        },
    )
    return 0


def command_compare(args: argparse.Namespace) -> int:
    targets = Path(args.targets)
    yakkai_root = Path(args.yakkai_root)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    roi = validate_roi_config(load_json(targets / "roi.json"))
    regions = roi["regions"]
    tiers = {}
    tier_scores = {}
    tier_motion = {}
    for tier in TIER_ORDER:
        we_dir = _tier_target_dir(targets, tier)
        yakkai_dir = yakkai_root / tier
        result = _compare_tier(tier, we_dir, yakkai_dir, regions, output_dir)
        tiers[tier] = result
        tier_scores[tier] = result["whiteMargin"]
        tier_motion[tier] = result["motion"]

    classification = classify_first_bad_boundary(tier_scores, motion_by_tier=tier_motion)
    report = {
        "classification": classification,
        "tiers": tiers,
        "roi": roi,
    }
    write_json(output_dir / "prefix-boundary-report.json", report)
    _write_text(output_dir / "final-summary.md", _final_summary_markdown(report))
    return 0


def _compare_tier(
    tier: str,
    we_sequence_dir: Path,
    yakkai_sequence_dir: Path,
    regions: dict,
    output_dir: Path,
) -> dict[str, Any]:
    we_frames_dir = _frames_dir(we_sequence_dir)
    yakkai_frames_dir = _frames_dir(yakkai_sequence_dir)
    we_frames = load_frames(we_frames_dir)
    yakkai_frames = load_frames(yakkai_frames_dir)
    if len(we_frames) != len(yakkai_frames):
        raise ValueError(f"{tier} WE/Yakkai frame count mismatch: {len(we_frames)} != {len(yakkai_frames)}")
    we_timestamps = load_timestamps(_timestamp_path(we_sequence_dir))
    yakkai_timestamp_path = _timestamp_path(yakkai_sequence_dir)
    yakkai_timestamps = load_timestamps(yakkai_timestamp_path) if yakkai_timestamp_path.is_file() else we_timestamps
    if int(we_timestamps.size) != len(we_frames):
        raise ValueError(f"{tier} WE timestamp count mismatch: {we_timestamps.size} != {len(we_frames)}")
    if int(yakkai_timestamps.size) != len(yakkai_frames):
        raise ValueError(f"{tier} Yakkai timestamp count mismatch: {yakkai_timestamps.size} != {len(yakkai_frames)}")

    white_scores = [
        compute_white_margin_score(we_frame, yakkai_frame, regions["whiteMarginCheck"])
        for we_frame, yakkai_frame in zip(we_frames, yakkai_frames)
    ]
    motion = {}
    for region_name in ("bowMotion", "lowerRibbonTip"):
        motion[region_name] = {
            "we": compute_motion_peaks(we_frames, we_timestamps, regions[region_name]),
            "yakkai": compute_motion_peaks(yakkai_frames, yakkai_timestamps, regions[region_name]),
        }

    contact_path = output_dir / f"contact-{tier}.png"
    _write_contact_sheet(we_frames, yakkai_frames, contact_path)
    white_margin = _summarize_white_scores(white_scores)
    return {
        "tier": tier,
        "boundary": BOUNDARY_BY_TIER[tier],
        "frameCount": len(we_frames),
        "whiteMargin": white_margin,
        "motion": motion,
        "whiteMarginSummary": white_margin,
        "motionPeaks": motion,
        "contactSheet": str(contact_path),
    }


def _summarize_white_scores(scores: list[dict[str, float]]) -> dict[str, float]:
    if not scores:
        return {
            "frameCount": 0.0,
            "artifactPixelRatio": 0.0,
            "artifactPixelRatioP95": 0.0,
            "artifactPixelRatioMax": 0.0,
            "positiveLumaDeltaMean": 0.0,
            "positiveLumaDeltaP95": 0.0,
            "whiteCandidatePixelRatio": 0.0,
        }
    fields = ("artifactPixelRatio", "positiveLumaDeltaMean", "positiveLumaDeltaP95", "whiteCandidatePixelRatio")
    values = {field: np.asarray([score[field] for score in scores], dtype=np.float64) for field in fields}
    return {
        "frameCount": float(len(scores)),
        "artifactPixelRatio": float(np.mean(values["artifactPixelRatio"])),
        "artifactPixelRatioP95": float(np.percentile(values["artifactPixelRatio"], 95.0)),
        "artifactPixelRatioMax": float(np.max(values["artifactPixelRatio"])),
        "positiveLumaDeltaMean": float(np.mean(values["positiveLumaDeltaMean"])),
        "positiveLumaDeltaP95": float(np.percentile(values["positiveLumaDeltaP95"], 95.0)),
        "whiteCandidatePixelRatio": float(np.mean(values["whiteCandidatePixelRatio"])),
    }


def _write_contact_sheet(we_frames: list[np.ndarray], yakkai_frames: list[np.ndarray], path: Path) -> None:
    selected = [index for index in CONTACT_INDICES if index < len(we_frames) and index < len(yakkai_frames)]
    if not selected:
        return
    rows = []
    for index in selected:
        we_image = Image.fromarray(we_frames[index], "RGBA")
        yakkai_image = Image.fromarray(yakkai_frames[index], "RGBA")
        width = we_image.width + yakkai_image.width
        height = max(we_image.height, yakkai_image.height)
        row = Image.new("RGBA", (width, height), (0, 0, 0, 255))
        row.paste(we_image, (0, 0))
        row.paste(yakkai_image, (we_image.width, 0))
        rows.append(row)

    sheet_width = max(row.width for row in rows)
    sheet_height = sum(row.height for row in rows)
    sheet = Image.new("RGBA", (sheet_width, sheet_height), (0, 0, 0, 255))
    y = 0
    for row in rows:
        sheet.paste(row, (0, y))
        y += row.height
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)


def _write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="ascii")


def _target_inventory_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# Arona Prefix Boundary Target Inventory",
        "",
        "| tier | frames | timestamps | ok | enabled effects |",
        "|---|---:|---:|---|---|",
    ]
    for item in report["tiers"]:
        effects = item["enabledEffectsText"].replace("\n", ", ") or "(none)"
        lines.append(
            f"| {item['tier']} | {item['frameCount']} | {item['timestampCount']} | "
            f"{'yes' if item['ok'] else 'no'} | {effects} |"
        )
    hypothesis = report.get("dualWavesHypothesis", report.get("dualwavesHypothesis", {}))
    lines.extend(
        [
            "",
            "## DUALWAVES Hypothesis",
            "",
            f"- shaderHasDualWavesCombo: {hypothesis['shaderHasDualWavesCombo']}",
            f"- shaderComboDefaultZero: {hypothesis['shaderComboDefaultZero']}",
            f"- shaderFragHasSpeed2: {hypothesis['shaderFragHasSpeed2']}",
            f"- materialMentionsDualWaves: {hypothesis['materialMentionsDualWaves']}",
            f"- sceneMentionsDualWaves: {hypothesis['sceneMentionsDualWaves']}",
            f"- acceptedAsFact: {hypothesis['acceptedAsFact']}",
            "",
        ]
    )
    return "\n".join(lines)


def _final_summary_markdown(report: dict[str, Any]) -> str:
    classification = report["classification"]
    lines = [
        "# Arona Prefix Boundary Summary",
        "",
        f"Classification: {classification['classification']}",
        f"First bad tier: {classification['firstBadTier']}",
        f"Classification metric: {classification.get('classificationMetric')}",
        f"Classification metric value: {_format_optional_float(classification.get('classificationMetricValue'))}",
        f"Threshold: {_format_optional_float(classification.get('threshold'))}",
        f"Reason: {classification.get('reason', '')}",
        "",
        "| tier | boundary | artifact ratio | p95 | max | contact sheet |",
        "|---|---|---:|---:|---:|---|",
    ]
    for tier in TIER_ORDER:
        item = report["tiers"][tier]
        summary = item.get("whiteMargin", item.get("whiteMarginSummary", {}))
        lines.append(
            f"| {tier} | {item['boundary']} | {summary['artifactPixelRatio']:.6f} | "
            f"{summary['artifactPixelRatioP95']:.6f} | {summary['artifactPixelRatioMax']:.6f} | "
            f"{item['contactSheet']} |"
        )
    lines.extend(
        [
            "",
            "## Motion",
            "",
            "| tier | region | source | dominant Hz | dominant amplitude | signal P2P | amp 0.159 | amp 0.318 | amp 0.398 |",
            "|---|---|---|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for tier in TIER_ORDER:
        item = report["tiers"][tier]
        motion = item.get("motion", item.get("motionPeaks", {}))
        for region_name in ("bowMotion", "lowerRibbonTip"):
            region = motion.get(region_name, {})
            for source in ("we", "yakkai"):
                peaks = region.get(source, {})
                lines.append(_motion_summary_row(tier, region_name, source, peaks))
    lines.append("")
    return "\n".join(lines)


def _motion_summary_row(tier: str, region_name: str, source: str, peaks: dict[str, Any]) -> str:
    nearest = peaks.get("nearestExpected", {})
    return (
        f"| {tier} | {region_name} | {source} | "
        f"{_format_optional_float(peaks.get('dominantHz'))} | "
        f"{_format_optional_float(peaks.get('dominantAmplitude'))} | "
        f"{_format_optional_float(peaks.get('signalP2P'))} | "
        f"{_format_optional_float(nearest.get('0.159', {}).get('amplitude'))} | "
        f"{_format_optional_float(nearest.get('0.318', {}).get('amplitude'))} | "
        f"{_format_optional_float(nearest.get('0.398', {}).get('amplitude'))} |"
    )


def _format_optional_float(value: Any) -> str:
    number = _finite_float(value)
    if number is None:
        return ""
    return f"{number:.6f}"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Arona WE prefix boundary bisect diagnostics")
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate = subparsers.add_parser("validate-targets", help="validate WE target captures")
    validate.add_argument("--targets", required=True)
    validate.add_argument("--scene-dir", required=True)
    validate.add_argument("--output-dir", required=True)
    validate.set_defaults(func=command_validate_targets)

    prepare = subparsers.add_parser("prepare-scenes", help="copy tier scene variants")
    prepare.add_argument("--scene-dir", required=True)
    prepare.add_argument("--output-dir", required=True)
    prepare.add_argument("--report-dir", required=True)
    prepare.set_defaults(func=command_prepare_scenes)

    compare = subparsers.add_parser("compare", help="compare WE targets against Yakkai tier captures")
    compare.add_argument("--targets", required=True)
    compare.add_argument("--yakkai-root", required=True)
    compare.add_argument("--output-dir", required=True)
    compare.set_defaults(func=command_compare)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
