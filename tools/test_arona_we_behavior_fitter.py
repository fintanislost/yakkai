import contextlib
import io
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from arona_we_behavior_fitter import (
    CandidateResult,
    _candidate_visual_label,
    _trace_fit_confidence,
    align_trace_pair,
    apply_amplitude_scale,
    apply_temporal_smoothing,
    apply_time_phase,
    build_candidate_results,
    build_reference_inventory,
    classify_fit_report,
    create_kymograph_image,
    extract_trace_features,
    inventory_frame_directory,
    list_frame_files,
    load_region_config,
    main,
    rank_candidates,
    render_final_summary_markdown,
    resample_trace_time_axis,
    resolve_candidate_source_frames,
    score_temporal_trace,
    write_candidate_report,
)


class BehaviorFitterTests(unittest.TestCase):
    def make_trace(self, *, phase=0.0, amp=8.0, frames=32):
        xs = np.linspace(0.0, math.tau, 48, dtype=np.float32)
        rows = []
        for frame in range(frames):
            t = (frame / frames) * math.tau
            rows.append(amp * np.sin(xs + t + phase))
        return np.asarray(rows, dtype=np.float32)

    def make_edge_trace(self, trace: np.ndarray):
        return {
            "confidence": "high",
            "edgeY": [[round(float(value), 6) for value in row] for row in trace],
        }

    def write_json(self, path: Path, payload):
        path.write_text(json.dumps(payload), encoding="utf-8")

    def make_frame_set(self, directory: Path, count=24):
        directory.mkdir(parents=True, exist_ok=True)
        for index in range(count):
            (directory / f"frame-{index:03d}.png").write_text("placeholder", encoding="utf-8")

    def make_png_frame_set(self, directory: Path, *, size=(16, 9), count=24):
        directory.mkdir(parents=True, exist_ok=True)
        for index in range(count):
            image = Image.new("RGBA", size, (index % 255, 64, 128, 255))
            image.save(directory / f"frame-{index:03d}.png")

    def fake_flutter_sequence(self, frame_dir, region, *, sample_columns=96):
        edge_trace = self.make_edge_trace(self.make_trace(frames=24))
        return {
            "trace": edge_trace,
            "rigidMotionAmplitudePx": 1.25,
            "flutterResidualRmsPx": 2.5,
            "travelLagFrames": 1,
            "travelCorrelation": 0.85,
            "motionClass": "flutter-wave",
        }

    def fake_flutter_sequence_with_prefix_error(self, frame_dir, region, *, sample_columns=96):
        if "/prefix/N8/frames" in str(frame_dir):
            raise ValueError("bad prefix extraction")
        return self.fake_flutter_sequence(frame_dir, region, sample_columns=sample_columns)

    def test_phase_shift_scores_worse_than_matched_phase(self):
        reference = self.make_trace(phase=0.0)
        matched = self.make_trace(phase=0.0)
        shifted = self.make_trace(phase=math.pi / 2.0)

        matched_score = score_temporal_trace(reference, matched)
        shifted_score = score_temporal_trace(reference, shifted)

        self.assertLess(matched_score["rmse"], shifted_score["rmse"])
        self.assertGreater(matched_score["correlation"], shifted_score["correlation"])

    def test_align_trace_pair_resamples_candidate_to_reference_frame_count(self):
        reference = self.make_trace(frames=50)
        candidate = self.make_trace(frames=90)

        aligned_reference, aligned_candidate = align_trace_pair(reference, candidate)
        score = score_temporal_trace(aligned_reference, aligned_candidate)

        self.assertEqual(aligned_reference.shape, (50, 48))
        self.assertEqual(aligned_candidate.shape, (50, 48))
        self.assertLess(score["rmse"], 0.05)
        self.assertGreater(score["correlation"], 0.98)

    def test_resample_trace_time_axis_rejects_empty_and_non_finite_traces(self):
        with self.assertRaises(ValueError):
            resample_trace_time_axis(np.zeros((0, 48), dtype=np.float32), 50)
        bad_trace = self.make_trace(frames=50)
        bad_trace[0, 0] = math.nan

        with self.assertRaises(ValueError):
            resample_trace_time_axis(bad_trace, 50)

    def test_score_temporal_trace_rejects_empty_traces(self):
        empty = np.asarray([], dtype=np.float32)

        with self.assertRaises(ValueError):
            score_temporal_trace(empty, empty)

    def test_apply_time_phase_rolls_frames_and_rejects_invalid_trace(self):
        trace = np.asarray([[1.0, 10.0], [2.0, 20.0], [3.0, 30.0]], dtype=np.float32)

        shifted = apply_time_phase(trace, -1)

        np.testing.assert_allclose(shifted, np.asarray([[2.0, 20.0], [3.0, 30.0], [1.0, 10.0]]))
        with self.assertRaises(ValueError):
            apply_time_phase(np.asarray([1.0, 2.0], dtype=np.float32), 1)
        with self.assertRaises(ValueError):
            apply_time_phase(trace, 1.5)

    def test_apply_amplitude_scale_scales_around_column_mean(self):
        trace = np.asarray([[0.0, 10.0], [2.0, 14.0], [4.0, 18.0]], dtype=np.float32)

        flattened = apply_amplitude_scale(trace, 0.0)
        doubled = apply_amplitude_scale(trace, 2.0)

        np.testing.assert_allclose(flattened, np.asarray([[2.0, 14.0], [2.0, 14.0], [2.0, 14.0]]))
        np.testing.assert_allclose(doubled, np.asarray([[-2.0, 6.0], [2.0, 14.0], [6.0, 22.0]]))
        for bad_scale in (-0.1, math.nan, math.inf):
            with self.subTest(scale=bad_scale):
                with self.assertRaises(ValueError):
                    apply_amplitude_scale(trace, bad_scale)

    def test_apply_temporal_smoothing_uses_edge_padding(self):
        trace = np.asarray([[0.0], [9.0], [0.0]], dtype=np.float32)

        smoothed = apply_temporal_smoothing(trace, 1)
        unchanged = apply_temporal_smoothing(trace, 0)

        np.testing.assert_allclose(smoothed, np.asarray([[3.0], [3.0], [3.0]], dtype=np.float32))
        np.testing.assert_allclose(unchanged, trace)
        self.assertIsNot(unchanged, trace)
        with self.assertRaises(ValueError):
            apply_temporal_smoothing(trace, -1)
        with self.assertRaises(ValueError):
            apply_temporal_smoothing(trace, 1.5)

    def test_score_temporal_trace_rejects_non_finite_values(self):
        valid = np.asarray([0.0, 1.0, 2.0], dtype=np.float32)

        for bad_trace in (
            np.asarray([0.0, math.nan, 2.0], dtype=np.float32),
            np.asarray([0.0, math.inf, 2.0], dtype=np.float32),
        ):
            with self.subTest(bad_trace=bad_trace):
                with self.assertRaises(ValueError):
                    score_temporal_trace(bad_trace, valid)
                with self.assertRaises(ValueError):
                    score_temporal_trace(valid, bad_trace)

    def test_candidate_result_score_fails_closed_for_non_finite_fields(self):
        for field in ("rmse", "correlation", "confidence"):
            for value in (math.nan, math.inf, -math.inf):
                kwargs = {"rmse": 0.05, "correlation": 0.96, "confidence": 0.90}
                kwargs[field] = value
                with self.subTest(field=field, value=value):
                    candidate = CandidateResult("bad", "shake", evidence={}, **kwargs)

                    self.assertEqual(candidate.score, 0.0)

    def test_rank_candidates_prefers_low_error_high_confidence(self):
        candidates = [
            CandidateResult("bad", "shake", rmse=0.40, correlation=0.20, confidence=0.30, evidence={}),
            CandidateResult("good", "shake", rmse=0.05, correlation=0.96, confidence=0.90, evidence={}),
            CandidateResult("medium", "waterwaves", rmse=0.18, correlation=0.70, confidence=0.60, evidence={}),
        ]

        ranked = rank_candidates(candidates)

        self.assertEqual(ranked[0].name, "good")
        self.assertEqual(ranked[-1].name, "bad")

    def test_step_discontinuity_has_higher_jerk_than_smooth_trace(self):
        smooth_trace = self.make_trace(phase=0.0, amp=2.0, frames=32)
        step_trace = smooth_trace.copy()
        step_trace[16:, :] += 12.0

        smooth_result = extract_trace_features(self.make_edge_trace(smooth_trace))
        step_result = extract_trace_features(self.make_edge_trace(step_trace))

        self.assertGreater(step_result["jerkP95Px"], smooth_result["jerkP95Px"])

    def test_extract_trace_features_uses_expected_feature_names(self):
        result = extract_trace_features(self.make_edge_trace(self.make_trace()))

        self.assertEqual(
            set(result),
            {
                "edgeTrace",
                "rigidMotionAmplitudePx",
                "flutterResidualRmsPx",
                "adjacentDeltaP95Px",
                "jerkP95Px",
                "travelLagFrames",
                "travelCorrelation",
                "motionClass",
            },
        )

    def test_under_determined_when_top_candidates_are_close(self):
        report = {
            "alignmentConfidence": 0.92,
            "rankedCandidates": [
                {"classification": "shake-sampler-or-coordinate-mismatch", "score": 0.80},
                {"classification": "waterwaves-sampler-or-coordinate-mismatch", "score": 0.79},
            ],
        }

        self.assertEqual(
            classify_fit_report(report),
            "black-box-underdetermined-need-graphics-capture",
        )

    def test_high_confidence_time_shift_classifies_as_time_phase_mismatch(self):
        report = {
            "alignmentConfidence": 0.94,
            "rankedCandidates": [
                {"classification": "time-scale-or-phase-mismatch", "score": 0.91},
                {"classification": "shake-sampler-or-coordinate-mismatch", "score": 0.78},
            ],
        }

        self.assertEqual(classify_fit_report(report), "time-scale-or-phase-mismatch")

    def test_low_alignment_stops_before_runtime_claim(self):
        report = {
            "alignmentConfidence": 0.55,
            "rankedCandidates": [
                {"classification": "shake-sampler-or-coordinate-mismatch", "score": 0.95},
            ],
        }

        self.assertEqual(classify_fit_report(report), "reference-alignment-insufficient")

    def test_non_finite_alignment_confidence_fails_closed(self):
        for alignment in (None, math.nan, math.inf, -math.inf, "not-a-number"):
            with self.subTest(alignment=alignment):
                report = {
                    "alignmentConfidence": alignment,
                    "rankedCandidates": [
                        {"classification": "shake-sampler-or-coordinate-mismatch", "score": 0.95},
                    ],
                }

                self.assertEqual(classify_fit_report(report), "reference-alignment-insufficient")

    def test_non_finite_top_candidate_score_does_not_return_renderer_classification(self):
        for score in (math.nan, math.inf, -math.inf, "not-a-score"):
            with self.subTest(score=score):
                report = {
                    "alignmentConfidence": 0.95,
                    "rankedCandidates": [
                        {"classification": "shake-sampler-or-coordinate-mismatch", "score": score},
                    ],
                }

                self.assertEqual(classify_fit_report(report), "no-actionable-runtime-gap-found")

    def test_trace_fit_confidence_returns_zero_for_non_finite_metrics(self):
        for metrics in (
            {"rmse": math.nan, "correlation": 0.95},
            {"rmse": math.inf, "correlation": 0.95},
            {"rmse": 0.05, "correlation": math.nan},
            {"rmse": 0.05, "correlation": math.inf},
        ):
            with self.subTest(metrics=metrics):
                self.assertEqual(_trace_fit_confidence(metrics), 0.0)

    def test_build_candidate_results_includes_required_candidate_families(self):
        windows = self.make_trace(frames=24)
        features_by_sequence = {
            "windows": {"features": {"lowerRibbonTip": {"edgeTrace": self.make_edge_trace(windows)}}},
            "yakkai-normal": {
                "features": {"lowerRibbonTip": {"edgeTrace": self.make_edge_trace(apply_time_phase(windows, 2))}}
            },
            "prefix-N7": {
                "features": {"lowerRibbonTip": {"edgeTrace": self.make_edge_trace(apply_amplitude_scale(windows, 0.65))}}
            },
            "prefix-N8": {
                "features": {"lowerRibbonTip": {"edgeTrace": self.make_edge_trace(apply_amplitude_scale(windows, 1.5))}}
            },
            "prefix-N10": {
                "features": {"lowerRibbonTip": {"edgeTrace": self.make_edge_trace(apply_temporal_smoothing(windows, 1))}}
            },
            "prefix-N11": {
                "features": {"lowerRibbonTip": {"edgeTrace": self.make_edge_trace(apply_temporal_smoothing(windows, 2))}}
            },
        }

        candidates = build_candidate_results(features_by_sequence, "lowerRibbonTip")
        classifications = {candidate.classification for candidate in candidates}
        names = {candidate.name for candidate in candidates}

        self.assertIn("normal-yakkai", names)
        self.assertIn("prefix-N7", names)
        self.assertIn("time-scale-or-phase-mismatch", classifications)
        self.assertIn("shake-sampler-or-coordinate-mismatch", classifications)
        self.assertIn("waterwaves-sampler-or-coordinate-mismatch", classifications)
        self.assertIn("effect-pass-order-or-prefix-target-mismatch", classifications)

    def test_write_candidate_report_writes_ranked_json_and_markdown(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output_dir = Path(tmpdir)
            windows = self.make_trace(frames=24)
            for filename, sequence, trace in (
                ("features-windows.json", "windows", windows),
                ("features-yakkai-normal.json", "yakkai-normal", apply_time_phase(windows, 2)),
                ("features-prefix-N7.json", "prefix-N7", apply_amplitude_scale(windows, 0.65)),
                ("features-prefix-N8.json", "prefix-N8", apply_amplitude_scale(windows, 1.5)),
                ("features-prefix-N10.json", "prefix-N10", apply_temporal_smoothing(windows, 1)),
                ("features-prefix-N11.json", "prefix-N11", apply_temporal_smoothing(windows, 2)),
            ):
                self.write_json(
                    output_dir / filename,
                    {
                        "sequence": sequence,
                        "features": {
                            "lowerRibbonTip": {
                                "edgeTrace": self.make_edge_trace(trace),
                            }
                        },
                    },
                )

            report = write_candidate_report(output_dir, output_dir, region_name="lowerRibbonTip")

            report_json = json.loads((output_dir / "candidate-ranking.json").read_text(encoding="utf-8"))
            report_markdown = (output_dir / "candidate-ranking.md").read_text(encoding="utf-8")

        self.assertEqual(report["regionName"], "lowerRibbonTip")
        self.assertEqual(report_json["regionName"], "lowerRibbonTip")
        self.assertIn("alignmentConfidence", report_json)
        self.assertIn("classification", report_json)
        self.assertGreater(len(report_json["rankedCandidates"]), 0)
        self.assertIn("time-scale-or-phase-mismatch", {row["classification"] for row in report_json["rankedCandidates"]})
        self.assertIn("# Candidate Ranking", report_markdown)

    def test_write_candidate_report_marks_low_alignment_candidates_diagnostic_only(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output_dir = Path(tmpdir)
            windows = self.make_trace(frames=24)
            flat_trace = np.zeros_like(windows)
            for filename, sequence, trace in (
                ("features-windows.json", "windows", windows),
                ("features-yakkai-normal.json", "yakkai-normal", flat_trace),
            ):
                self.write_json(
                    output_dir / filename,
                    {
                        "sequence": sequence,
                        "features": {
                            "lowerRibbonTip": {
                                "edgeTrace": self.make_edge_trace(trace),
                            }
                        },
                    },
                )

            report = write_candidate_report(output_dir, output_dir, region_name="lowerRibbonTip")
            report_json = json.loads((output_dir / "candidate-ranking.json").read_text(encoding="utf-8"))
            report_markdown = (output_dir / "candidate-ranking.md").read_text(encoding="utf-8")

        self.assertEqual(report["classification"], "reference-alignment-insufficient")
        self.assertEqual(report_json["candidateUse"], "diagnostic-only")
        self.assertIn("below threshold", report_json["gateReason"])
        self.assertTrue(report_json["rankedCandidates"])
        self.assertFalse(report_json["rankedCandidates"][0]["usableForRendererFix"])
        self.assertIn("Candidate rows are exploratory only", report_markdown)

    def test_empty_frame_directory_inventory_fails_not_enough_frames(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            inventory = inventory_frame_directory(Path(tmpdir))

        self.assertFalse(inventory["ok"])
        self.assertEqual(inventory["reason"], "not-enough-frames")

    def test_list_frame_files_sorts_frames_and_ignores_non_frames(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            directory = Path(tmpdir)
            for filename in ("frame-010.PNG", "notes.txt", "frame-002.jpg", "frame-001.JPEG"):
                (directory / filename).write_text("placeholder", encoding="utf-8")

            frames = list_frame_files(directory)

        self.assertEqual(
            [frame.name for frame in frames],
            ["frame-001.JPEG", "frame-002.jpg", "frame-010.PNG"],
        )

    def test_valid_region_config_loads(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "regions.json"
            self.write_json(
                path,
                {
                    "baseSize": [1600, 900],
                    "regions": {
                        "lowerRibbonTip": [1170, 260, 210, 180],
                    },
                },
            )

            config = load_region_config(path)

        self.assertEqual(config["baseSize"], [1600, 900])
        self.assertEqual(config["regions"]["lowerRibbonTip"], [1170, 260, 210, 180])

    def test_invalid_region_config_marks_inventory_invalid_regions(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            windows_frames = root / "windows"
            yakkai_frames = root / "yakkai"
            prefix_root = root / "prefix"
            regions = root / "regions.json"
            self.make_frame_set(windows_frames)
            self.make_frame_set(yakkai_frames)
            prefix_root.mkdir()
            self.write_json(
                regions,
                {
                    "baseSize": [1600, 900],
                    "regions": {
                        "bad": [0, 0, 0, 10],
                    },
                },
            )

            inventory = build_reference_inventory(windows_frames, yakkai_frames, prefix_root, regions)

        self.assertFalse(inventory["ok"])
        self.assertEqual(inventory["inputs"]["regions"]["reason"], "invalid-regions")
        self.assertTrue(any("invalid-regions" in error for error in inventory["errors"]))

    def test_cli_writes_feature_artifacts_and_records_missing_prefixes(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            windows_frames = root / "windows"
            yakkai_frames = root / "yakkai"
            prefix_root = root / "prefix"
            output_dir = root / "reports"
            regions = root / "regions.json"
            self.make_png_frame_set(windows_frames)
            self.make_png_frame_set(yakkai_frames)
            self.make_png_frame_set(prefix_root / "N7" / "frames")
            self.write_json(
                regions,
                {
                    "baseSize": [16, 9],
                    "regions": {
                        "lowerRibbonAndWall": [0, 0, 4, 4],
                        "lowerRibbonTip": [1, 1, 4, 4],
                        "wallGlowBehindRibbon": [2, 2, 4, 4],
                    },
                },
            )

            with mock.patch(
                "arona_we_behavior_fitter.measure_flutter_sequence",
                side_effect=self.fake_flutter_sequence,
            ):
                exit_code = main(
                    [
                        "--windows-frames",
                        str(windows_frames),
                        "--yakkai-frames",
                        str(yakkai_frames),
                        "--prefix-root",
                        str(prefix_root),
                        "--regions",
                        str(regions),
                        "--output-dir",
                        str(output_dir),
                    ]
                )

            self.assertEqual(exit_code, 0)
            for filename in (
                "features-windows.json",
                "features-yakkai-normal.json",
                "features-prefix-N7.json",
            ):
                feature_report = json.loads((output_dir / filename).read_text(encoding="utf-8"))
                self.assertEqual(
                    set(feature_report["features"]),
                    {"lowerRibbonAndWall", "lowerRibbonTip", "wallGlowBehindRibbon"},
                )
                self.assertIn("jerkP95Px", feature_report["features"]["lowerRibbonTip"])

            self.assertFalse((output_dir / "features-prefix-N8.json").exists())
            summary = (output_dir / "feature-summary.md").read_text(encoding="utf-8")
            self.assertIn("features-prefix-N7.json", summary)
            self.assertIn("prefix-N8", summary)
            self.assertIn("missing-directory", summary)

    def test_cli_records_prefix_extraction_error_without_failing(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            windows_frames = root / "windows"
            yakkai_frames = root / "yakkai"
            prefix_root = root / "prefix"
            output_dir = root / "reports"
            regions = root / "regions.json"
            self.make_png_frame_set(windows_frames)
            self.make_png_frame_set(yakkai_frames)
            self.make_png_frame_set(prefix_root / "N8" / "frames")
            self.write_json(
                regions,
                {
                    "baseSize": [16, 9],
                    "regions": {
                        "lowerRibbonAndWall": [0, 0, 4, 4],
                        "lowerRibbonTip": [1, 1, 4, 4],
                        "wallGlowBehindRibbon": [2, 2, 4, 4],
                    },
                },
            )

            with mock.patch(
                "arona_we_behavior_fitter.measure_flutter_sequence",
                side_effect=self.fake_flutter_sequence_with_prefix_error,
            ):
                exit_code = main(
                    [
                        "--windows-frames",
                        str(windows_frames),
                        "--yakkai-frames",
                        str(yakkai_frames),
                        "--prefix-root",
                        str(prefix_root),
                        "--regions",
                        str(regions),
                        "--output-dir",
                        str(output_dir),
                    ]
                )

            self.assertEqual(exit_code, 0)
            self.assertTrue((output_dir / "features-windows.json").exists())
            self.assertFalse((output_dir / "features-prefix-N8.json").exists())
            summary = (output_dir / "feature-summary.md").read_text(encoding="utf-8")
            self.assertIn("prefix-N8", summary)
            self.assertIn("feature-extraction-failed: bad prefix extraction", summary)

    def test_missing_prefix_root_does_not_fail_required_inventory_or_cli(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            windows_frames = root / "windows"
            yakkai_frames = root / "yakkai"
            prefix_root = root / "missing-prefix"
            output_dir = root / "reports"
            regions = root / "regions.json"
            self.make_png_frame_set(windows_frames)
            self.make_png_frame_set(yakkai_frames)
            self.write_json(
                regions,
                {
                    "baseSize": [16, 9],
                    "regions": {
                        "lowerRibbonAndWall": [0, 0, 4, 4],
                        "lowerRibbonTip": [1, 1, 4, 4],
                        "wallGlowBehindRibbon": [2, 2, 4, 4],
                    },
                },
            )

            inventory = build_reference_inventory(windows_frames, yakkai_frames, prefix_root, regions)
            with mock.patch(
                "arona_we_behavior_fitter.measure_flutter_sequence",
                side_effect=self.fake_flutter_sequence,
            ):
                exit_code = main(
                    [
                        "--windows-frames",
                        str(windows_frames),
                        "--yakkai-frames",
                        str(yakkai_frames),
                        "--prefix-root",
                        str(prefix_root),
                        "--regions",
                        str(regions),
                        "--output-dir",
                        str(output_dir),
                    ]
                )

            self.assertTrue(inventory["ok"])
            self.assertFalse(inventory["inputs"]["prefixRoot"]["ok"])
            self.assertEqual(exit_code, 0)
            summary = (output_dir / "feature-summary.md").read_text(encoding="utf-8")
            self.assertIn("prefix-N7", summary)
            self.assertIn("missing-directory", summary)

    def test_required_frame_size_mismatch_fails_cli_and_skips_feature_json(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            windows_frames = root / "windows"
            yakkai_frames = root / "yakkai"
            prefix_root = root / "prefix"
            output_dir = root / "reports"
            regions = root / "regions.json"
            self.make_png_frame_set(windows_frames, size=(12, 8))
            self.make_png_frame_set(yakkai_frames, size=(16, 9))
            prefix_root.mkdir()
            self.write_json(
                regions,
                {
                    "baseSize": [16, 9],
                    "regions": {
                        "lowerRibbonAndWall": [0, 0, 4, 4],
                        "lowerRibbonTip": [1, 1, 4, 4],
                        "wallGlowBehindRibbon": [2, 2, 4, 4],
                    },
                },
            )
            (output_dir / "features-windows.json").parent.mkdir(parents=True, exist_ok=True)
            (output_dir / "features-windows.json").write_text("stale", encoding="utf-8")

            with mock.patch(
                "arona_we_behavior_fitter.measure_flutter_sequence",
                side_effect=self.fake_flutter_sequence,
            ), contextlib.redirect_stderr(io.StringIO()):
                exit_code = main(
                    [
                        "--windows-frames",
                        str(windows_frames),
                        "--yakkai-frames",
                        str(yakkai_frames),
                        "--prefix-root",
                        str(prefix_root),
                        "--regions",
                        str(regions),
                        "--output-dir",
                        str(output_dir),
                    ]
                )

            self.assertNotEqual(exit_code, 0)
            self.assertFalse((output_dir / "features-windows.json").exists())
            summary = (output_dir / "feature-summary.md").read_text(encoding="utf-8")
            self.assertIn("windows", summary)
            self.assertIn("frame-size-mismatch", summary)

    def test_required_inventory_failure_removes_stale_feature_artifacts(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            windows_frames = root / "missing-windows"
            yakkai_frames = root / "yakkai"
            prefix_root = root / "prefix"
            output_dir = root / "reports"
            regions = root / "regions.json"
            self.make_png_frame_set(yakkai_frames)
            prefix_root.mkdir()
            self.write_json(
                regions,
                {
                    "baseSize": [16, 9],
                    "regions": {
                        "lowerRibbonAndWall": [0, 0, 4, 4],
                        "lowerRibbonTip": [1, 1, 4, 4],
                        "wallGlowBehindRibbon": [2, 2, 4, 4],
                    },
                },
            )
            stale_files = [
                "features-windows.json",
                "features-yakkai-normal.json",
                "features-prefix-N7.json",
                "features-prefix-N8.json",
                "features-prefix-N10.json",
                "features-prefix-N11.json",
                "feature-summary.md",
            ]
            output_dir.mkdir(parents=True)
            for filename in stale_files:
                (output_dir / filename).write_text("stale", encoding="utf-8")

            with contextlib.redirect_stderr(io.StringIO()):
                exit_code = main(
                    [
                        "--windows-frames",
                        str(windows_frames),
                        "--yakkai-frames",
                        str(yakkai_frames),
                        "--prefix-root",
                        str(prefix_root),
                        "--regions",
                        str(regions),
                        "--output-dir",
                        str(output_dir),
                    ]
                )

            self.assertEqual(exit_code, 1)
            self.assertTrue((output_dir / "reference-inventory.json").exists())
            self.assertTrue((output_dir / "reference-inventory.md").exists())
            for filename in stale_files:
                self.assertFalse((output_dir / filename).exists(), filename)

    def test_prefix_frame_size_mismatch_is_recorded_without_failing_cli(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            windows_frames = root / "windows"
            yakkai_frames = root / "yakkai"
            prefix_root = root / "prefix"
            output_dir = root / "reports"
            regions = root / "regions.json"
            self.make_png_frame_set(windows_frames, size=(16, 9))
            self.make_png_frame_set(yakkai_frames, size=(16, 9))
            self.make_png_frame_set(prefix_root / "N7" / "frames", size=(12, 8))
            self.write_json(
                regions,
                {
                    "baseSize": [16, 9],
                    "regions": {
                        "lowerRibbonAndWall": [0, 0, 4, 4],
                        "lowerRibbonTip": [1, 1, 4, 4],
                        "wallGlowBehindRibbon": [2, 2, 4, 4],
                    },
                },
            )
            (output_dir / "features-prefix-N7.json").parent.mkdir(parents=True, exist_ok=True)
            (output_dir / "features-prefix-N7.json").write_text("stale", encoding="utf-8")

            with mock.patch(
                "arona_we_behavior_fitter.measure_flutter_sequence",
                side_effect=self.fake_flutter_sequence,
            ):
                exit_code = main(
                    [
                        "--windows-frames",
                        str(windows_frames),
                        "--yakkai-frames",
                        str(yakkai_frames),
                        "--prefix-root",
                        str(prefix_root),
                        "--regions",
                        str(regions),
                        "--output-dir",
                        str(output_dir),
                    ]
                )

            self.assertEqual(exit_code, 0)
            self.assertTrue((output_dir / "features-windows.json").exists())
            self.assertFalse((output_dir / "features-prefix-N7.json").exists())
            summary = (output_dir / "feature-summary.md").read_text(encoding="utf-8")
            self.assertIn("prefix-N7", summary)
            self.assertIn("frame-size-mismatch", summary)

    def test_kymograph_image_creation_returns_non_empty_png(self):
        windows_feature = {"edgeTrace": self.make_edge_trace(self.make_trace(frames=8))}
        yakkai_feature = {"edgeTrace": self.make_edge_trace(self.make_trace(frames=8, amp=4.0))}

        image = create_kymograph_image(
            windows_feature,
            yakkai_feature,
            left_label="Windows lowerRibbonTip",
            right_label="Yakkai lowerRibbonTip",
        )
        png_bytes = io.BytesIO()
        image.save(png_bytes, format="PNG")

        self.assertGreater(image.size[0], 0)
        self.assertGreater(image.size[1], 0)
        self.assertGreater(len(png_bytes.getvalue()), 100)

    def test_candidate_source_frame_resolution_handles_normal_and_prefix_sources(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            yakkai_frames = root / "yakkai" / "day-normal"
            prefix_root = root / "prefix"

            normal_path = resolve_candidate_source_frames(
                {"evidence": {"sourceSequence": "yakkai-normal"}},
                yakkai_frames=yakkai_frames,
                prefix_root=prefix_root,
            )
            prefix_path = resolve_candidate_source_frames(
                {"evidence": {"sourceSequence": "prefix-N11"}},
                yakkai_frames=yakkai_frames,
                prefix_root=prefix_root,
            )

        self.assertEqual(normal_path, yakkai_frames)
        self.assertEqual(prefix_path, prefix_root / "N11" / "frames")

    def test_final_summary_low_alignment_recommends_stronger_windows_evidence_not_renderer_edit(self):
        candidate_report = {
            "regionName": "lowerRibbonTip",
            "alignmentConfidence": 0.73758,
            "classification": "reference-alignment-insufficient",
            "candidateUse": "diagnostic-only",
            "gateReason": "Alignment confidence is below threshold.",
            "rankedCandidates": [
                {
                    "name": "prefix-N11-amplitude-scale-0p5",
                    "classification": "shake-sampler-or-coordinate-mismatch",
                    "score": 0.784769,
                    "rmse": 0.191948,
                    "correlation": 0.742001,
                    "confidence": 0.784934,
                    "usableForRendererFix": False,
                    "evidence": {
                        "candidateFamily": "amplitude-scale",
                        "sourceSequence": "prefix-N11",
                        "scale": 0.5,
                    },
                }
            ],
        }
        features_by_sequence = {
            "windows": {
                "frameCount": 8,
                "features": {
                    "lowerRibbonTip": extract_trace_features(self.make_edge_trace(self.make_trace(frames=8)))
                },
            },
            "yakkai-normal": {
                "frameCount": 8,
                "features": {
                    "lowerRibbonTip": extract_trace_features(self.make_edge_trace(self.make_trace(frames=8, amp=4.0)))
                },
            },
        }
        inventory = {
            "inputs": {
                "windowsFrames": {"path": "/windows", "frameCount": 8, "ok": True},
                "yakkaiFrames": {"path": "/yakkai", "frameCount": 8, "ok": True},
                "prefixRoot": {"path": "/prefix", "ok": True},
                "regions": {"path": "/regions.json", "regionNames": ["lowerRibbonTip"], "ok": True},
            }
        }

        summary = render_final_summary_markdown(
            inventory=inventory,
            features_by_sequence=features_by_sequence,
            candidate_report=candidate_report,
            visual_artifacts={
                "contact-windows-vs-yakkai-normal.png": True,
                "contact-windows-vs-top-candidate.png": True,
                "kymograph-windows-vs-yakkai-normal.png": True,
                "kymograph-windows-vs-top-candidate.png": True,
            },
        )
        recommended_section = summary.split("## Recommended Next Goal", 1)[1].split("## Human Visual Gate", 1)[0]

        self.assertIn("stronger Windows evidence", recommended_section)
        self.assertNotIn("native/scene_backend", recommended_section)
        self.assertNotIn("shader", recommended_section.lower())
        self.assertNotIn("renderer edit", recommended_section.lower())
        self.assertIn(
            "contact sheets and top-candidate kymographs show source data only",
            summary,
        )
        self.assertIn(
            "Trace transforms are scoring diagnostics and are not synthesized into visual artifacts.",
            summary,
        )

    def test_top_candidate_visual_label_names_raw_source_and_scoring_transform(self):
        label = _candidate_visual_label(
            {
                "evidence": {
                    "candidateFamily": "amplitude-scale",
                    "sourceSequence": "prefix-N11",
                    "scale": 0.5,
                }
            }
        )

        self.assertEqual(
            label,
            "Top candidate source: prefix-N11 (transform used for scoring: amplitude scale 0.5)",
        )


if __name__ == "__main__":
    unittest.main()
