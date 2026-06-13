import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from PIL import Image, ImageDraw

import tools.arona_mouse_parallax_probe as probe


def _config() -> probe.ProbeConfig:
    root = Path("/repo")
    return probe.ProbeConfig(
        repo_root=root,
        harness=root / "build/native/scene_harness/yakkai_scene_harness",
        source=root / "scene.pkg",
        assets=root / "assets",
        output_root=root / "smoke-tests/artifacts/tmp/arona-mouse-parallax-probe",
        capture_delay_ms=1234,
    )


def _manifest(offset_x: float, offset_y: float = 0.0) -> dict:
    return {
        "mouseParallax": {
            "parallaxLayers": [
                {
                    "layerId": 328,
                    "expectedOffset": [offset_x, offset_y],
                }
            ]
        }
    }


def _anchor_manifest(parallax_layers: list[dict]) -> dict:
    return {
        "mouseParallax": {
            "authoredTransformAnchorLayerIds": [195, 217, 325, 328],
            "parallaxLayers": parallax_layers,
        }
    }


def _gate_summary(
    *,
    classification: str = "movement-detected",
    anchor_classification: str | None = "anchor-propagation-evidence-present",
    center_offset: float = 0.0,
    left_offset: float = 8.0,
    right_offset: float = -8.0,
    motion_classification: str = "opposite-sign-motion",
    left_region_dx: int = 6,
    right_region_dx: int = -6,
    layer_count: int = 17,
) -> dict:
    layer_ids = [328] + [index for index in range(layer_count - 1)]
    layers = [
        {
            "layerId": layer_id,
            "expectedOffset": [0.0, 0.0],
        }
        for layer_id in layer_ids
    ]
    if layers:
        layers[0]["expectedOffset"] = [center_offset, 0.0]

    left_layers = [dict(layer) for layer in layers]
    center_layers = [dict(layer) for layer in layers]
    right_layers = [dict(layer) for layer in layers]
    if left_layers:
        left_layers[0]["expectedOffset"] = [left_offset, 0.0]
        center_layers[0]["expectedOffset"] = [center_offset, 0.0]
        right_layers[0]["expectedOffset"] = [right_offset, 0.0]

    summary = {
        "classification": classification,
        "motionRegions": {
            name: {
                "classification": motion_classification,
                "leftVsCenter": {"dx": left_region_dx, "dy": 0, "rmse": 0.01},
                "rightVsCenter": {"dx": right_region_dx, "dy": 0, "rmse": 0.01},
            }
            for name in probe.ARONA_REVIEW_REGIONS
        },
        "manifests": {
            "left": {"mouseParallax": {"authoredTransformAnchorLayerIds": [328], "parallaxLayers": left_layers}},
            "center": {"mouseParallax": {"authoredTransformAnchorLayerIds": [328], "parallaxLayers": center_layers}},
            "right": {"mouseParallax": {"authoredTransformAnchorLayerIds": [328], "parallaxLayers": right_layers}},
        },
    }
    if anchor_classification is not None:
        summary["anchorPropagation"] = {
            "classification": anchor_classification,
        }
    return summary


def _write_probe_artifacts(output_root: Path, classification: str) -> None:
    colors = {
        "left": (255, 0, 0),
        "center": (0, 255, 0),
        "right": (0, 0, 255),
    }
    for name, color in colors.items():
        variant_dir = output_root / name
        variant_dir.mkdir(parents=True, exist_ok=True)
        Image.new("RGB", (2, 2), color).save(variant_dir / "still.png")

    report = {
        "classification": classification,
        "imageMetrics": {
            "leftCenterRmse": 0.1,
            "rightCenterRmse": 0.2,
        },
        "manifests": {
            "center": {
                "mouseParallax": {
                    "parallaxLayers": [{"layerId": 328}, {"layerId": 329}],
                }
            }
        },
    }
    (output_root / "report.json").write_text(json.dumps(report), encoding="utf-8")


class AronaMouseParallaxProbeTests(unittest.TestCase):
    def test_parse_window_sizes_accepts_multiple_sizes(self):
        self.assertEqual(
            probe.parse_window_sizes("1600x900,1920x1080,2560x1440"),
            ["1600x900", "1920x1080", "2560x1440"],
        )

    def test_estimate_translation_detects_right_shift(self):
        base = Image.new("L", (80, 50), 0)
        moved = Image.new("L", (80, 50), 0)
        ImageDraw.Draw(base).rectangle((20, 15, 45, 35), fill=255)
        ImageDraw.Draw(moved).rectangle((26, 15, 51, 35), fill=255)
        result = probe.estimate_translation(base, moved, (0, 0, 70, 50), 10, 1)
        self.assertEqual(result["dx"], 6)
        self.assertEqual(result["dy"], 0)

    def test_estimate_translation_prefers_zero_shift_when_scores_tie(self):
        base = Image.new("L", (40, 30), 0)
        moved = Image.new("L", (40, 30), 0)

        result = probe.estimate_translation(base, moved, (5, 5, 20, 20), max_shift=5, step=1)

        self.assertEqual(result["dx"], 0)
        self.assertEqual(result["dy"], 0)

    def test_estimate_translation_skips_out_of_bounds_shift_candidates(self):
        base = Image.new("L", (12, 12), 0)
        moved = Image.new("L", (12, 12), 0)
        ImageDraw.Draw(base).rectangle((4, 0, 7, 3), fill=255)
        ImageDraw.Draw(moved).rectangle((0, 0, 3, 3), fill=255)

        result = probe.estimate_translation(base, moved, (0, 0, 8, 4), max_shift=4, step=1)

        self.assertNotEqual(result["dx"], -4)
        self.assertGreaterEqual(result["dx"], 0)
        self.assertEqual(result["dy"], 0)

    def test_estimate_translation_downsamples_large_regions(self):
        base = Image.new("L", (1000, 700), 0)
        moved = Image.new("L", (1000, 700), 0)
        ImageDraw.Draw(base).rectangle((320, 240, 520, 420), fill=255)
        ImageDraw.Draw(moved).rectangle((344, 240, 544, 420), fill=255)

        result = probe.estimate_translation(base, moved, (50, 50, 950, 650), max_shift=80, step=1)

        self.assertLess(result["searchScale"], 1.0)
        self.assertLess(result["searchedMaxShift"], 80)
        self.assertAlmostEqual(result["dx"], 24, delta=3)
        self.assertEqual(result["dy"], 0)

    def test_estimate_translation_rejects_scaled_candidate_that_is_worse_than_zero_shift(self):
        base = Image.new("L", (80, 50), 0)
        ImageDraw.Draw(base).rectangle((20, 15, 45, 35), fill=255)
        moved = base.copy()
        search_base = base.copy()
        search_moved = Image.new("L", (80, 50), 0)
        ImageDraw.Draw(search_moved).rectangle((26, 15, 51, 35), fill=255)

        with mock.patch.object(
            probe,
            "_scaled_registration_inputs",
            return_value=(search_base, search_moved, (0, 0, 70, 50), 10, 1.0),
        ):
            result = probe.estimate_translation(base, moved, (0, 0, 70, 50), max_shift=10, step=1)

        self.assertEqual(result["dx"], 0)
        self.assertEqual(result["dy"], 0)
        self.assertEqual(result["rmse"], 0.0)
        self.assertEqual(result["zeroShiftRmse"], 0.0)

    def test_classifies_opposite_sign_motion_region(self):
        classification = probe.classify_motion_region(
            {"dx": 6, "dy": 0, "rmse": 0.01},
            {"dx": -5, "dy": 0, "rmse": 0.01},
        )

        self.assertEqual(classification, "opposite-sign-motion")

    def test_classifies_weak_motion_region(self):
        classification = probe.classify_motion_region(
            {"dx": 1, "dy": 0, "rmse": 0.01},
            {"dx": -1, "dy": 0, "rmse": 0.01},
        )

        self.assertEqual(classification, "weak-motion")

    def test_classifies_boundary_one_sided_motion_as_weak(self):
        classification = probe.classify_motion_region(
            {"dx": 2, "dy": -2, "rmse": 0.01},
            {"dx": 0, "dy": 0, "rmse": 0.01},
        )

        self.assertEqual(classification, "weak-motion")

    def test_classifies_same_direction_motion_region(self):
        classification = probe.classify_motion_region(
            {"dx": 4, "dy": 0, "rmse": 0.01},
            {"dx": 3, "dy": 0, "rmse": 0.01},
        )

        self.assertEqual(classification, "same-direction-motion")

    def test_classifies_one_sided_motion_region(self):
        classification = probe.classify_motion_region(
            {"dx": 0, "dy": 0, "rmse": 0.01},
            {"dx": -48, "dy": 0, "rmse": 0.01},
        )

        self.assertEqual(classification, "one-sided-motion")

    def test_classifies_uncertain_motion_region(self):
        classification = probe.classify_motion_region(
            {"dx": 4, "dy": 0, "rmse": probe.MOTION_REGION_RMSE_UNCERTAIN_THRESHOLD + 0.01},
            {"dx": -4, "dy": 0, "rmse": 0.01},
        )

        self.assertEqual(classification, "registration-uncertain")

    def test_classifies_missing_authored_transform_anchor_inventory(self):
        diagnostic = probe.classify_anchor_propagation(
            _anchor_manifest(
                [
                    {
                        "layerId": 405,
                        "layerName": "ARONA_CROP_SHEET",
                        "layerKind": "image",
                        "parentLayerId": 328,
                    }
                ]
            )
        )

        self.assertEqual(diagnostic["classification"], "anchor-inventory-missing")
        self.assertEqual(diagnostic["missingAnchorLayerIds"], [195, 217, 325, 328])

    def test_classifies_anchor_child_map_missing(self):
        diagnostic = probe.classify_anchor_propagation(
            _anchor_manifest(
                [
                    {"layerId": 195, "layerKind": "transform-anchor"},
                    {"layerId": 217, "layerKind": "transform-anchor"},
                    {"layerId": 325, "layerKind": "transform-anchor"},
                    {"layerId": 328, "layerKind": "transform-anchor"},
                ]
            )
        )

        self.assertEqual(diagnostic["classification"], "anchor-child-map-missing")

    def test_classifies_anchor_propagation_evidence_present(self):
        diagnostic = probe.classify_anchor_propagation(
            _anchor_manifest(
                [
                    {
                        "layerId": 195,
                        "layerKind": "transform-anchor",
                        "hasChildren": True,
                        "childLayerIds": [217],
                        "propagationExpectation": "parent-offset-affects-children",
                    },
                    {
                        "layerId": 217,
                        "layerKind": "transform-anchor",
                        "hasChildren": True,
                        "childLayerIds": [325],
                        "propagationExpectation": "parent-offset-affects-children",
                    },
                    {
                        "layerId": 325,
                        "layerKind": "transform-anchor",
                        "hasChildren": True,
                        "childLayerIds": [328],
                        "propagationExpectation": "parent-offset-affects-children",
                    },
                    {
                        "layerId": 328,
                        "layerKind": "transform-anchor",
                        "hasChildren": True,
                        "childLayerIds": [405],
                        "propagationExpectation": "parent-offset-affects-children",
                    },
                ]
            )
        )

        self.assertEqual(diagnostic["classification"], "anchor-propagation-evidence-present")

    def test_parse_window_sizes_rejects_bad_size(self):
        with self.assertRaises(ValueError):
            probe.parse_window_sizes("1600x900,wide")

    def test_main_reports_bad_window_sizes_as_argparse_error(self):
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            with self.assertRaises(SystemExit) as raised:
                probe.main(
                    [
                        "--source",
                        "/repo/scene.pkg",
                        "--assets",
                        "/repo/assets",
                        "--window-sizes",
                        "wide",
                    ]
                )

        self.assertEqual(raised.exception.code, 2)
        self.assertIn("invalid window size: wide", stderr.getvalue())
        self.assertNotIn("Traceback", stderr.getvalue())

    def test_size_specific_output_dir_is_used(self):
        config = _config()
        sized = probe.config_for_window_size(config, "1920x1080")
        self.assertTrue(str(sized.output_root).endswith("arona-mouse-parallax-probe/1920x1080"))
        command = probe.build_harness_command(sized, "center", "0.5,0.5")
        self.assertIn("--window-size", command)
        self.assertIn("1920x1080", command)

    def test_main_uses_window_sizes_instead_of_window_size_for_sweep(self):
        with mock.patch.object(probe, "run_probe") as run_probe, mock.patch.object(
            probe, "run_probe_sweep", return_value=0
        ) as run_probe_sweep:
            result = probe.main(
                [
                    "--source",
                    "/repo/scene.pkg",
                    "--assets",
                    "/repo/assets",
                    "--window-size",
                    "1600x900",
                    "--window-sizes",
                    "1920x1080,2560x1440",
                ]
            )

        self.assertEqual(result, 0)
        run_probe.assert_not_called()
        run_probe_sweep.assert_called_once()
        self.assertEqual(run_probe_sweep.call_args.args[1], ["1920x1080", "2560x1440"])

    def test_main_passes_gate_to_single_probe(self):
        with mock.patch.object(probe, "run_probe", return_value=3) as run_probe:
            result = probe.main(
                [
                    "--source",
                    "/repo/scene.pkg",
                    "--assets",
                    "/repo/assets",
                    "--gate",
                ]
            )

        self.assertEqual(result, 3)
        run_probe.assert_called_once()
        self.assertTrue(run_probe.call_args.kwargs["gate"])

    def test_main_passes_gate_to_probe_sweep(self):
        with mock.patch.object(probe, "run_probe_sweep", return_value=3) as run_probe_sweep:
            result = probe.main(
                [
                    "--source",
                    "/repo/scene.pkg",
                    "--assets",
                    "/repo/assets",
                    "--window-sizes",
                    "1600x900,1920x1080",
                    "--gate",
                ]
            )

        self.assertEqual(result, 3)
        run_probe_sweep.assert_called_once()
        self.assertTrue(run_probe_sweep.call_args.kwargs["gate"])

    def test_run_probe_sweep_writes_aggregate_reports_and_contact_sheet(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            config = probe.ProbeConfig(
                repo_root=root,
                harness=root / "harness",
                source=root / "scene.pkg",
                assets=root / "assets",
                output_root=root / "smoke-tests/artifacts/tmp/arona-mouse-parallax-probe",
            )

            def fake_run_probe(sized_config: probe.ProbeConfig) -> int:
                _write_probe_artifacts(sized_config.output_root, f"class-{sized_config.window_size}")
                return 0

            with mock.patch.object(probe, "run_probe", side_effect=fake_run_probe):
                result = probe.run_probe_sweep(config, ["1600x900", "1920x1080"])

            self.assertEqual(result, 0)
            aggregate_json = config.output_root / "sweep-report.json"
            aggregate_md = config.output_root / "sweep-report.md"
            contact_sheet = config.output_root / "sweep-contact-sheet.png"
            self.assertTrue(aggregate_json.exists())
            self.assertTrue(aggregate_md.exists())
            self.assertTrue(contact_sheet.exists())

            data = json.loads(aggregate_json.read_text(encoding="utf-8"))
            self.assertEqual([row["windowSize"] for row in data], ["1600x900", "1920x1080"])
            self.assertEqual(data[0]["classification"], "class-1600x900")
            markdown = aggregate_md.read_text(encoding="utf-8")
            self.assertIn("| `1600x900` | `class-1600x900` | `0.10000000` | `0.20000000` | 2 |", markdown)
            self.assertIn("| `1920x1080` | `class-1920x1080` | `0.10000000` | `0.20000000` | 2 |", markdown)

    def test_write_reports_includes_motion_regions_and_diff_maps(self):
        with tempfile.TemporaryDirectory() as temp:
            output_root = Path(temp)
            for name in ("left", "center", "right"):
                variant_dir = output_root / name / "effect-captures"
                variant_dir.mkdir(parents=True, exist_ok=True)
                (variant_dir / "manifest.json").write_text(json.dumps(_manifest(0.0)), encoding="utf-8")

            center = Image.new("RGBA", (100, 80), (0, 0, 0, 255))
            left = Image.new("RGBA", (100, 80), (0, 0, 0, 255))
            right = Image.new("RGBA", (100, 80), (0, 0, 0, 255))
            ImageDraw.Draw(center).rectangle((70, 8, 88, 28), fill=(255, 255, 255, 255))
            ImageDraw.Draw(left).rectangle((74, 8, 92, 28), fill=(255, 255, 255, 255))
            ImageDraw.Draw(right).rectangle((66, 8, 84, 28), fill=(255, 255, 255, 255))
            ImageDraw.Draw(center).rectangle((45, 20, 60, 35), fill=(255, 255, 255, 255))
            ImageDraw.Draw(left).rectangle((49, 20, 64, 35), fill=(255, 255, 255, 255))
            ImageDraw.Draw(right).rectangle((41, 20, 56, 35), fill=(255, 255, 255, 255))
            center.save(output_root / "center" / "still.png")
            left.save(output_root / "left" / "still.png")
            right.save(output_root / "right" / "still.png")

            probe.write_reports(
                output_root,
                "movement-detected",
                {"leftCenterRmse": 0.1, "rightCenterRmse": 0.2},
            )

            report = json.loads((output_root / "report.json").read_text(encoding="utf-8"))
            self.assertIn("motionRegions", report)
            self.assertEqual(
                report["motionRegions"]["character"]["classification"],
                "opposite-sign-motion",
            )
            self.assertEqual(report["motionRegions"]["ribbon_tip"]["region"], "ribbon_tip")
            self.assertEqual(report["motionRegions"]["ribbon_tip"]["normalizedRegion"], [0.70, 0.05, 0.98, 0.42])
            self.assertEqual(report["motionRegions"]["ribbon_tip"]["pixelRegion"], [70, 4, 98, 34])
            self.assertTrue((output_root / "left-vs-center-diff.png").exists())
            self.assertTrue((output_root / "right-vs-center-diff.png").exists())
            markdown = (output_root / "report.md").read_text(encoding="utf-8")
            self.assertIn("| Region | Left vs center | Right vs center | Classification |", markdown)

    def test_write_reports_includes_anchor_propagation_diagnostics(self):
        with tempfile.TemporaryDirectory() as temp:
            output_root = Path(temp)
            for name in ("left", "center", "right"):
                variant_dir = output_root / name / "effect-captures"
                variant_dir.mkdir(parents=True, exist_ok=True)
                (variant_dir / "manifest.json").write_text(
                    json.dumps(
                        _anchor_manifest(
                            [
                                {
                                    "layerId": 405,
                                    "layerName": "ARONA_CROP_SHEET",
                                    "layerKind": "image",
                                    "parentLayerId": 328,
                                }
                            ]
                        )
                    ),
                    encoding="utf-8",
                )
                Image.new("RGBA", (100, 80), (0, 0, 0, 255)).save(output_root / name / "still.png")

            probe.write_reports(
                output_root,
                "movement-detected",
                {"leftCenterRmse": 0.1, "rightCenterRmse": 0.2},
            )

            report = json.loads((output_root / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(
                report["anchorPropagation"]["classification"],
                "anchor-inventory-missing",
            )
            self.assertEqual(report["anchorPropagation"]["missingAnchorLayerIds"], [195, 217, 325, 328])
            markdown = (output_root / "report.md").read_text(encoding="utf-8")
            self.assertIn("- Anchor propagation: `anchor-inventory-missing`", markdown)

    def test_write_diff_map_outputs_visible_opaque_pixels(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "diff.png"
            left = Image.new("RGBA", (2, 1), (10, 10, 10, 255))
            right = Image.new("RGBA", (2, 1), (10, 10, 10, 255))
            right.putpixel((1, 0), (30, 10, 10, 255))

            probe.write_diff_map(left, right, output)

            with Image.open(output) as diff:
                self.assertIn(diff.mode, ("RGB", "RGBA"))
                pixel = diff.getpixel((1, 0))
                channels = pixel if isinstance(pixel, tuple) else (pixel,)
                self.assertGreater(channels[0], 0)
                if diff.mode == "RGBA":
                    self.assertEqual(channels[3], 255)

    def test_build_harness_command_uses_repo_local_output_and_debug_mouse_position(self):
        command = probe.build_harness_command(_config(), "left", "0,0.5")

        self.assertIn("--debug-mouse-position", command)
        self.assertIn("0,0.5", command)
        self.assertIn("--debug-effect-captures", command)
        self.assertIn("--debug-effect-capture-layers", command)
        self.assertIn("405", command)
        command_text = " ".join(command)
        self.assertIn("smoke-tests/artifacts/tmp/arona-mouse-parallax-probe/left", command_text)

    def test_build_harness_command_includes_scene_properties_when_provided(self):
        config = probe.ProbeConfig(
            repo_root=Path("/repo"),
            harness=Path("/repo/harness"),
            source=Path("/repo/scene.pkg"),
            assets=Path("/repo/assets"),
            output_root=Path("/repo/smoke-tests/artifacts/tmp/arona-mouse-parallax-probe"),
            scene_properties_json='{"timeofday":{"value":"1"}}',
        )

        command = probe.build_harness_command(config, "center", "0.5,0.5")

        self.assertIn("--scene-properties-json", command)
        self.assertIn('{"timeofday":{"value":"1"}}', command)

    def test_classifies_opposite_offsets_and_image_delta_as_movement_detected(self):
        classification = probe.classify_probe(
            _manifest(10.0),
            _manifest(0.0),
            _manifest(-10.0),
            {"leftCenterRmse": 0.01, "rightCenterRmse": 0.01},
        )

        self.assertEqual(classification, "movement-detected")

    def test_classifies_non_neutral_center(self):
        classification = probe.classify_probe(
            _manifest(10.0),
            _manifest(0.5),
            _manifest(-10.0),
            {"leftCenterRmse": 0.01, "rightCenterRmse": 0.01},
        )

        self.assertEqual(classification, "center-not-neutral")

    def test_classifies_missing_offsets_and_no_image_delta_as_no_local_movement(self):
        classification = probe.classify_probe(
            _manifest(0.0),
            _manifest(0.0),
            _manifest(0.0),
            {"leftCenterRmse": 0.0, "rightCenterRmse": 0.0},
        )

        self.assertEqual(classification, "no-local-movement")

    def test_classifies_manifest_without_image_delta_as_review_needed(self):
        classification = probe.classify_probe(
            _manifest(10.0),
            _manifest(0.0),
            _manifest(-10.0),
            {"leftCenterRmse": 0.0, "rightCenterRmse": 0.0},
        )

        self.assertEqual(classification, "review-needed")

    def test_gate_all_criteria_passes(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(probe.gate_probe_results([_gate_summary()]), 0)

    def test_gate_large_expected_offsets_without_visible_translation_fails(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(
                probe.gate_probe_results(
                    [
                        _gate_summary(
                            left_offset=48.0,
                            right_offset=-48.0,
                            motion_classification="weak-motion",
                            left_region_dx=0,
                            right_region_dx=0,
                        )
                    ]
                ),
                3,
            )

    def test_gate_missing_anchor_inventory_fails(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(
                probe.gate_probe_results(
                    [
                        _gate_summary(
                            anchor_classification="anchor-inventory-missing",
                        )
                    ]
                ),
                3,
            )

    def test_gate_center_not_neutral_fails(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(
                probe.gate_probe_results([_gate_summary(center_offset=0.02)]),
                3,
            )

    def test_gate_same_direction_motion_fails(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(
                probe.gate_probe_results(
                    [
                        _gate_summary(
                            motion_classification="same-direction-motion",
                        )
                    ]
                ),
                3,
            )

    def test_gate_missing_motion_region_row_fails(self):
        summary = _gate_summary()
        del summary["motionRegions"]["wall"]

        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(probe.gate_probe_results([summary]), 3)

    def test_gate_unknown_motion_region_classification_fails(self):
        summary = _gate_summary()
        summary["motionRegions"]["wall"]["classification"] = ""

        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(probe.gate_probe_results([summary]), 3)


if __name__ == "__main__":
    unittest.main()
