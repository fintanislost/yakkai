import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

import arona_protected_puppet_lab as lab


class AronaProtectedPuppetLabTests(unittest.TestCase):
    def write_json(self, path: Path, payload: dict) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload), encoding="utf-8")

    def test_build_report_extracts_protected_probe_captures_and_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = root / "night" / "effect-captures" / "manifest.json"
            summary = root / "summary.json"
            self.write_json(
                manifest,
                {
                    "status": "ok",
                    "captures": [
                        {
                            "stage": "effect-input",
                            "path": str(root / "night" / "effect-input.tga"),
                            "renderTargetInfo": {"width": 512, "height": 512},
                            "layer": {
                                "layerId": 405,
                                "layerName": "ARONA_CROP_SHEET",
                                "alpha": 0.84,
                                "policy": {
                                    "keepEffects": False,
                                    "strippedEffects": True,
                                    "reason": "puppet-alpha-strip",
                                },
                                "candidateChainShape": "protected-puppet-mixed",
                                "candidateEffectClass": "none",
                                "effectNames": ["LUT Loader", "Pulse"],
                                "debugProbe": {
                                    "requested": True,
                                    "overrodePolicy": True,
                                    "reason": "layer-id-probe",
                                },
                            },
                        },
                        {
                            "stage": "effect-output",
                            "path": str(root / "night" / "effect-output.tga"),
                            "renderTargetInfo": {"width": 512, "height": 512},
                            "layer": {
                                "layerId": 405,
                                "layerName": "ARONA_CROP_SHEET",
                                "candidateChainShape": "protected-puppet-mixed",
                                "debugProbe": {
                                    "requested": True,
                                    "overrodePolicy": True,
                                    "reason": "layer-id-probe",
                                },
                            },
                        },
                    ],
                    "protectedPuppetDiagnostics": [
                        {
                            "layerId": 405,
                            "layerName": "ARONA_CROP_SHEET",
                            "captureMode": "metadata-only",
                            "diagnosticKind": "protected-puppet-chain",
                            "alphaEvidence": {"layerAlpha": 0.84},
                            "effectOrder": ["LUT Loader", "Pulse"],
                            "candidateChainShape": "protected-puppet-mixed",
                            "candidateEffectClass": "none",
                            "materialShaders": ["workshop/3165346237/effects/lut_loader"],
                        }
                    ],
                },
            )
            self.write_json(
                summary,
                {
                    "status": "review",
                    "outputDir": str(root),
                    "variants": [
                        {
                            "name": "night",
                            "status": "review",
                            "reference": str(root / "night" / "reference.png"),
                            "yakkai": str(root / "night" / "yakkai.png"),
                            "registeredYakkai": str(root / "night" / "yakkai.registered.png"),
                            "effectManifest": str(manifest),
                            "metrics": {"registeredRmse": 0.18},
                        }
                    ],
                },
            )

            result = lab.build_report(summary, layer_id=405)
            markdown = lab.format_markdown(result)

        self.assertEqual(result["variants"][0]["name"], "night")
        self.assertEqual(result["variants"][0]["probeCaptureCount"], 2)
        self.assertEqual(result["variants"][0]["probeStages"], ["effect-input", "effect-output"])
        self.assertEqual(result["variants"][0]["debugProbeReason"], "layer-id-probe")
        self.assertTrue(result["variants"][0]["debugProbeOverrodePolicy"])
        self.assertEqual(result["variants"][0]["candidateEffectClass"], "protected-puppet-lut")
        self.assertEqual(result["variants"][0]["protectedDiagnostic"]["captureMode"], "metadata-only")
        self.assertIn("night: status=review registeredRmse=0.18", markdown)
        self.assertIn("probe captures: 2 `effect-input, effect-output`", markdown)
        self.assertIn("protected diagnostic: metadata-only", markdown)

    def test_build_report_adds_alpha_and_color_stats_for_probe_images(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            input_image = root / "input.png"
            output_image = root / "output.png"
            Image.new("RGBA", (4, 4), (0, 0, 0, 0)).save(input_image)
            image = Image.new("RGBA", (4, 4), (0, 0, 0, 0))
            pixels = image.load()
            pixels[1, 1] = (100, 50, 25, 255)
            pixels[2, 1] = (100, 50, 25, 128)
            image.save(output_image)
            manifest = root / "manifest.json"
            summary = root / "summary.json"
            self.write_json(
                manifest,
                {
                    "status": "ok",
                    "captures": [
                        {
                            "stage": "effect-input",
                            "path": str(input_image),
                            "renderTargetInfo": {"width": 4, "height": 4},
                            "layer": {"layerId": 405},
                        },
                        {
                            "stage": "effect-output",
                            "path": str(output_image),
                            "renderTargetInfo": {"width": 4, "height": 4},
                            "layer": {"layerId": 405},
                        },
                    ],
                    "protectedPuppetDiagnostics": [
                        {"layerId": 405, "captureMode": "metadata-only"}
                    ],
                },
            )
            self.write_json(
                summary,
                {"variants": [{"name": "night", "effectManifest": str(manifest)}]},
            )

            result = lab.build_report(summary, layer_id=405)

        stats = result["variants"][0]["probeCaptures"][1]["imageStats"]
        self.assertEqual(stats["dimensions"], [4, 4])
        self.assertEqual(stats["alphaBoundsPixels"], [1, 1, 2, 1])
        self.assertAlmostEqual(stats["visibleFraction"], 0.125)
        self.assertAlmostEqual(stats["opaqueFraction"], 0.0625)
        self.assertEqual(stats["alphaWeightedMeanRgb"], [100.0, 50.0, 25.0])

    def test_build_report_compares_effect_input_to_output_boundary(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            input_image = root / "input.png"
            output_image = root / "output.png"
            Image.new("RGBA", (2, 2), (100, 50, 25, 255)).save(input_image)
            Image.new("RGBA", (2, 2), (150, 80, 25, 255)).save(output_image)
            manifest = root / "manifest.json"
            summary = root / "summary.json"
            self.write_json(
                manifest,
                {
                    "status": "ok",
                    "captures": [
                        {
                            "stage": "effect-input",
                            "path": str(input_image),
                            "renderTargetInfo": {"width": 2, "height": 2},
                            "layer": {"layerId": 405},
                        },
                        {
                            "stage": "effect-output",
                            "path": str(output_image),
                            "renderTargetInfo": {"width": 2, "height": 2},
                            "layer": {"layerId": 405},
                        },
                    ],
                },
            )
            self.write_json(
                summary,
                {"variants": [{"name": "night", "effectManifest": str(manifest)}]},
            )

            result = lab.build_report(summary, layer_id=405)

        comparison = result["variants"][0]["boundaryComparisons"][0]
        self.assertEqual(comparison["boundary"], "effect-input->effect-output")
        self.assertEqual(comparison["classification"], "color-drift")
        self.assertGreater(comparison["rmse"], 0.1)

    def test_build_report_classifies_effect_output_to_final_publish_as_composition_boundary(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            effect_output = root / "effect-output.png"
            final_publish = root / "final-publish.png"
            Image.new("RGBA", (4, 4), (100, 50, 25, 128)).save(effect_output)
            Image.new("RGBA", (2, 2), (110, 60, 25, 255)).save(final_publish)
            manifest = root / "manifest.json"
            summary = root / "summary.json"
            self.write_json(
                manifest,
                {
                    "status": "ok",
                    "captures": [
                        {
                            "stage": "effect-output",
                            "path": str(effect_output),
                            "renderTargetInfo": {"width": 4, "height": 4},
                            "layer": {"layerId": 405},
                        },
                        {
                            "stage": "final-publish",
                            "path": str(final_publish),
                            "renderTargetInfo": {"width": 2, "height": 2},
                            "layer": {"layerId": 405},
                        },
                    ],
                },
            )
            self.write_json(
                summary,
                {"variants": [{"name": "night", "effectManifest": str(manifest)}]},
            )

            result = lab.build_report(summary, layer_id=405)

        comparison = result["variants"][0]["boundaryComparisons"][0]
        self.assertEqual(comparison["boundary"], "effect-output->final-publish")
        self.assertEqual(comparison["classification"], "local-to-frame-composition")

    def test_build_report_classifies_final_coverage_loss_for_active_slots(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            effect_input = root / "effect-input.png"
            effect_output = root / "effect-output.png"
            final_publish = root / "final-publish.png"
            input_image = Image.new("RGBA", (4, 4), (0, 0, 0, 0))
            draw = ImageDraw.Draw(input_image)
            draw.rectangle((1, 1, 2, 2), fill=(100, 50, 25, 255))
            input_image.save(effect_input)
            input_image.save(effect_output)
            final_image = Image.new("RGBA", (4, 4), (0, 0, 0, 0))
            final_image.putpixel((1, 1), (100, 50, 25, 255))
            final_image.save(final_publish)
            manifest = root / "manifest.json"
            summary = root / "summary.json"
            layer = {
                "layerId": 405,
                "publish": {
                    "puppetCutoutSlotCoverage": [
                        {"slot": 2, "active": True, "vertexCount": 157, "triangleCount": 294},
                        {"slot": 4, "active": False, "vertexCount": 22, "triangleCount": 10},
                        {
                            "slot": 13,
                            "active": False,
                            "primaryVertexCount": 0,
                            "primaryTriangleCount": 0,
                            "weightedVertexCount": 409,
                            "weightedTriangleCount": 786,
                            "secondaryOnly": True,
                        },
                    ]
                },
            }
            self.write_json(
                manifest,
                {
                    "captures": [
                        {
                            "stage": "effect-input",
                            "path": str(effect_input),
                            "renderTargetInfo": {"width": 4, "height": 4},
                            "layer": layer,
                        },
                        {
                            "stage": "effect-output",
                            "path": str(effect_output),
                            "renderTargetInfo": {"width": 4, "height": 4},
                            "layer": layer,
                        },
                        {
                            "stage": "final-publish",
                            "path": str(final_publish),
                            "renderTargetInfo": {"width": 4, "height": 4},
                            "layer": layer,
                        },
                    ],
                },
            )
            self.write_json(
                summary,
                {"variants": [{"name": "day", "effectManifest": str(manifest)}]},
            )

            result = lab.build_report(summary, layer_id=405)
            markdown = lab.format_markdown(result)

        coverage = result["variants"][0]["finalCoverageDiagnostics"]
        self.assertEqual(coverage["classification"], "final-coverage-loss")
        self.assertEqual(coverage["activeSlots"], ["2"])
        self.assertEqual(coverage["secondaryOnlySlots"], ["13"])
        self.assertAlmostEqual(coverage["inputVisibleFraction"], 0.25)
        self.assertAlmostEqual(coverage["outputVisibleFraction"], 0.25)
        self.assertAlmostEqual(coverage["finalVisibleFraction"], 0.0625)
        self.assertAlmostEqual(coverage["finalToOutputAlphaRatio"], 0.25)
        self.assertIn("final coverage: class=`final-coverage-loss`", markdown)
        self.assertIn("secondaryOnlySlots=`[13]`", markdown)

    def test_build_report_prefers_isolated_final_display_boundary_when_available(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            effect_input = root / "effect-input.png"
            effect_output = root / "effect-output.png"
            final_before = root / "final-display-before.png"
            final_after = root / "final-display-after.png"
            final_publish = root / "final-publish.png"

            layer_image = Image.new("RGBA", (4, 4), (0, 0, 0, 0))
            draw = ImageDraw.Draw(layer_image)
            draw.rectangle((1, 1, 2, 2), fill=(100, 50, 25, 255))
            layer_image.save(effect_input)
            layer_image.save(effect_output)

            before_image = Image.new("RGBA", (4, 4), (20, 20, 20, 255))
            before_image.save(final_before)
            after_image = before_image.copy()
            draw = ImageDraw.Draw(after_image)
            draw.rectangle((1, 1, 2, 2), fill=(100, 50, 25, 255))
            after_image.save(final_after)
            Image.new("RGBA", (4, 4), (20, 20, 20, 255)).save(final_publish)

            manifest = root / "manifest.json"
            summary = root / "summary.json"
            layer = {
                "layerId": 405,
                "publish": {
                    "finalDisplayBoundaryCaptureTiming": "render-graph-copy-around-final-display-node",
                    "finalDisplayBeforeRenderTarget": "_rt_debug_final_display_before_node",
                    "finalDisplayAfterRenderTarget": "_rt_debug_final_display_after_node",
                    "puppetCutoutSlotCoverage": [
                        {"slot": 2, "active": True, "vertexCount": 157, "triangleCount": 294},
                    ],
                },
            }
            self.write_json(
                manifest,
                {
                    "captures": [
                        {
                            "stage": "effect-input",
                            "path": str(effect_input),
                            "renderTargetInfo": {"width": 4, "height": 4},
                            "layer": layer,
                        },
                        {
                            "stage": "effect-output",
                            "path": str(effect_output),
                            "renderTargetInfo": {"width": 4, "height": 4},
                            "layer": layer,
                        },
                        {
                            "stage": "final-display-before",
                            "path": str(final_before),
                            "renderTargetInfo": {"width": 4, "height": 4},
                            "layer": layer,
                        },
                        {
                            "stage": "final-display-after",
                            "path": str(final_after),
                            "renderTargetInfo": {"width": 4, "height": 4},
                            "layer": layer,
                        },
                        {
                            "stage": "final-publish",
                            "path": str(final_publish),
                            "renderTargetInfo": {"width": 4, "height": 4},
                            "layer": layer,
                        },
                    ],
                },
            )
            self.write_json(
                summary,
                {"variants": [{"name": "day", "effectManifest": str(manifest)}]},
            )

            result = lab.build_report(summary, layer_id=405)
            markdown = lab.format_markdown(result)

        coverage = result["variants"][0]["finalCoverageDiagnostics"]
        self.assertEqual(coverage["source"], "final-display-boundary")
        self.assertEqual(coverage["classification"], "final-display-boundary-present")
        self.assertEqual(coverage["activeSlots"], ["2"])
        self.assertAlmostEqual(coverage["finalDisplayDeltaVisibleFraction"], 0.25)
        alignment = coverage["finalDisplayAlignment"]
        self.assertEqual(alignment["classification"], "final-display-aligned")
        self.assertAlmostEqual(alignment["deltaToOutputVisibleRatio"], 1.0)
        self.assertAlmostEqual(alignment["boundsIou"], 1.0)
        self.assertAlmostEqual(alignment["centroidDrift"], 0.0)
        self.assertEqual(coverage["finalDisplayBoundaryTiming"], "render-graph-copy-around-final-display-node")
        self.assertIn("final-display-before->final-display-after", [
            item["boundary"] for item in result["variants"][0]["boundaryComparisons"]
        ])
        self.assertIn("final coverage: class=`final-display-boundary-present` source=`final-display-boundary`", markdown)
        self.assertIn("alignment=`final-display-aligned`", markdown)

    def test_build_report_flags_final_display_delta_shape_drift(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            effect_output = root / "effect-output.png"
            final_before = root / "final-display-before.png"
            final_after = root / "final-display-after.png"
            manifest = root / "manifest.json"
            summary = root / "summary.json"

            output_image = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
            draw = ImageDraw.Draw(output_image)
            draw.rectangle((2, 2, 5, 5), fill=(100, 50, 25, 255))
            output_image.save(effect_output)

            before_image = Image.new("RGBA", (8, 8), (20, 20, 20, 255))
            before_image.save(final_before)
            after_image = before_image.copy()
            draw = ImageDraw.Draw(after_image)
            draw.rectangle((1, 0, 6, 6), fill=(100, 50, 25, 255))
            after_image.save(final_after)

            layer = {
                "layerId": 405,
                "publish": {
                    "finalDisplayBoundaryCaptureTiming": "render-graph-copy-around-final-display-node",
                    "puppetCutoutSlotCoverage": [
                        {"slot": 2, "active": True, "vertexCount": 157, "triangleCount": 294},
                    ],
                },
            }
            self.write_json(
                manifest,
                {
                    "captures": [
                        {
                            "stage": "effect-output",
                            "path": str(effect_output),
                            "renderTargetInfo": {"width": 8, "height": 8},
                            "layer": layer,
                        },
                        {
                            "stage": "final-display-before",
                            "path": str(final_before),
                            "renderTargetInfo": {"width": 8, "height": 8},
                            "layer": layer,
                        },
                        {
                            "stage": "final-display-after",
                            "path": str(final_after),
                            "renderTargetInfo": {"width": 8, "height": 8},
                            "layer": layer,
                        },
                    ],
                },
            )
            self.write_json(
                summary,
                {"variants": [{"name": "day", "effectManifest": str(manifest)}]},
            )

            result = lab.build_report(summary, layer_id=405)

        alignment = result["variants"][0]["finalCoverageDiagnostics"]["finalDisplayAlignment"]
        self.assertEqual(alignment["classification"], "final-display-shape-drift")
        self.assertGreater(alignment["deltaToOutputVisibleRatio"], 2.0)
        self.assertLess(alignment["boundsIou"], 0.5)
        projection = result["variants"][0]["finalCoverageDiagnostics"]["finalDisplayScreenSpaceProjection"]
        self.assertEqual(projection["classification"], "screen-space-affine-consistent")
        self.assertLess(projection["projectedCentroidDrift"], 0.03)

    def test_build_report_flags_threshold_sensitive_final_display_drift(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            effect_output = root / "effect-output.png"
            final_before = root / "final-display-before.png"
            final_after = root / "final-display-after.png"
            manifest = root / "manifest.json"
            summary = root / "summary.json"

            output_image = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
            draw = ImageDraw.Draw(output_image)
            draw.rectangle((2, 2, 5, 5), fill=(100, 50, 25, 255))
            output_image.save(effect_output)

            before_image = Image.new("RGBA", (8, 8), (20, 20, 20, 255))
            before_image.save(final_before)
            after_image = before_image.copy()
            draw = ImageDraw.Draw(after_image)
            draw.rectangle((1, 0, 6, 6), fill=(22, 22, 22, 255))
            draw.rectangle((2, 2, 5, 5), fill=(100, 50, 25, 255))
            after_image.save(final_after)

            layer = {
                "layerId": 405,
                "publish": {
                    "finalDisplayBoundaryCaptureTiming": "render-graph-copy-around-final-display-node",
                    "puppetCutoutSlotCoverage": [
                        {"slot": 2, "active": True, "vertexCount": 157, "triangleCount": 294},
                    ],
                },
            }
            self.write_json(
                manifest,
                {
                    "captures": [
                        {
                            "stage": "effect-output",
                            "path": str(effect_output),
                            "renderTargetInfo": {"width": 8, "height": 8},
                            "layer": layer,
                        },
                        {
                            "stage": "final-display-before",
                            "path": str(final_before),
                            "renderTargetInfo": {"width": 8, "height": 8},
                            "layer": layer,
                        },
                        {
                            "stage": "final-display-after",
                            "path": str(final_after),
                            "renderTargetInfo": {"width": 8, "height": 8},
                            "layer": layer,
                        },
                    ],
                },
            )
            self.write_json(
                summary,
                {"variants": [{"name": "day", "effectManifest": str(manifest)}]},
            )

            result = lab.build_report(summary, layer_id=405)
            markdown = lab.format_markdown(result)

        coverage = result["variants"][0]["finalCoverageDiagnostics"]
        sensitivity = coverage["finalDisplayThresholdSensitivity"]
        self.assertEqual(sensitivity["classification"], "threshold-sensitive-shape-drift")
        self.assertIn(1, sensitivity["shapeDriftThresholdRgb255"])
        self.assertIn(4, sensitivity["alignedThresholdRgb255"])
        sweep = coverage["finalDisplayDeltaThresholdSweep"]
        self.assertEqual(sweep[0]["thresholdRgb255"], 1)
        self.assertEqual(sweep[0]["alignment"]["classification"], "final-display-shape-drift")
        self.assertEqual(sweep[1]["thresholdRgb255"], 4)
        self.assertEqual(sweep[1]["alignment"]["classification"], "final-display-aligned")
        self.assertIn("thresholds=`threshold-sensitive-shape-drift`", markdown)

    def test_build_report_compares_probe_final_to_normal_yakkai_when_available(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            probe_final = root / "probe-final.png"
            normal_final = root / "normal-final.png"
            Image.new("RGBA", (2, 2), (100, 100, 100, 255)).save(probe_final)
            Image.new("RGBA", (2, 2), (110, 100, 100, 255)).save(normal_final)
            manifest = root / "manifest.json"
            probe_summary = root / "probe-summary.json"
            normal_summary = root / "normal-summary.json"
            self.write_json(
                manifest,
                {
                    "captures": [
                        {
                            "stage": "final-publish",
                            "path": str(probe_final),
                            "renderTargetInfo": {"width": 2, "height": 2},
                            "layer": {"layerId": 405},
                        }
                    ]
                },
            )
            self.write_json(
                probe_summary,
                {"variants": [{"name": "night", "effectManifest": str(manifest)}]},
            )
            self.write_json(
                normal_summary,
                {"variants": [{"name": "night", "yakkai": str(normal_final)}]},
            )

            result = lab.build_report(
                probe_summary,
                layer_id=405,
                normal_summary_path=normal_summary,
            )

        self.assertEqual(result["variants"][0]["normalYakkai"], str(normal_final))
        self.assertGreater(result["variants"][0]["probeFinalToNormal"]["rmse"], 0.0)

    def test_build_report_flags_screen_space_shift_between_probe_and_normal(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            probe_final = root / "probe-final.png"
            normal_final = root / "normal-final.png"
            normal = Image.new("RGBA", (128, 64), (20, 40, 80, 255))
            draw = ImageDraw.Draw(normal)
            draw.rectangle((70, 18, 116, 54), fill=(220, 230, 240, 255))
            normal.save(normal_final)
            probe = Image.new("RGBA", (128, 64), (20, 40, 80, 255))
            draw = ImageDraw.Draw(probe)
            draw.rectangle((12, 18, 58, 54), fill=(220, 230, 240, 255))
            probe.save(probe_final)
            manifest = root / "manifest.json"
            probe_summary = root / "probe-summary.json"
            normal_summary = root / "normal-summary.json"
            self.write_json(
                manifest,
                {
                    "captures": [
                        {
                            "stage": "final-publish",
                            "path": str(probe_final),
                            "renderTargetInfo": {"width": 128, "height": 64},
                            "layer": {"layerId": 405},
                        }
                    ]
                },
            )
            self.write_json(
                probe_summary,
                {"variants": [{"name": "day", "effectManifest": str(manifest)}]},
            )
            self.write_json(
                normal_summary,
                {"variants": [{"name": "day", "yakkai": str(normal_final)}]},
            )

            result = lab.build_report(
                probe_summary,
                layer_id=405,
                normal_summary_path=normal_summary,
            )

        drift = result["variants"][0]["screenSpaceDrift"]
        self.assertEqual(drift["classification"], "screen-space-drift")
        self.assertLess(drift["centroidDeltaPixels"][0], -40.0)
        self.assertGreater(drift["driftMagnitudePixels"], 40.0)

    def test_build_report_summarizes_generic_puppet_effect_route_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            final_publish = root / "probe-final.png"
            Image.new("RGBA", (2, 2), (100, 100, 100, 255)).save(final_publish)
            manifest = root / "manifest.json"
            summary = root / "summary.json"
            self.write_json(
                manifest,
                {
                    "captures": [
                        {
                            "stage": "final-publish",
                            "path": str(final_publish),
                            "renderTargetInfo": {"width": 2, "height": 2},
                            "layer": {
                                "layerId": 405,
                                "publish": {
                                    "puppetLayer": True,
                                    "standalonePuppetFinalDisplay": True,
                                    "publishFinalOutput": False,
                                    "effectInputMeshKind": "puppet-skinned-mesh",
                                    "effectFinalMeshKind": "flat-card",
                                    "standaloneFinalMeshKind": "flat-card",
                                    "finalDisplayRoute": "standalone-puppet-final-display",
                                    "routeRisk": "puppet-effect-output-displayed-as-flat-card",
                                },
                            },
                        }
                    ]
                },
            )
            self.write_json(
                summary,
                {"variants": [{"name": "day", "effectManifest": str(manifest)}]},
            )

            result = lab.build_report(summary, layer_id=405)
            markdown = lab.format_markdown(result)

        route = result["variants"][0]["routeDiagnostics"]
        self.assertEqual(route["classification"], "flat-card-puppet-effect-route")
        self.assertEqual(route["routeRisk"], "puppet-effect-output-displayed-as-flat-card")
        self.assertIn("route: class=`flat-card-puppet-effect-route`", markdown)

    def test_build_report_summarizes_final_publish_composition_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = root / "manifest.json"
            summary = root / "summary.json"
            self.write_json(
                manifest,
                {
                    "captures": [
                        {
                            "stage": "final-publish",
                            "path": str(root / "final-publish.png"),
                            "renderTargetInfo": {"width": 2, "height": 2},
                            "layer": {
                                "layerId": 405,
                                "publish": {
                                    "parentId": 44,
                                    "puppetLayer": True,
                                    "standalonePuppetFinalDisplay": True,
                                    "publishFinalOutput": False,
                                    "effectInputMeshKind": "puppet-skinned-mesh",
                                    "standaloneFinalMeshKind": "layer-card",
                                    "finalDisplayRoute": "standalone-puppet-final-display",
                                    "routeRisk": "",
                                    "effectInputLocalTransform": {
                                        "origin": [0.0, 0.0, 0.0],
                                        "scale": [1.0, 1.0, 1.0],
                                        "angles": [0.0, 0.0, 0.0],
                                    },
                                    "standaloneDisplayLocalTransform": {
                                        "origin": [12.0, 24.0, 0.0],
                                        "scale": [1.0, 1.0, 1.0],
                                        "angles": [0.0, 0.0, 0.0],
                                    },
                                    "standaloneDisplayParentId": 44,
                                    "standaloneDisplayHasParsedParentNode": True,
                                    "standaloneDisplayNodeOrdinal": 1,
                                    "standaloneFinalMaterialBlendMode": 1,
                                    "standaloneFinalTexture": "_rt_effect_ppong_b",
                                    "effectInputMeshBounds": {
                                        "vertexArrayCount": 1,
                                        "vertexCount": 120,
                                        "positionMin": [-2080.0, -1461.5, 0.0],
                                        "positionMax": [2080.0, 1461.5, 0.0],
                                    },
                                    "standaloneFinalMeshBounds": {
                                        "vertexArrayCount": 1,
                                        "vertexCount": 4,
                                        "positionMin": [-2080.0, -1461.5, 0.0],
                                        "positionMax": [2080.0, 1461.5, 0.0],
                                    },
                                },
                            },
                        }
                    ]
                },
            )
            self.write_json(
                summary,
                {"variants": [{"name": "day", "effectManifest": str(manifest)}]},
            )

            result = lab.build_report(summary, layer_id=405)
            markdown = lab.format_markdown(result)

        composition = result["variants"][0]["compositionDiagnostics"]
        self.assertEqual(composition["classification"], "composition-metadata-present")
        self.assertEqual(composition["standaloneDisplayNodeOrdinal"], 1)
        self.assertEqual(composition["standaloneFinalTexture"], "_rt_effect_ppong_b")
        self.assertEqual(composition["standaloneFinalMeshBounds"]["vertexCount"], 4)
        self.assertIn("composition: class=`composition-metadata-present`", markdown)

    def test_build_report_recovers_failed_variant_manifest_from_output_dir(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = root / "sunset" / "effect-captures" / "manifest.json"
            summary = root / "summary.json"
            self.write_json(
                manifest,
                {
                    "status": "ok",
                    "captures": [
                        {
                            "stage": "final-publish",
                            "path": str(root / "sunset" / "final-publish.tga"),
                            "renderTargetInfo": {"width": 1280, "height": 720},
                            "layer": {
                                "layerId": 405,
                                "publish": {
                                    "puppetLayer": True,
                                    "standalonePuppetFinalDisplay": True,
                                    "publishFinalOutput": False,
                                    "standaloneFinalTexture": "_rt_effect_ppong_b",
                                    "standaloneDisplayParentId": 328,
                                    "parentId": 328,
                                    "standaloneFinalMeshBounds": {"vertexCount": 4},
                                },
                            },
                        }
                    ],
                },
            )
            self.write_json(
                summary,
                {
                    "outputDir": str(root),
                    "variants": [
                        {
                            "name": "sunset",
                            "status": "fail",
                            "effectManifest": None,
                        }
                    ],
                },
            )

            result = lab.build_report(summary, layer_id=405)

        variant = result["variants"][0]
        self.assertEqual(variant["manifestStatus"], "ok")
        self.assertEqual(variant["effectManifest"], str(manifest))
        self.assertEqual(variant["probeCaptureCount"], 1)


if __name__ == "__main__":
    unittest.main()
