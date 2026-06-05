#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, ImageDraw

from arona_ribbon_flutter_parity import measure_flutter_sequence

FRAME_EXTENSIONS = {".png", ".jpg", ".jpeg"}
FEATURE_REGION_NAMES = ("lowerRibbonAndWall", "lowerRibbonTip", "wallGlowBehindRibbon")
PREFIX_SEQUENCE_NAMES = ("N7", "N8", "N10", "N11")
FEATURE_ARTIFACT_NAMES = (
    "features-windows.json",
    "features-yakkai-normal.json",
    "features-prefix-N7.json",
    "features-prefix-N8.json",
    "features-prefix-N10.json",
    "features-prefix-N11.json",
    "feature-summary.md",
)
CANDIDATE_ARTIFACT_NAMES = (
    "candidate-ranking.json",
    "candidate-ranking.md",
)
VISUAL_ARTIFACT_NAMES = (
    "contact-windows-vs-yakkai-normal.png",
    "contact-windows-vs-top-candidate.png",
    "kymograph-windows-vs-yakkai-normal.png",
    "kymograph-windows-vs-top-candidate.png",
)
FINAL_REPORT_ARTIFACT_NAMES = (
    *VISUAL_ARTIFACT_NAMES,
    "final-summary.md",
)
CANDIDATE_SOURCE_SEQUENCE_NAMES = (
    "yakkai-normal",
    "prefix-N7",
    "prefix-N8",
    "prefix-N10",
    "prefix-N11",
)
AMPLITUDE_SCALES = (0.5, 0.65, 0.8, 1.2, 1.5)
PHASE_FRAME_SHIFTS = tuple(shift for shift in range(-6, 7) if shift != 0)
SMOOTHING_RADII = (1, 2)
FEATURE_REPORT_FILENAMES = {
    "windows": "features-windows.json",
    "yakkai-normal": "features-yakkai-normal.json",
    "prefix-N7": "features-prefix-N7.json",
    "prefix-N8": "features-prefix-N8.json",
    "prefix-N10": "features-prefix-N10.json",
    "prefix-N11": "features-prefix-N11.json",
}
ALIGNMENT_CONFIDENCE_THRESHOLD = 0.75
CANDIDATE_ACTION_THRESHOLD = 0.70
UNDERDETERMINED_SCORE_DELTA = 0.08


@dataclass(frozen=True)
class CandidateResult:
    name: str
    classification: str
    rmse: float
    correlation: float
    confidence: float
    evidence: dict[str, Any]

    @property
    def score(self) -> float:
        scoring_fields = np.asarray([self.rmse, self.correlation, self.confidence], dtype=np.float64)
        if not bool(np.all(np.isfinite(scoring_fields))):
            return 0.0
        score = (1.0 - self.rmse) * 0.55 + self.correlation * 0.30 + self.confidence * 0.15
        return max(0.0, min(1.0, score))


def score_temporal_trace(reference: np.ndarray, candidate: np.ndarray) -> dict[str, float]:
    ref = np.asarray(reference, dtype=np.float32)
    cand = np.asarray(candidate, dtype=np.float32)
    if ref.size == 0 or cand.size == 0:
        raise ValueError("trace must not be empty")
    if not bool(np.all(np.isfinite(ref))) or not bool(np.all(np.isfinite(cand))):
        raise ValueError("trace contains non-finite values")
    if ref.shape != cand.shape:
        raise ValueError(f"trace shape mismatch: {ref.shape} != {cand.shape}")
    diff = ref - cand
    rmse = float(np.sqrt(np.mean(diff * diff)))
    scale = max(float(np.std(ref)), 1.0)
    normalized_rmse = min(1.0, rmse / (scale * 4.0))
    ref_flat = ref.reshape(-1)
    cand_flat = cand.reshape(-1)
    if float(np.std(ref_flat)) < 1e-6 or float(np.std(cand_flat)) < 1e-6:
        correlation = 0.0
    else:
        correlation = float(np.corrcoef(ref_flat, cand_flat)[0, 1])
    return {
        "rmse": normalized_rmse,
        "rawRmsePx": rmse,
        "correlation": max(-1.0, min(1.0, correlation)),
    }


def _validated_trace_matrix(trace: np.ndarray, *, label: str) -> np.ndarray:
    values = np.asarray(trace, dtype=np.float32)
    if values.ndim != 2:
        raise ValueError(f"{label} trace must be a two-dimensional matrix")
    if values.size == 0 or values.shape[0] == 0 or values.shape[1] == 0:
        raise ValueError(f"{label} trace must not be empty")
    if not bool(np.all(np.isfinite(values))):
        raise ValueError(f"{label} trace contains non-finite values")
    return values


def resample_trace_time_axis(trace: np.ndarray, target_frames: int) -> np.ndarray:
    if type(target_frames) is not int or target_frames <= 0:
        raise ValueError("target_frames must be a positive int")
    values = _validated_trace_matrix(trace, label="source")
    if values.shape[0] == target_frames:
        return values.copy()
    if values.shape[0] == 1:
        return np.repeat(values, target_frames, axis=0).astype(np.float32)

    source_x = np.linspace(0.0, values.shape[0] - 1, values.shape[0], dtype=np.float32)
    target_x = np.linspace(0.0, values.shape[0] - 1, target_frames, dtype=np.float32)
    columns = [
        np.interp(target_x, source_x, values[:, column]).astype(np.float32)
        for column in range(values.shape[1])
    ]
    return np.stack(columns, axis=1).astype(np.float32)


def align_trace_pair(reference: np.ndarray, candidate: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    ref = _validated_trace_matrix(reference, label="reference")
    cand = _validated_trace_matrix(candidate, label="candidate")
    if ref.shape[1] != cand.shape[1]:
        raise ValueError(f"trace column mismatch: {ref.shape[1]} != {cand.shape[1]}")
    return ref.copy(), resample_trace_time_axis(cand, ref.shape[0])


def apply_time_phase(trace: np.ndarray, frame_shift: int) -> np.ndarray:
    if type(frame_shift) is not int:
        raise ValueError("frame_shift must be an int")
    values = _validated_trace_matrix(trace, label="time-phase source")
    return np.roll(values, shift=frame_shift, axis=0).astype(np.float32, copy=True)


def apply_amplitude_scale(trace: np.ndarray, scale: float) -> np.ndarray:
    if isinstance(scale, bool) or not isinstance(scale, (int, float, np.integer, np.floating)):
        raise ValueError("scale must be a finite non-negative number")
    scale_value = float(scale)
    if not np.isfinite(scale_value) or scale_value < 0.0:
        raise ValueError("scale must be a finite non-negative number")
    values = _validated_trace_matrix(trace, label="amplitude source")
    mean = np.mean(values, axis=0, keepdims=True)
    return (mean + (values - mean) * scale_value).astype(np.float32)


def apply_temporal_smoothing(trace: np.ndarray, radius: int) -> np.ndarray:
    if isinstance(radius, bool) or not isinstance(radius, (int, np.integer)):
        raise ValueError("radius must be an int >= 0")
    radius_value = int(radius)
    if radius_value < 0:
        raise ValueError("radius must be an int >= 0")
    values = _validated_trace_matrix(trace, label="smoothing source")
    if radius_value <= 0:
        return values.copy()
    padded = np.pad(values, ((radius_value, radius_value), (0, 0)), mode="edge")
    rows = []
    for index in range(values.shape[0]):
        rows.append(np.mean(padded[index:index + radius_value * 2 + 1], axis=0))
    return np.asarray(rows, dtype=np.float32)


def rank_candidates(candidates: list[CandidateResult]) -> list[CandidateResult]:
    return sorted(candidates, key=lambda candidate: candidate.score, reverse=True)


def _finite_float(value: Any) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    if not np.isfinite(number):
        return None
    return number


def _candidate_score_for_classification(value: Any) -> float:
    score = _finite_float(value)
    if score is None:
        return 0.0
    return score


def classify_fit_report(report: dict[str, Any]) -> str:
    alignment_confidence = _finite_float(report.get("alignmentConfidence"))
    if alignment_confidence is None or alignment_confidence < ALIGNMENT_CONFIDENCE_THRESHOLD:
        return "reference-alignment-insufficient"
    ranked = report.get("rankedCandidates", [])
    if not ranked:
        return "no-actionable-runtime-gap-found"
    top = ranked[0]
    top_score = _candidate_score_for_classification(top.get("score"))
    if top_score < CANDIDATE_ACTION_THRESHOLD:
        return "no-actionable-runtime-gap-found"
    if len(ranked) > 1:
        second_score = _candidate_score_for_classification(ranked[1].get("score"))
        if abs(top_score - second_score) < UNDERDETERMINED_SCORE_DELTA:
            return "black-box-underdetermined-need-graphics-capture"
    return str(top["classification"])


def trace_matrix_from_feature(feature: dict[str, Any]) -> np.ndarray:
    if not isinstance(feature, dict):
        raise ValueError("feature must be a JSON object")
    edge_trace = feature.get("edgeTrace")
    if not isinstance(edge_trace, dict):
        raise ValueError("feature missing edgeTrace object")
    return _validated_trace_matrix(_dense_edge_trace(edge_trace), label="feature edgeY")


def load_feature_report(path: Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"{path.name} must be valid JSON: {exc.msg}") from exc
    if not isinstance(payload, dict):
        raise ValueError(f"{path.name} must contain a JSON object")
    if not isinstance(payload.get("features"), dict):
        raise ValueError(f"{path.name} missing features object")
    return payload


def _candidate_source_label(sequence_name: str) -> str:
    if sequence_name == "yakkai-normal":
        return "normal-yakkai"
    return sequence_name


def _phase_shift_label(frame_shift: int) -> str:
    if frame_shift < 0:
        return f"minus-{abs(frame_shift)}"
    return f"plus-{frame_shift}"


def _scale_label(scale: float) -> str:
    return str(scale).replace(".", "p")


def _trace_fit_confidence(metrics: dict[str, float]) -> float:
    rmse = _finite_float(metrics.get("rmse"))
    correlation = _finite_float(metrics.get("correlation"))
    if rmse is None or correlation is None:
        return 0.0
    confidence = (1.0 - rmse) * 0.65 + max(0.0, correlation) * 0.35
    return max(0.0, min(1.0, confidence))


def _score_trace_candidate(
    *,
    reference_trace: np.ndarray,
    candidate_trace: np.ndarray,
    name: str,
    classification: str,
    evidence: dict[str, Any],
) -> CandidateResult:
    aligned_reference, aligned_candidate = align_trace_pair(reference_trace, candidate_trace)
    metrics = score_temporal_trace(aligned_reference, aligned_candidate)
    evidence = {
        **evidence,
        "rawRmsePx": round(float(metrics["rawRmsePx"]), 6),
    }
    return CandidateResult(
        name=name,
        classification=classification,
        rmse=float(metrics["rmse"]),
        correlation=float(metrics["correlation"]),
        confidence=_trace_fit_confidence(metrics),
        evidence=evidence,
    )


def _region_feature(report: dict[str, Any], region_name: str, sequence_name: str) -> dict[str, Any]:
    features = report.get("features")
    if not isinstance(features, dict):
        raise ValueError(f"{sequence_name} feature report missing features object")
    feature = features.get(region_name)
    if not isinstance(feature, dict):
        raise ValueError(f"{sequence_name} feature report missing region {region_name!r}")
    return feature


def _amplitude_classification(sequence_name: str) -> str:
    if sequence_name == "prefix-N7":
        return "waterwaves-sampler-or-coordinate-mismatch"
    return "shake-sampler-or-coordinate-mismatch"


def _smoothing_classification(sequence_name: str) -> str:
    if sequence_name.startswith("prefix-"):
        return "effect-pass-order-or-prefix-target-mismatch"
    return "time-scale-or-phase-mismatch"


def _baseline_classification(sequence_name: str) -> str:
    if sequence_name == "yakkai-normal":
        return "no-actionable-runtime-gap-found"
    return "effect-pass-order-or-prefix-target-mismatch"


def _build_boundary_candidate(baseline_candidates: dict[str, CandidateResult]) -> CandidateResult | None:
    prefix_scores = [
        (sequence_name, baseline_candidates[sequence_name].score)
        for sequence_name in ("prefix-N7", "prefix-N8", "prefix-N10", "prefix-N11")
        if sequence_name in baseline_candidates
    ]
    if len(prefix_scores) < 2:
        return None

    transitions = []
    for (before_sequence, before_score), (after_sequence, after_score) in zip(prefix_scores, prefix_scores[1:]):
        transitions.append(
            {
                "beforeSequence": before_sequence,
                "afterSequence": after_sequence,
                "beforeScore": round(float(before_score), 6),
                "afterScore": round(float(after_score), 6),
                "scoreDelta": round(float(before_score - after_score), 6),
            }
        )
    biggest = max(transitions, key=lambda item: item["scoreDelta"])
    signal = max(0.0, min(1.0, float(biggest["scoreDelta"]) / 0.25))
    return CandidateResult(
        name=f"prefix-boundary-{biggest['beforeSequence']}-to-{biggest['afterSequence']}-degradation",
        classification="effect-pass-order-or-prefix-target-mismatch",
        rmse=1.0 - signal,
        correlation=signal,
        confidence=signal,
        evidence={
            "candidateFamily": "prefix-boundary-degradation",
            "biggestDegradation": biggest,
            "transitions": transitions,
        },
    )


def build_candidate_results(
    features_by_sequence: dict[str, dict[str, Any]],
    region_name: str,
) -> list[CandidateResult]:
    if "windows" not in features_by_sequence:
        raise ValueError("features missing windows sequence")
    reference_trace = trace_matrix_from_feature(
        _region_feature(features_by_sequence["windows"], region_name, "windows")
    )

    candidates: list[CandidateResult] = []
    baseline_candidates: dict[str, CandidateResult] = {}
    for sequence_name in CANDIDATE_SOURCE_SEQUENCE_NAMES:
        report = features_by_sequence.get(sequence_name)
        if report is None:
            continue
        source_trace = trace_matrix_from_feature(_region_feature(report, region_name, sequence_name))
        source_label = _candidate_source_label(sequence_name)
        baseline = _score_trace_candidate(
            reference_trace=reference_trace,
            candidate_trace=source_trace,
            name=source_label,
            classification=_baseline_classification(sequence_name),
            evidence={
                "candidateFamily": "baseline",
                "sourceSequence": sequence_name,
            },
        )
        candidates.append(baseline)
        baseline_candidates[sequence_name] = baseline

        for frame_shift in PHASE_FRAME_SHIFTS:
            candidates.append(
                _score_trace_candidate(
                    reference_trace=reference_trace,
                    candidate_trace=apply_time_phase(source_trace, frame_shift),
                    name=f"{source_label}-phase-{_phase_shift_label(frame_shift)}-frames",
                    classification="time-scale-or-phase-mismatch",
                    evidence={
                        "candidateFamily": "time-phase",
                        "sourceSequence": sequence_name,
                        "frameShift": frame_shift,
                    },
                )
            )

        for scale in AMPLITUDE_SCALES:
            candidates.append(
                _score_trace_candidate(
                    reference_trace=reference_trace,
                    candidate_trace=apply_amplitude_scale(source_trace, scale),
                    name=f"{source_label}-amplitude-scale-{_scale_label(scale)}",
                    classification=_amplitude_classification(sequence_name),
                    evidence={
                        "candidateFamily": "amplitude-scale",
                        "sourceSequence": sequence_name,
                        "scale": scale,
                    },
                )
            )

        for radius in SMOOTHING_RADII:
            candidates.append(
                _score_trace_candidate(
                    reference_trace=reference_trace,
                    candidate_trace=apply_temporal_smoothing(source_trace, radius),
                    name=f"{source_label}-temporal-smoothing-radius-{radius}",
                    classification=_smoothing_classification(sequence_name),
                    evidence={
                        "candidateFamily": "temporal-smoothing",
                        "sourceSequence": sequence_name,
                        "radius": radius,
                    },
                )
            )

    boundary_candidate = _build_boundary_candidate(baseline_candidates)
    if boundary_candidate is not None:
        candidates.append(boundary_candidate)
    return candidates


def _load_feature_reports(features_dir: Path) -> dict[str, dict[str, Any]]:
    features_by_sequence: dict[str, dict[str, Any]] = {}
    for sequence_name, filename in FEATURE_REPORT_FILENAMES.items():
        path = features_dir / filename
        if not path.exists():
            if sequence_name in {"windows", "yakkai-normal"}:
                raise ValueError(f"required feature report missing: {path}")
            continue
        report = load_feature_report(path)
        features_by_sequence[sequence_name] = report
    return features_by_sequence


def _candidate_to_json(candidate: CandidateResult) -> dict[str, Any]:
    return {
        "name": candidate.name,
        "classification": candidate.classification,
        "score": round(float(candidate.score), 6),
        "rmse": round(float(candidate.rmse), 6),
        "correlation": round(float(candidate.correlation), 6),
        "confidence": round(float(candidate.confidence), 6),
        "evidence": candidate.evidence,
    }


def _alignment_confidence(features_by_sequence: dict[str, dict[str, Any]], region_name: str) -> float:
    if "windows" not in features_by_sequence or "yakkai-normal" not in features_by_sequence:
        return 0.0
    reference_trace = trace_matrix_from_feature(
        _region_feature(features_by_sequence["windows"], region_name, "windows")
    )
    normal_trace = trace_matrix_from_feature(
        _region_feature(features_by_sequence["yakkai-normal"], region_name, "yakkai-normal")
    )
    aligned_reference, aligned_normal = align_trace_pair(reference_trace, normal_trace)
    return _trace_fit_confidence(score_temporal_trace(aligned_reference, aligned_normal))


def _render_candidate_ranking_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# Candidate Ranking",
        "",
        f"- Region: `{report['regionName']}`",
        f"- Alignment confidence: {report['alignmentConfidence']:.6f}",
        f"- Classification: `{report['classification']}`",
        f"- Candidate use: `{report['candidateUse']}`",
        f"- Generated at: {report['generatedAt']}",
        "",
    ]
    if report.get("gateReason"):
        lines.extend(
            [
                f"- Gate reason: {report['gateReason']}",
                "",
            ]
        )
    if report["classification"] == "reference-alignment-insufficient":
        lines.extend(
            [
                "**Candidate rows are exploratory only; do not use them as renderer-fix targets until alignment passes.**",
                "",
            ]
        )
    lines.extend(
        [
            "## Ranked Candidates",
            "",
            "| Rank | Candidate | Classification | Usable For Renderer Fix | Score | RMSE | Correlation | Confidence | Evidence |",
            "| ---: | --- | --- | --- | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    for index, candidate in enumerate(report["rankedCandidates"][:30], start=1):
        evidence = json.dumps(candidate["evidence"], sort_keys=True)
        lines.append(
            f"| {index} | `{candidate['name']}` | `{candidate['classification']}` | "
            f"{candidate['usableForRendererFix']} | "
            f"{candidate['score']:.6f} | {candidate['rmse']:.6f} | "
            f"{candidate['correlation']:.6f} | {candidate['confidence']:.6f} | `{evidence}` |"
        )
    return "\n".join(lines) + "\n"


def _annotate_candidate_report_gate(report: dict[str, Any]) -> None:
    gate_failed = report["classification"] == "reference-alignment-insufficient"
    report["candidateUse"] = "diagnostic-only" if gate_failed else "candidate-targets"
    if gate_failed:
        report["gateReason"] = (
            f"Alignment confidence {report['alignmentConfidence']:.6f} is below threshold "
            f"{ALIGNMENT_CONFIDENCE_THRESHOLD:.2f}; ranked candidates must not drive renderer changes."
        )
    else:
        report["gateReason"] = ""
    for candidate in report["rankedCandidates"]:
        candidate["usableForRendererFix"] = not gate_failed


def write_candidate_report(
    features_dir: Path,
    output_dir: Path,
    region_name: str = "lowerRibbonTip",
) -> dict[str, Any]:
    output_dir.mkdir(parents=True, exist_ok=True)
    features_by_sequence = _load_feature_reports(features_dir)
    ranked_candidates = rank_candidates(build_candidate_results(features_by_sequence, region_name))
    report = {
        "regionName": region_name,
        "alignmentConfidence": round(float(_alignment_confidence(features_by_sequence, region_name)), 6),
        "rankedCandidates": [_candidate_to_json(candidate) for candidate in ranked_candidates],
        "generatedAt": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
    }
    report["classification"] = classify_fit_report(report)
    _annotate_candidate_report_gate(report)
    (output_dir / "candidate-ranking.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (output_dir / "candidate-ranking.md").write_text(
        _render_candidate_ranking_markdown(report),
        encoding="utf-8",
    )
    return report


def _image_resampling_filter() -> Any:
    return getattr(getattr(Image, "Resampling", Image), "BILINEAR")


def _draw_text(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str, *, fill: tuple[int, int, int]) -> None:
    draw.text(xy, text, fill=fill)


def _draw_wrapped_text(
    draw: ImageDraw.ImageDraw,
    xy: tuple[int, int],
    text: str,
    *,
    fill: tuple[int, int, int],
    max_chars: int = 34,
) -> None:
    words = text.split()
    lines: list[str] = []
    current = ""
    for word in words:
        candidate = f"{current} {word}".strip()
        if current and len(candidate) > max_chars:
            lines.append(current)
            current = word
        else:
            current = candidate
    if current:
        lines.append(current)
    x, y = xy
    for offset, line in enumerate(lines):
        _draw_text(draw, (x, y + offset * 14), line, fill=fill)


def _representative_positions() -> list[tuple[str, float]]:
    return [("start", 0.0), ("middle", 0.5), ("end", 1.0)]


def _index_for_position(frame_count: int, position: float) -> int:
    if frame_count <= 0:
        raise ValueError("frame_count must be positive")
    return int(round(position * float(frame_count - 1)))


def _crop_frame_region(frame_path: Path, region: list[int]) -> Image.Image:
    x, y, width, height = (int(value) for value in region)
    with Image.open(frame_path) as frame:
        image = frame.convert("RGB")
    left = max(0, x)
    top = max(0, y)
    right = min(image.width, x + width)
    bottom = min(image.height, y + height)
    if left >= right or top >= bottom:
        raise ValueError(f"region {region} is outside frame {frame_path}")
    return image.crop((left, top, right, bottom))


def _paste_centered(base: Image.Image, crop: Image.Image, box: tuple[int, int, int, int]) -> None:
    left, top, right, bottom = box
    x = left + max(0, (right - left - crop.width) // 2)
    y = top + max(0, (bottom - top - crop.height) // 2)
    base.paste(crop, (x, y))


def _candidate_source_sequence(candidate: dict[str, Any]) -> str:
    evidence = candidate.get("evidence")
    if not isinstance(evidence, dict):
        raise ValueError("candidate missing evidence object")
    source_sequence = evidence.get("sourceSequence")
    if not isinstance(source_sequence, str) or not source_sequence:
        raise ValueError("candidate evidence missing sourceSequence")
    return source_sequence


def resolve_candidate_source_frames(
    candidate: dict[str, Any],
    *,
    yakkai_frames: Path,
    prefix_root: Path,
) -> Path:
    source_sequence = _candidate_source_sequence(candidate)
    if source_sequence == "yakkai-normal":
        return yakkai_frames
    if source_sequence.startswith("prefix-"):
        prefix_name = source_sequence.removeprefix("prefix-")
        return prefix_root / prefix_name / "frames"
    raise ValueError(f"unsupported candidate source sequence: {source_sequence}")


def _candidate_visual_label(candidate: dict[str, Any]) -> str:
    evidence = candidate.get("evidence") if isinstance(candidate.get("evidence"), dict) else {}
    source_sequence = str(evidence.get("sourceSequence", "unknown-source"))
    family = str(evidence.get("candidateFamily", "baseline"))
    if family == "baseline":
        return f"Top candidate source: {source_sequence}"
    if family == "amplitude-scale":
        return (
            f"Top candidate source: {source_sequence} "
            f"(transform used for scoring: amplitude scale {evidence.get('scale')})"
        )
    if family == "time-phase":
        return (
            f"Top candidate source: {source_sequence} "
            f"(transform used for scoring: phase shift {evidence.get('frameShift')} frames)"
        )
    if family == "temporal-smoothing":
        return (
            f"Top candidate source: {source_sequence} "
            f"(transform used for scoring: temporal smoothing radius {evidence.get('radius')})"
        )
    return f"Top candidate source: {source_sequence} (transform used for scoring: {family})"


def create_contact_sheet_image(
    *,
    windows_frames: Path,
    comparison_frames: Path,
    region: list[int],
    comparison_label: str,
) -> Image.Image:
    windows_files = list_frame_files(windows_frames)
    comparison_files = list_frame_files(comparison_frames)
    if not windows_files:
        raise ValueError(f"no Windows frame files found: {windows_frames}")
    if not comparison_files:
        raise ValueError(f"no comparison frame files found: {comparison_frames}")

    sampled: list[tuple[str, Path, Path, Image.Image, Image.Image]] = []
    for label, position in _representative_positions():
        windows_index = _index_for_position(len(windows_files), position)
        comparison_index = _index_for_position(len(comparison_files), position)
        windows_crop = _crop_frame_region(windows_files[windows_index], region)
        comparison_crop = _crop_frame_region(comparison_files[comparison_index], region)
        sampled.append(
            (
                label,
                windows_files[windows_index],
                comparison_files[comparison_index],
                windows_crop,
                comparison_crop,
            )
        )

    cell_width = max(max(left.width, right.width) for _, _, _, left, right in sampled) + 20
    crop_height = max(max(left.height, right.height) for _, _, _, left, right in sampled)
    label_height = 42
    row_label_width = 260
    top_header_height = 72
    row_height = crop_height + label_height
    width = row_label_width + cell_width * 3 + 24
    height = top_header_height + row_height * 2 + 18
    sheet = Image.new("RGB", (width, height), (248, 248, 246))
    draw = ImageDraw.Draw(sheet)

    _draw_text(draw, (12, 12), "lowerRibbonTip ROI contact sheet", fill=(20, 24, 28))
    _draw_text(draw, (12, 31), "Windows WE compared with source frame pixels", fill=(76, 82, 90))
    for column, (slot_label, windows_path, comparison_path, windows_crop, comparison_crop) in enumerate(sampled):
        x0 = row_label_width + column * cell_width + 8
        _draw_text(draw, (x0, top_header_height - 24), slot_label, fill=(20, 24, 28))
        for row, (row_label, frame_path, crop) in enumerate(
            (
                ("Windows WE", windows_path, windows_crop),
                (comparison_label, comparison_path, comparison_crop),
            )
        ):
            y0 = top_header_height + row * row_height
            if column == 0:
                _draw_wrapped_text(draw, (12, y0 + 8), row_label, fill=(20, 24, 28))
            _paste_centered(
                sheet,
                crop,
                (x0, y0 + 4, x0 + cell_width - 12, y0 + 4 + crop_height),
            )
            _draw_text(
                draw,
                (x0, y0 + crop_height + 8),
                frame_path.name,
                fill=(76, 82, 90),
            )
    return sheet


def _trace_values_for_kymograph(feature: dict[str, Any]) -> np.ndarray:
    return trace_matrix_from_feature(feature)


def _kymograph_panel(values: np.ndarray, *, value_min: float, value_max: float, size: tuple[int, int]) -> Image.Image:
    span = max(value_max - value_min, 1.0e-6)
    normalized = np.clip((values - value_min) / span, 0.0, 1.0)
    intensity = (normalized * 255.0).astype(np.uint8)
    pixels = np.zeros((intensity.shape[0], intensity.shape[1], 3), dtype=np.uint8)
    pixels[:, :, 0] = intensity
    pixels[:, :, 1] = np.minimum(255, 48 + intensity // 2).astype(np.uint8)
    pixels[:, :, 2] = (255 - intensity // 3).astype(np.uint8)
    panel = Image.fromarray(pixels, mode="RGB")
    return panel.resize(size, _image_resampling_filter())


def create_kymograph_image(
    left_feature: dict[str, Any],
    right_feature: dict[str, Any],
    *,
    left_label: str,
    right_label: str,
) -> Image.Image:
    left_values = _trace_values_for_kymograph(left_feature)
    right_values = _trace_values_for_kymograph(right_feature)
    all_values = np.concatenate((left_values.reshape(-1), right_values.reshape(-1)))
    value_min = float(np.percentile(all_values, 2.0))
    value_max = float(np.percentile(all_values, 98.0))
    if value_max <= value_min:
        value_min = float(np.min(all_values))
        value_max = float(np.max(all_values))
    panel_size = (384, 220)
    left_panel = _kymograph_panel(left_values, value_min=value_min, value_max=value_max, size=panel_size)
    right_panel = _kymograph_panel(right_values, value_min=value_min, value_max=value_max, size=panel_size)

    padding = 18
    label_height = 60
    gutter = 18
    width = padding * 2 + panel_size[0] * 2 + gutter
    height = padding * 2 + label_height + panel_size[1] + 24
    image = Image.new("RGB", (width, height), (248, 248, 246))
    draw = ImageDraw.Draw(image)
    _draw_wrapped_text(draw, (padding, padding), left_label, fill=(20, 24, 28), max_chars=42)
    _draw_wrapped_text(
        draw,
        (padding + panel_size[0] + gutter, padding),
        right_label,
        fill=(20, 24, 28),
        max_chars=42,
    )
    image.paste(left_panel, (padding, padding + label_height))
    image.paste(right_panel, (padding + panel_size[0] + gutter, padding + label_height))
    _draw_text(
        draw,
        (padding, padding + label_height + panel_size[1] + 6),
        f"edgeY normalized to p2-p98 range {value_min:.2f}..{value_max:.2f}",
        fill=(76, 82, 90),
    )
    return image


def _write_png(image: Image.Image, path: Path) -> bool:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="PNG")
    return path.exists() and path.stat().st_size > 0


def _feature_for_sequence(
    features_by_sequence: dict[str, dict[str, Any]],
    sequence_name: str,
    region_name: str,
) -> dict[str, Any]:
    return _region_feature(features_by_sequence[sequence_name], region_name, sequence_name)


def write_visual_evidence_outputs(
    *,
    windows_frames: Path,
    yakkai_frames: Path,
    prefix_root: Path,
    region_config: dict[str, Any],
    features_by_sequence: dict[str, dict[str, Any]],
    candidate_report: dict[str, Any],
    output_dir: Path,
    region_name: str = "lowerRibbonTip",
) -> dict[str, bool]:
    region = _feature_region_map(region_config)[region_name]
    artifacts = {name: False for name in VISUAL_ARTIFACT_NAMES}
    ranked_candidates = candidate_report.get("rankedCandidates", [])
    top_candidate = ranked_candidates[0] if ranked_candidates else None

    artifact_path = output_dir / "contact-windows-vs-yakkai-normal.png"
    artifacts[artifact_path.name] = _write_png(
        create_contact_sheet_image(
            windows_frames=windows_frames,
            comparison_frames=yakkai_frames,
            region=region,
            comparison_label="Yakkai normal",
        ),
        artifact_path,
    )

    artifact_path = output_dir / "kymograph-windows-vs-yakkai-normal.png"
    artifacts[artifact_path.name] = _write_png(
        create_kymograph_image(
            _feature_for_sequence(features_by_sequence, "windows", region_name),
            _feature_for_sequence(features_by_sequence, "yakkai-normal", region_name),
            left_label="Windows WE lowerRibbonTip",
            right_label="Yakkai normal lowerRibbonTip",
        ),
        artifact_path,
    )

    if isinstance(top_candidate, dict):
        source_sequence = _candidate_source_sequence(top_candidate)
        source_frames = resolve_candidate_source_frames(
            top_candidate,
            yakkai_frames=yakkai_frames,
            prefix_root=prefix_root,
        )
        artifact_path = output_dir / "contact-windows-vs-top-candidate.png"
        artifacts[artifact_path.name] = _write_png(
            create_contact_sheet_image(
                windows_frames=windows_frames,
                comparison_frames=source_frames,
                region=region,
                comparison_label=_candidate_visual_label(top_candidate),
            ),
            artifact_path,
        )

        artifact_path = output_dir / "kymograph-windows-vs-top-candidate.png"
        artifacts[artifact_path.name] = _write_png(
            create_kymograph_image(
                _feature_for_sequence(features_by_sequence, "windows", region_name),
                _feature_for_sequence(features_by_sequence, source_sequence, region_name),
                left_label="Windows WE lowerRibbonTip",
                right_label=_candidate_visual_label(top_candidate),
            ),
            artifact_path,
        )
    return artifacts


def _feature_metric(feature: dict[str, Any], key: str) -> str:
    value = _finite_float(feature.get(key))
    if value is None:
        return "-"
    if key == "travelLagFrames":
        return str(int(round(value)))
    return f"{value:.6f}"


def _classification_renderer_area(classification: str) -> str:
    areas = {
        "time-scale-or-phase-mismatch": "scene time and effect phase scheduling",
        "shake-sampler-or-coordinate-mismatch": "generic shake sampler coordinate mapping",
        "waterwaves-sampler-or-coordinate-mismatch": "generic waterwaves sampler coordinate mapping",
        "mask-decode-or-channel-mismatch": "generic mask decode channel handling",
        "effect-pass-order-or-prefix-target-mismatch": "effect chain pass order and prefix target selection",
        "composition-glow-or-blur-dominant": "final composition glow and blur handling",
    }
    return areas.get(classification, "stronger Windows evidence capture")


def _recommended_next_goal(candidate_report: dict[str, Any]) -> str:
    classification = str(candidate_report.get("classification", ""))
    candidate_use = str(candidate_report.get("candidateUse", ""))
    if classification == "reference-alignment-insufficient":
        return (
            "Capture stronger Windows evidence: collect a longer frame-accurate Windows WE "
            "lower-ribbon sequence at 1600x900 Day time, verify ROI registration against "
            "the Yakkai normal capture, and rerun this fitter before starting a code-change goal."
        )
    if classification == "black-box-underdetermined-need-graphics-capture":
        return (
            "Capture stronger Windows evidence with non-invasive graphics/debug output for the "
            "lower-ribbon effect boundary, then rerun the fitter before starting a code-change goal."
        )
    if candidate_use != "candidate-targets":
        return (
            "Improve Windows/Yakkai alignment and rerun the diagnostic before starting a code-change goal."
        )
    area = _classification_renderer_area(classification)
    return f"Open exactly one renderer investigation area: {area}."


def _sequence_metric_row(features_by_sequence: dict[str, dict[str, Any]], sequence: str, region_name: str) -> str:
    if sequence not in features_by_sequence:
        return f"| `{sequence}` | - | - | - | - | - | - | missing feature report |"
    feature = _feature_for_sequence(features_by_sequence, sequence, region_name)
    return (
        f"| `{sequence}` | "
        f"{_feature_metric(feature, 'rigidMotionAmplitudePx')} | "
        f"{_feature_metric(feature, 'flutterResidualRmsPx')} | "
        f"{_feature_metric(feature, 'adjacentDeltaP95Px')} | "
        f"{_feature_metric(feature, 'jerkP95Px')} | "
        f"{_feature_metric(feature, 'travelLagFrames')} | "
        f"{_feature_metric(feature, 'travelCorrelation')} | "
        f"{feature.get('motionClass', '-')} |"
    )


def _boundary_summary(candidate_report: dict[str, Any]) -> list[str]:
    for candidate in candidate_report.get("rankedCandidates", []):
        evidence = candidate.get("evidence")
        if not isinstance(evidence, dict):
            continue
        if evidence.get("candidateFamily") != "prefix-boundary-degradation":
            continue
        biggest = evidence.get("biggestDegradation")
        if not isinstance(biggest, dict):
            return [f"- Boundary candidate: `{candidate.get('name', '-')}`"]
        return [
            f"- Boundary candidate: `{candidate.get('name', '-')}`",
            (
                f"- Biggest degradation: `{biggest.get('beforeSequence', '-')}` to "
                f"`{biggest.get('afterSequence', '-')}` "
                f"(delta {biggest.get('scoreDelta', '-')})"
            ),
        ]
    return ["- No prefix boundary degradation candidate was available in the ranking."]


def _input_summary_line(inventory: dict[str, Any], key: str, label: str) -> str:
    entry = inventory.get("inputs", {}).get(key, {})
    path = entry.get("path", "-")
    ok = entry.get("ok", "-")
    frame_count = entry.get("frameCount")
    if frame_count is None:
        return f"- {label}: `{path}` (ok: {ok})"
    return f"- {label}: `{path}` ({frame_count} frames, ok: {ok})"


def render_final_summary_markdown(
    *,
    inventory: dict[str, Any],
    features_by_sequence: dict[str, dict[str, Any]],
    candidate_report: dict[str, Any],
    visual_artifacts: dict[str, bool],
    region_name: str = "lowerRibbonTip",
) -> str:
    classification = str(candidate_report.get("classification", "unknown"))
    ranked_candidates = candidate_report.get("rankedCandidates", [])
    top_candidate = ranked_candidates[0] if ranked_candidates else {}
    region_inventory = inventory.get("inputs", {}).get("regions", {})
    lines = [
        "# Arona WE Behavior Fitter Final Summary",
        "",
        "## Inputs",
        "",
        _input_summary_line(inventory, "windowsFrames", "Windows WE frames"),
        _input_summary_line(inventory, "yakkaiFrames", "Yakkai normal frames"),
        _input_summary_line(inventory, "prefixRoot", "Yakkai prefix root"),
        f"- Regions: `{region_inventory.get('path', '-')}` ({', '.join(region_inventory.get('regionNames', []))})",
        "",
        "## Alignment",
        "",
        f"- Region scored: `{candidate_report.get('regionName', region_name)}`",
        f"- Alignment confidence: {float(candidate_report.get('alignmentConfidence', 0.0)):.6f}",
        f"- Candidate use: `{candidate_report.get('candidateUse', '-')}`",
    ]
    if candidate_report.get("gateReason"):
        lines.append(f"- Gate reason: {candidate_report['gateReason']}")
    lines.extend(
        [
            "",
            "## Windows vs Yakkai Metrics",
            "",
            "| Sequence | Rigid amp px | Flutter RMS px | Adjacent p95 px | Jerk p95 px | Travel lag | Travel corr | Motion class |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
            _sequence_metric_row(features_by_sequence, "windows", region_name),
            _sequence_metric_row(features_by_sequence, "yakkai-normal", region_name),
            "",
            "## Prefix Boundary",
            "",
            *_boundary_summary(candidate_report),
            "",
            "## Ranked Hypotheses",
            "",
            "| Rank | Candidate | Classification | Score | Usable for fix | Evidence |",
            "| ---: | --- | --- | ---: | --- | --- |",
        ]
    )
    for index, candidate in enumerate(ranked_candidates[:8], start=1):
        evidence = json.dumps(candidate.get("evidence", {}), sort_keys=True)
        lines.append(
            f"| {index} | `{candidate.get('name', '-')}` | "
            f"`{candidate.get('classification', '-')}` | "
            f"{float(candidate.get('score', 0.0)):.6f} | "
            f"{candidate.get('usableForRendererFix', '-')} | `{evidence}` |"
        )
    if not ranked_candidates:
        lines.append("| - | - | - | - | - | - |")

    lines.extend(
        [
            "",
            "## Classification",
            "",
            f"- Final classification: `{classification}`",
        ]
    )
    if top_candidate:
        lines.append(
            f"- Top candidate: `{top_candidate.get('name', '-')}` "
            f"from `{top_candidate.get('evidence', {}).get('sourceSequence', '-')}`"
        )
    if candidate_report.get("candidateUse") == "diagnostic-only":
        lines.append("- Ranked candidates remain diagnostic-only while the alignment gate fails.")

    lines.extend(
        [
            "",
            "## Recommended Next Goal",
            "",
            _recommended_next_goal(candidate_report),
            "",
            "## Human Visual Gate",
            "",
            "| Artifact | Written |",
            "| --- | --- |",
        ]
    )
    for artifact_name in VISUAL_ARTIFACT_NAMES:
        lines.append(f"| `{artifact_name}` | {bool(visual_artifacts.get(artifact_name))} |")
    lines.extend(
        [
            "",
            "- Confirm the contact-sheet ROI covers the lower white ribbon tip and nearby wall glow.",
            "- Compare start, middle, and end crops for camera alignment and obvious frame registration drift.",
            "- Inspect kymographs for lower-ribbon edge travel, not just one representative frame.",
            (
                "- For transformed candidates, contact sheets and top-candidate kymographs show "
                "source data only. Trace transforms are scoring diagnostics and are not "
                "synthesized into visual artifacts."
            ),
        ]
    )
    return "\n".join(lines) + "\n"


def write_final_summary(
    *,
    inventory: dict[str, Any],
    features_by_sequence: dict[str, dict[str, Any]],
    candidate_report: dict[str, Any],
    visual_artifacts: dict[str, bool],
    output_dir: Path,
) -> Path:
    output_path = output_dir / "final-summary.md"
    output_path.write_text(
        render_final_summary_markdown(
            inventory=inventory,
            features_by_sequence=features_by_sequence,
            candidate_report=candidate_report,
            visual_artifacts=visual_artifacts,
        ),
        encoding="utf-8",
    )
    return output_path


def list_frame_files(directory: Path) -> list[Path]:
    if not directory.is_dir():
        return []
    return sorted(
        (
            path
            for path in directory.iterdir()
            if path.is_file() and path.suffix.lower() in FRAME_EXTENSIONS
        ),
        key=lambda path: path.name.lower(),
    )


def inventory_frame_directory(directory: Path, *, min_frames: int = 24) -> dict[str, Any]:
    frames = list_frame_files(directory)
    inventory: dict[str, Any] = {
        "path": str(directory),
        "exists": directory.exists(),
        "isDirectory": directory.is_dir(),
        "frameExtensions": sorted(FRAME_EXTENSIONS),
        "frameCount": len(frames),
        "minFrames": min_frames,
        "frames": [frame.name for frame in frames],
        "ok": False,
    }
    if not directory.exists():
        inventory["reason"] = "missing-directory"
    elif not directory.is_dir():
        inventory["reason"] = "not-a-directory"
    elif len(frames) < min_frames:
        inventory["reason"] = "not-enough-frames"
    else:
        inventory["ok"] = True
    return inventory


def _require_int(value: Any, label: str, *, minimum: int, minimum_label: str) -> int:
    if type(value) is not int:
        raise ValueError(f"{label} must be an int")
    if value < minimum:
        raise ValueError(f"{label} must be {minimum_label}")
    return value


def load_region_config(path: Path) -> dict[str, Any]:
    try:
        config = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"regions must be valid JSON: {exc.msg}") from exc

    if not isinstance(config, dict):
        raise ValueError("regions config must be a JSON object")

    base_size = config.get("baseSize")
    if not isinstance(base_size, list) or len(base_size) != 2:
        raise ValueError("baseSize must be a two-item list of positive ints")
    for index, value in enumerate(base_size):
        _require_int(value, f"baseSize[{index}]", minimum=1, minimum_label="> 0")

    regions = config.get("regions")
    if not isinstance(regions, dict) or not regions:
        raise ValueError("regions must be a non-empty object")
    for name, region in regions.items():
        if not isinstance(region, list) or len(region) != 4:
            raise ValueError(f"region {name!r} must be a four-item list of ints")
        x, y, width, height = region
        _require_int(x, f"region {name!r} x", minimum=0, minimum_label=">= 0")
        _require_int(y, f"region {name!r} y", minimum=0, minimum_label=">= 0")
        _require_int(width, f"region {name!r} width", minimum=1, minimum_label="> 0")
        _require_int(height, f"region {name!r} height", minimum=1, minimum_label="> 0")

    return config


def _dense_edge_trace(edge_trace: dict[str, Any]) -> np.ndarray:
    edge_y = edge_trace.get("edgeY")
    if not isinstance(edge_y, list):
        raise ValueError("edge trace missing edgeY rows")
    values = np.asarray(
        [[np.nan if value is None else float(value) for value in row] for row in edge_y],
        dtype=np.float32,
    )
    if values.ndim != 2:
        raise ValueError("edge trace must be a two-dimensional matrix")
    if values.size == 0:
        raise ValueError("edge trace must not be empty")
    valid = ~np.isnan(values)
    if not bool(np.any(valid)):
        return np.zeros(values.shape, dtype=np.float32)
    global_mean = float(np.mean(values[valid]))
    column_counts = np.count_nonzero(valid, axis=0)
    column_sums = np.nansum(values, axis=0)
    column_means = np.where(column_counts > 0, column_sums / np.maximum(column_counts, 1), global_mean)
    missing = np.where(np.isnan(values))
    values[missing] = np.take(column_means, missing[1])
    return values


def _best_lag_and_correlation(left: np.ndarray, right: np.ndarray, max_lag: int = 8) -> tuple[int, float]:
    left = left.astype(np.float32) - float(np.mean(left))
    right = right.astype(np.float32) - float(np.mean(right))
    best_lag = 0
    best_corr = 0.0
    for lag in range(-max_lag, max_lag + 1):
        if lag < 0:
            left_values = left[-lag:]
            right_values = right[: left.size + lag]
        elif lag > 0:
            left_values = left[: left.size - lag]
            right_values = right[lag:]
        else:
            left_values = left
            right_values = right
        if left_values.size < 4:
            continue
        denom = float(np.linalg.norm(left_values) * np.linalg.norm(right_values))
        if denom <= 1.0e-6:
            continue
        corr = float(np.dot(left_values, right_values) / denom)
        if corr > best_corr:
            best_corr = corr
            best_lag = lag
    return best_lag, best_corr


def _edge_trace_metrics(edge_trace: dict[str, Any]) -> dict[str, Any]:
    values = _dense_edge_trace(edge_trace)
    rigid = np.median(values, axis=1)
    residual = values - rigid[:, None]
    temporal_residual = residual - np.mean(residual, axis=0, keepdims=True)
    flutter_rms = float(np.sqrt(np.mean(temporal_residual * temporal_residual)))
    rigid_amp = float(np.percentile(rigid, 95.0) - np.percentile(rigid, 5.0))

    adjacent_delta = np.abs(np.diff(values, axis=1))
    adjacent_p95 = float(np.percentile(adjacent_delta, 95.0)) if adjacent_delta.size else 0.0
    jerk = np.abs(np.diff(values, n=2, axis=0))
    jerk_p95 = float(np.percentile(jerk, 95.0)) if jerk.size else 0.0

    left_column = temporal_residual[:, max(0, temporal_residual.shape[1] // 4)]
    right_column = temporal_residual[:, min(temporal_residual.shape[1] - 1, temporal_residual.shape[1] * 3 // 4)]
    travel_lag, travel_corr = _best_lag_and_correlation(left_column, right_column)
    confidence = str(edge_trace.get("confidence", "high"))
    motion_class = "low-confidence" if confidence != "high" else "flutter-wave" if flutter_rms >= 1.5 else "rigid-motion"
    return {
        "rigidMotionAmplitudePx": round(rigid_amp, 6),
        "flutterResidualRmsPx": round(flutter_rms, 6),
        "adjacentDeltaP95Px": round(adjacent_p95, 6),
        "jerkP95Px": round(jerk_p95, 6),
        "travelLagFrames": int(travel_lag),
        "travelCorrelation": round(float(travel_corr), 6),
        "motionClass": motion_class,
    }


def extract_trace_features(
    edge_trace: dict[str, Any],
    sequence_metrics: dict[str, Any] | None = None,
) -> dict[str, Any]:
    trace_metrics = _edge_trace_metrics(edge_trace)
    sequence_metrics = sequence_metrics or {}
    return {
        "edgeTrace": edge_trace,
        "rigidMotionAmplitudePx": sequence_metrics.get(
            "rigidMotionAmplitudePx",
            trace_metrics["rigidMotionAmplitudePx"],
        ),
        "flutterResidualRmsPx": sequence_metrics.get(
            "flutterResidualRmsPx",
            trace_metrics["flutterResidualRmsPx"],
        ),
        "adjacentDeltaP95Px": sequence_metrics.get(
            "adjacentDeltaP95Px",
            trace_metrics["adjacentDeltaP95Px"],
        ),
        "jerkP95Px": sequence_metrics.get("jerkP95Px", trace_metrics["jerkP95Px"]),
        "travelLagFrames": sequence_metrics.get("travelLagFrames", trace_metrics["travelLagFrames"]),
        "travelCorrelation": sequence_metrics.get("travelCorrelation", trace_metrics["travelCorrelation"]),
        "motionClass": sequence_metrics.get("motionClass", trace_metrics["motionClass"]),
    }


def extract_sequence_features(frame_dir: Path, region: list[int]) -> dict[str, Any]:
    result = measure_flutter_sequence(frame_dir, region, sample_columns=96)
    edge_trace = result.get("edgeTrace", result.get("trace"))
    if not isinstance(edge_trace, dict):
        raise ValueError(f"measure_flutter_sequence did not return an edge trace for {frame_dir}")
    return extract_trace_features(edge_trace, result)


def _validate_directory_input(label: str, path: Path) -> tuple[dict[str, Any], list[str]]:
    exists = path.exists()
    is_directory = path.is_dir()
    errors = []
    if not exists:
        errors.append(f"{label} does not exist: {path}")
    elif not is_directory:
        errors.append(f"{label} is not a directory: {path}")
    return (
        {
            "path": str(path),
            "exists": exists,
            "isDirectory": is_directory,
            "ok": exists and is_directory,
        },
        errors,
    )


def _inventory_region_config(path: Path) -> tuple[dict[str, Any], list[str]]:
    exists = path.exists()
    is_file = path.is_file()
    inventory: dict[str, Any] = {
        "path": str(path),
        "exists": exists,
        "isFile": is_file,
        "ok": False,
    }
    if not exists:
        inventory["reason"] = "missing-regions"
        return inventory, [f"regions does not exist: {path}"]
    if not is_file:
        inventory["reason"] = "not-a-file"
        return inventory, [f"regions is not a file: {path}"]

    try:
        config = load_region_config(path)
    except ValueError as exc:
        inventory["reason"] = "invalid-regions"
        inventory["detail"] = str(exc)
        return inventory, [f"regions invalid-regions: {exc}"]

    inventory["ok"] = True
    inventory["baseSize"] = config["baseSize"]
    inventory["regionCount"] = len(config["regions"])
    inventory["regionNames"] = sorted(config["regions"])
    return inventory, []


def build_reference_inventory(
    windows_frames: Path,
    yakkai_frames: Path,
    prefix_root: Path,
    regions: Path,
    *,
    min_frames: int = 24,
) -> dict[str, Any]:
    windows_inventory = inventory_frame_directory(windows_frames, min_frames=min_frames)
    yakkai_inventory = inventory_frame_directory(yakkai_frames, min_frames=min_frames)
    prefix_inventory, prefix_errors = _validate_directory_input("prefix-root", prefix_root)
    regions_inventory, regions_errors = _inventory_region_config(regions)

    errors: list[str] = []
    if not windows_inventory["exists"]:
        errors.append(f"windows-frames does not exist: {windows_frames}")
    elif not windows_inventory["isDirectory"]:
        errors.append(f"windows-frames is not a directory: {windows_frames}")
    elif windows_inventory.get("reason") == "not-enough-frames":
        errors.append(
            f"windows-frames has {windows_inventory['frameCount']} frame(s), "
            f"minimum {min_frames} required"
        )

    if not yakkai_inventory["exists"]:
        errors.append(f"yakkai-frames does not exist: {yakkai_frames}")
    elif not yakkai_inventory["isDirectory"]:
        errors.append(f"yakkai-frames is not a directory: {yakkai_frames}")
    elif yakkai_inventory.get("reason") == "not-enough-frames":
        errors.append(
            f"yakkai-frames has {yakkai_inventory['frameCount']} frame(s), "
            f"minimum {min_frames} required"
        )

    errors.extend(regions_errors)

    ok = (
        bool(windows_inventory["ok"])
        and bool(yakkai_inventory["ok"])
        and bool(regions_inventory["ok"])
    )
    return {
        "ok": ok,
        "generatedAt": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "minFrames": min_frames,
        "inputs": {
            "windowsFrames": windows_inventory,
            "yakkaiFrames": yakkai_inventory,
            "prefixRoot": prefix_inventory,
            "regions": regions_inventory,
        },
        "errors": errors,
        "optionalErrors": prefix_errors,
    }


def _render_reference_inventory_markdown(inventory: dict[str, Any]) -> str:
    inputs = inventory["inputs"]
    lines = [
        "# Reference Inventory",
        "",
        f"- Status: {'OK' if inventory['ok'] else 'FAILED'}",
        f"- Generated at: {inventory['generatedAt']}",
        f"- Minimum frames: {inventory['minFrames']}",
        "",
        "## Inputs",
        "",
    ]
    for label, key in (
        ("Windows frames", "windowsFrames"),
        ("Yakkai frames", "yakkaiFrames"),
        ("Prefix root", "prefixRoot"),
        ("Regions", "regions"),
    ):
        entry = inputs[key]
        lines.append(f"- {label}: `{entry['path']}`")
        lines.append(f"  - OK: {entry['ok']}")
        if "frameCount" in entry:
            lines.append(f"  - Frame count: {entry['frameCount']}")
        if "reason" in entry:
            lines.append(f"  - Reason: {entry['reason']}")

    if inventory["errors"]:
        lines.extend(["", "## Errors", ""])
        lines.extend(f"- {error}" for error in inventory["errors"])
    if inventory.get("optionalErrors"):
        lines.extend(["", "## Optional Input Notes", ""])
        lines.extend(f"- {error}" for error in inventory["optionalErrors"])

    return "\n".join(lines) + "\n"


def write_reference_inventory(inventory: dict[str, Any], output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "reference-inventory.json").write_text(
        json.dumps(inventory, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (output_dir / "reference-inventory.md").write_text(
        _render_reference_inventory_markdown(inventory),
        encoding="utf-8",
    )


def remove_feature_artifacts(output_dir: Path) -> None:
    for filename in FEATURE_ARTIFACT_NAMES:
        (output_dir / filename).unlink(missing_ok=True)


def remove_candidate_artifacts(output_dir: Path) -> None:
    for filename in CANDIDATE_ARTIFACT_NAMES:
        (output_dir / filename).unlink(missing_ok=True)


def remove_final_report_artifacts(output_dir: Path) -> None:
    for filename in FINAL_REPORT_ARTIFACT_NAMES:
        (output_dir / filename).unlink(missing_ok=True)


def _required_inventory_ok(inventory: dict[str, Any]) -> bool:
    inputs = inventory["inputs"]
    return bool(
        inputs["windowsFrames"]["ok"]
        and inputs["yakkaiFrames"]["ok"]
        and inputs["regions"]["ok"]
    )


def _required_inventory_errors(inventory: dict[str, Any]) -> list[str]:
    required_prefixes = ("windows-frames", "yakkai-frames", "regions")
    return [
        error
        for error in inventory["errors"]
        if any(error.startswith(prefix) for prefix in required_prefixes)
    ]


def _feature_region_map(config: dict[str, Any]) -> dict[str, list[int]]:
    regions = config["regions"]
    missing = [name for name in FEATURE_REGION_NAMES if name not in regions]
    if missing:
        raise ValueError(f"regions missing feature ROI(s): {', '.join(missing)}")
    return {name: [int(value) for value in regions[name]] for name in FEATURE_REGION_NAMES}


def _base_size_tuple(region_config: dict[str, Any]) -> tuple[int, int]:
    base_size = region_config["baseSize"]
    return int(base_size[0]), int(base_size[1])


def _first_frame_size(directory: Path) -> tuple[int, int]:
    frames = list_frame_files(directory)
    if not frames:
        raise ValueError("no frame files found")
    with Image.open(frames[0]) as image:
        return int(image.size[0]), int(image.size[1])


def _frame_size_error(directory: Path, expected_size: tuple[int, int]) -> str:
    try:
        actual_size = _first_frame_size(directory)
    except OSError as exc:
        return f"frame-size-unreadable: {exc}"
    except ValueError as exc:
        return str(exc)
    if actual_size != expected_size:
        expected = f"{expected_size[0]}x{expected_size[1]}"
        actual = f"{actual_size[0]}x{actual_size[1]}"
        return f"frame-size-mismatch: expected {expected}, got {actual}"
    return ""


def _remove_stale_artifact(output_path: Path) -> None:
    try:
        output_path.unlink(missing_ok=True)
    except FileNotFoundError:
        pass


def _write_sequence_feature_report(
    *,
    sequence_name: str,
    frame_dir: Path,
    regions: dict[str, list[int]],
    expected_size: tuple[int, int],
    output_path: Path,
    min_frames: int,
) -> dict[str, Any]:
    inventory = inventory_frame_directory(frame_dir, min_frames=min_frames)
    if not inventory["ok"]:
        _remove_stale_artifact(output_path)
        return {
            "sequence": sequence_name,
            "frameDir": str(frame_dir),
            "status": "error",
            "reason": inventory.get("reason", "invalid-frame-directory"),
            "frameCount": inventory["frameCount"],
            "artifact": None,
        }

    size_error = _frame_size_error(frame_dir, expected_size)
    if size_error:
        _remove_stale_artifact(output_path)
        return {
            "sequence": sequence_name,
            "frameDir": str(frame_dir),
            "status": "error",
            "reason": size_error,
            "frameCount": inventory["frameCount"],
            "artifact": None,
        }

    features = {
        region_name: extract_sequence_features(frame_dir, region)
        for region_name, region in regions.items()
    }
    report = {
        "sequence": sequence_name,
        "frameDir": str(frame_dir),
        "generatedAt": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "frameCount": inventory["frameCount"],
        "minFrames": min_frames,
        "regions": regions,
        "features": features,
    }
    output_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return {
        "sequence": sequence_name,
        "frameDir": str(frame_dir),
        "status": "ok",
        "reason": "",
        "frameCount": inventory["frameCount"],
        "artifact": output_path.name,
    }


def _sequence_error_row(sequence_name: str, frame_dir: Path, reason: str, *, min_frames: int) -> dict[str, Any]:
    inventory = inventory_frame_directory(frame_dir, min_frames=min_frames)
    return {
        "sequence": sequence_name,
        "frameDir": str(frame_dir),
        "status": "error",
        "reason": reason,
        "frameCount": inventory["frameCount"],
        "artifact": None,
    }


def _render_feature_summary(rows: list[dict[str, Any]]) -> str:
    lines = [
        "# Feature Summary",
        "",
        "| Sequence | Status | Frames | Artifact | Notes |",
        "| --- | --- | ---: | --- | --- |",
    ]
    for row in rows:
        artifact = row["artifact"] if row["artifact"] else "-"
        reason = row["reason"] if row["reason"] else "-"
        lines.append(
            f"| {row['sequence']} | {row['status']} | {row['frameCount']} | "
            f"{artifact} | {reason} |"
        )
    return "\n".join(lines) + "\n"


def _required_feature_errors(rows: list[dict[str, Any]]) -> list[str]:
    return [
        f"{row['sequence']}: {row['reason']}"
        for row in rows
        if row["sequence"] in {"windows", "yakkai-normal"} and row["status"] != "ok"
    ]


def write_feature_artifacts(
    *,
    windows_frames: Path,
    yakkai_frames: Path,
    prefix_root: Path,
    region_config: dict[str, Any],
    output_dir: Path,
    min_frames: int,
) -> list[dict[str, Any]]:
    output_dir.mkdir(parents=True, exist_ok=True)
    regions = _feature_region_map(region_config)
    expected_size = _base_size_tuple(region_config)
    rows = [
        _write_sequence_feature_report(
            sequence_name="windows",
            frame_dir=windows_frames,
            regions=regions,
            expected_size=expected_size,
            output_path=output_dir / "features-windows.json",
            min_frames=min_frames,
        ),
        _write_sequence_feature_report(
            sequence_name="yakkai-normal",
            frame_dir=yakkai_frames,
            regions=regions,
            expected_size=expected_size,
            output_path=output_dir / "features-yakkai-normal.json",
            min_frames=min_frames,
        ),
    ]
    for prefix in PREFIX_SEQUENCE_NAMES:
        frame_dir = prefix_root / prefix / "frames"
        sequence_name = f"prefix-{prefix}"
        output_path = output_dir / f"features-prefix-{prefix}.json"
        try:
            rows.append(
                _write_sequence_feature_report(
                    sequence_name=sequence_name,
                    frame_dir=frame_dir,
                    regions=regions,
                    expected_size=expected_size,
                    output_path=output_path,
                    min_frames=min_frames,
                )
            )
        except Exception as exc:
            _remove_stale_artifact(output_path)
            rows.append(
                _sequence_error_row(
                    sequence_name,
                    frame_dir,
                    f"feature-extraction-failed: {exc}",
                    min_frames=min_frames,
                )
            )

    (output_dir / "feature-summary.md").write_text(
        _render_feature_summary(rows),
        encoding="utf-8",
    )
    return rows


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Inventory Windows WE and Yakkai reference frame inputs."
    )
    parser.add_argument("--windows-frames", required=True, type=Path)
    parser.add_argument("--yakkai-frames", required=True, type=Path)
    parser.add_argument("--prefix-root", required=True, type=Path)
    parser.add_argument("--regions", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args(argv)

    inventory = build_reference_inventory(
        args.windows_frames,
        args.yakkai_frames,
        args.prefix_root,
        args.regions,
    )
    try:
        write_reference_inventory(inventory, args.output_dir)
    except OSError as exc:
        print(f"failed to write reference inventory: {exc}", file=sys.stderr)
        return 2

    if _required_inventory_ok(inventory):
        try:
            region_config = load_region_config(args.regions)
            feature_rows = write_feature_artifacts(
                windows_frames=args.windows_frames,
                yakkai_frames=args.yakkai_frames,
                prefix_root=args.prefix_root,
                region_config=region_config,
                output_dir=args.output_dir,
                min_frames=inventory["minFrames"],
            )
            feature_errors = _required_feature_errors(feature_rows)
            if feature_errors:
                remove_candidate_artifacts(args.output_dir)
                remove_final_report_artifacts(args.output_dir)
                print(
                    "required feature extraction failed: " + "; ".join(feature_errors),
                    file=sys.stderr,
                )
                return 1
            candidate_report = write_candidate_report(args.output_dir, args.output_dir)
            features_by_sequence = _load_feature_reports(args.output_dir)
            visual_artifacts = write_visual_evidence_outputs(
                windows_frames=args.windows_frames,
                yakkai_frames=args.yakkai_frames,
                prefix_root=args.prefix_root,
                region_config=region_config,
                features_by_sequence=features_by_sequence,
                candidate_report=candidate_report,
                output_dir=args.output_dir,
            )
            write_final_summary(
                inventory=inventory,
                features_by_sequence=features_by_sequence,
                candidate_report=candidate_report,
                visual_artifacts=visual_artifacts,
                output_dir=args.output_dir,
            )
        except (OSError, ValueError) as exc:
            try:
                remove_candidate_artifacts(args.output_dir)
                remove_final_report_artifacts(args.output_dir)
            except OSError:
                pass
            print(f"failed to write feature artifacts: {exc}", file=sys.stderr)
            return 2
    else:
        try:
            remove_feature_artifacts(args.output_dir)
            remove_candidate_artifacts(args.output_dir)
            remove_final_report_artifacts(args.output_dir)
        except OSError as exc:
            print(f"failed to remove stale feature artifacts: {exc}", file=sys.stderr)
            return 2
        message = "; ".join(_required_inventory_errors(inventory)) or "reference inventory validation failed"
        print(f"reference inventory validation failed: {message}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
