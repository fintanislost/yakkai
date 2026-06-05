import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import arona_lut_parity_report as report


class AronaLutParityReportTests(unittest.TestCase):
    def write_json(self, path: Path, payload: dict) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload), encoding="utf-8")

    def test_build_report_ranks_highest_rmse_and_extracts_lut_layers(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            day_manifest = root / "day" / "effect-captures" / "manifest.json"
            night_manifest = root / "night" / "effect-captures" / "manifest.json"
            summary = root / "summary.json"
            self.write_json(
                day_manifest,
                {
                    "status": "ok",
                    "captures": [
                        {
                            "stage": "effect-input",
                            "layer": {
                                "layerId": 12,
                                "layerName": "SKY",
                                "keepEffects": True,
                                "policyReason": "plain-background",
                                "candidateChainShape": "none",
                                "candidateMixFamilies": ["water"],
                            },
                        }
                    ],
                    "strippedCandidates": [],
                },
            )
            self.write_json(
                night_manifest,
                {
                    "status": "ok",
                    "captures": [
                        {
                            "stage": "effect-input",
                            "layer": {
                                "layerId": 82,
                                "layerName": "WALL",
                                "keepEffects": True,
                                "strippedEffects": False,
                                "policyReason": "lut-only-effect",
                                "candidateChainShape": "lut-only",
                                "candidateEffectClass": "regular-lut-only",
                                "candidateMixFamilies": ["lut"],
                                "materialShaders": ["workshop/3165346237/effects/lut_loader"],
                            },
                        }
                    ],
                    "strippedCandidates": [
                        {
                            "layerId": 405,
                            "layerName": "ARONA_CROP_SHEET",
                            "keepEffects": False,
                            "strippedEffects": True,
                            "reason": "puppet-alpha-strip",
                            "candidateChainShape": "protected-puppet-mixed",
                            "candidateEffectClass": "protected-puppet-lut",
                            "candidateMixFamilies": ["lut", "pulse", "shake"],
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
                            "name": "day",
                            "status": "review",
                            "metrics": {
                                "rmse": 0.077,
                                "referenceMeanRgb": [0.6, 0.6, 0.7],
                                "yakkaiMeanRgb": [0.61, 0.60, 0.69],
                            },
                        },
                        {
                            "name": "night",
                            "status": "review",
                            "effectManifest": str(night_manifest),
                            "metrics": {
                                "rmse": 0.188,
                                "referenceMeanRgb": [0.2, 0.4, 0.6],
                                "yakkaiMeanRgb": [0.4, 0.5, 0.7],
                            },
                        },
                    ],
                },
            )

            result = report.build_report(summary)
            markdown = report.format_markdown(result)

        self.assertEqual(result["highestDrift"]["name"], "night")
        self.assertEqual(result["variants"][0]["manifest"], str(day_manifest))
        self.assertEqual(result["variants"][1]["lutLayerCount"], 2)
        self.assertEqual(
            result["variants"][1]["lutClassCounts"],
            {"protected-puppet-lut": 1, "regular-lut-only": 1},
        )
        self.assertEqual(
            result["variants"][1]["lutDispositionCounts"],
            {"allowed": 1, "protected": 1},
        )
        self.assertAlmostEqual(result["variants"][1]["rgbDelta"][0], 0.2)
        self.assertIn("highest drift: night", markdown)
        self.assertIn("rgbDelta=[+0.2000, +0.1000, +0.1000]", markdown)
        self.assertIn("class counts: `protected-puppet-lut=1, regular-lut-only=1`", markdown)
        self.assertIn("disposition counts: `allowed=1, protected=1`", markdown)
        self.assertIn("82 WALL kept allowed regular-lut-only lut-only-effect lut-only", markdown)
        self.assertIn("405 ARONA_CROP_SHEET stripped protected protected-puppet-lut puppet-alpha-strip protected-puppet-mixed", markdown)

    def test_missing_manifest_is_reported_and_causes_exit_one(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            summary = root / "summary.json"
            self.write_json(
                summary,
                {
                    "status": "review",
                    "outputDir": str(root),
                    "variants": [
                        {
                            "name": "day",
                            "status": "review",
                            "effectManifest": str(root / "missing.json"),
                            "metrics": {"rmse": 0.1},
                        }
                    ],
                },
            )

            result = report.build_report(summary)
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                exit_code = report.main([str(summary)])

        self.assertEqual(result["variants"][0]["manifestStatus"], "missing")
        self.assertEqual(result["variants"][0]["lutLayerCount"], 0)
        self.assertEqual(exit_code, 1)
        self.assertIn("missing", stdout.getvalue())

    def test_final_layers_does_not_count_as_captured_kept_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = root / "day" / "effect-captures" / "manifest.json"
            summary = root / "summary.json"
            self.write_json(
                manifest,
                {
                    "status": "ok",
                    "finalLayers": [
                        {
                            "layerId": 82,
                            "layerName": "WALL",
                            "policyReason": "stale-lut-layer",
                            "candidateChainShape": "lut-only",
                            "candidateMixFamilies": ["lut"],
                        }
                    ],
                    "layers": [
                        {
                            "layerId": 83,
                            "layerName": "SKY",
                            "policyReason": "stale-layer-list",
                            "candidateChainShape": "lut-only",
                            "candidateMixFamilies": ["lut"],
                        }
                    ],
                    "strippedCandidates": [
                        {
                            "layerId": 405,
                            "layerName": "ARONA_CROP_SHEET",
                            "reason": "puppet-alpha-strip",
                            "candidateChainShape": "protected-puppet-mixed",
                            "candidateMixFamilies": ["lut", "pulse"],
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
                            "name": "day",
                            "status": "review",
                            "metrics": {"rmse": 0.1},
                        }
                    ],
                },
            )

            result = report.build_report(summary)
            markdown = report.format_markdown(result)

        layers = result["variants"][0]["lutLayers"]
        self.assertEqual([layer["id"] for layer in layers], [405])
        self.assertEqual(result["variants"][0]["lutLayerCount"], 1)

    def test_preserved_layers_are_read_from_real_capture_records_once(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = root / "night" / "effect-captures" / "manifest.json"
            summary = root / "summary.json"
            capture_layer = {
                "layerId": 82,
                "layerName": "WALL",
                "layerImage": "models/WALL.json",
                "visibleEffectCount": 1,
                "effectNames": ["LUT Loader", "LUT Loader"],
                "policy": {"reason": "lut-only-effect", "strippedEffects": False},
                "candidateChainShape": "lut-only",
                "candidateEffectClass": "regular-lut-only",
                "candidateMixFamilies": ["lut"],
                "materialShaders": [
                    "workshop/3165346237/effects/lut_loader",
                    "workshop/3165346237/effects/lut_loader",
                ],
                "effectMaterials": [
                    {
                        "effectIndex": 1,
                        "materialIndex": 0,
                        "shader": "workshop/3165346237/effects/lut_loader",
                        "authoredTextures": ["effects/lut/night.png"],
                        "resolvedTextures": ["effects/lut/night.png"],
                        "textureBindings": [
                            {
                                "slot": 0,
                                "authored": "effects/lut/night.png",
                                "resolved": "effects/lut/night.png",
                            }
                        ],
                        "authoredCombos": {"LUTMODE": "1"},
                        "resolvedCombos": {"LUTMODE": "1"},
                        "materialValues": {"strength": [0.75]},
                        "resolvedConstValues": {
                            "g_Texture0Resolution": [256.0, 16.0, 256.0, 16.0]
                        },
                        "defines": ["g_Texture0"],
                    }
                ],
            }
            self.write_json(
                manifest,
                {
                    "status": "ok",
                    "captures": [
                        {
                            "stage": "effect-input",
                            "path": "/tmp/night/82_WALL/effect_input.tga",
                            "renderTarget": "_rt_debug_effect_input_82",
                            "renderTargetInfo": {"width": 4160, "height": 2923, "format": "RGBA8"},
                            "layer": capture_layer,
                        },
                        {
                            "stage": "effect-output",
                            "path": "/tmp/night/82_WALL/effect_output.tga",
                            "renderTarget": "_rt_debug_effect_output_82",
                            "renderTargetInfo": {"width": 1280, "height": 720, "format": "RGBA8"},
                            "layer": dict(capture_layer),
                        },
                        {
                            "stage": "final-publish",
                            "path": "/tmp/night/82_WALL/final_publish.tga",
                            "renderTarget": "_rt_default",
                            "renderTargetInfo": {"width": 1280, "height": 720, "format": "RGBA8"},
                            "layer": dict(capture_layer),
                        },
                    ],
                    "strippedCandidates": [],
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
                            "metrics": {"rmse": 0.2},
                        }
                    ],
                },
            )

            result = report.build_report(summary)

        markdown = report.format_markdown(result)
        layers = result["variants"][0]["lutLayers"]
        self.assertEqual(len(layers), 1)
        self.assertEqual(layers[0]["id"], 82)
        self.assertEqual(layers[0]["state"], "kept")
        self.assertEqual(layers[0]["class"], "regular-lut-only")
        self.assertEqual(layers[0]["disposition"], "allowed")
        self.assertEqual(layers[0]["reason"], "lut-only-effect")
        self.assertEqual(layers[0]["layerImage"], "models/WALL.json")
        self.assertEqual(layers[0]["visibleEffectCount"], 1)
        self.assertEqual(layers[0]["effectNames"], ["LUT Loader"])
        self.assertEqual(layers[0]["materialShaders"], ["workshop/3165346237/effects/lut_loader"])
        self.assertEqual(
            [stage["stage"] for stage in layers[0]["captureStages"]],
            ["effect-input", "effect-output", "final-publish"],
        )
        self.assertEqual(layers[0]["captureStages"][0]["width"], 4160)
        self.assertEqual(layers[0]["captureStages"][0]["height"], 2923)
        self.assertEqual(layers[0]["captureStages"][0]["format"], "RGBA8")
        self.assertEqual(layers[0]["effectMaterials"][0]["effectIndex"], 1)
        self.assertEqual(layers[0]["effectMaterials"][0]["materialIndex"], 0)
        self.assertEqual(layers[0]["effectMaterials"][0]["resolvedTextures"], ["effects/lut/night.png"])
        self.assertEqual(layers[0]["effectMaterials"][0]["materialValues"], {"strength": [0.75]})
        self.assertEqual(
            layers[0]["effectMaterials"][0]["resolvedConstValues"],
            {"g_Texture0Resolution": [256.0, 16.0, 256.0, 16.0]},
        )
        self.assertEqual(layers[0]["missingEvidence"], [])
        self.assertIn("image=models/WALL.json", markdown)
        self.assertIn("shaders=workshop/3165346237/effects/lut_loader", markdown)
        self.assertIn("effect-input 4160x2923 RGBA8", markdown)
        self.assertIn("material[1:0] shader=workshop/3165346237/effects/lut_loader", markdown)
        self.assertIn("textures[0] authored=effects/lut/night.png resolved=effects/lut/night.png", markdown)
        self.assertIn("materialValues=strength=[0.75]", markdown)
        self.assertIn("constValues=g_Texture0Resolution=[256.0, 16.0, 256.0, 16.0]", markdown)
        self.assertNotIn("missingEvidence=", markdown)

    def test_capture_records_without_layer_are_not_kept_evidence(self):
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
                            "layerId": 82,
                            "layerName": "WALL",
                            "candidateChainShape": "lut-only",
                            "candidateMixFamilies": ["lut"],
                        }
                    ],
                    "strippedCandidates": [],
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
                            "metrics": {"rmse": 0.2},
                        }
                    ],
                },
            )

            result = report.build_report(summary)

        self.assertEqual(result["variants"][0]["lutLayerCount"], 0)
        self.assertEqual(result["variants"][0]["lutLayers"], [])

    def test_json_output_emits_machine_readable_report(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = root / "day" / "effect-captures" / "manifest.json"
            summary = root / "summary.json"
            self.write_json(
                manifest,
                {
                    "status": "ok",
                    "captures": [
                        {
                            "stage": "effect-input",
                            "layer": {
                                "layerId": 9,
                                "layerName": "COLOR_PASS",
                                "keepEffects": True,
                                "policy": {"reason": "color-grade-preserved"},
                                "candidateChainShape": "color_grading-only",
                                "effectNames": ["colorgrading"],
                            },
                        }
                    ],
                    "strippedCandidates": [],
                },
            )
            self.write_json(
                summary,
                {
                    "status": "review",
                    "outputDir": str(root),
                    "variants": [
                        {
                            "name": "day",
                            "status": "review",
                            "metrics": {
                                "rmse": 0.02,
                                "referenceMeanRgb": [0.1, 0.2, 0.3],
                                "yakkaiMeanRgb": [0.2, 0.3, 0.4],
                            },
                        }
                    ],
                },
            )
            stdout = io.StringIO()

            with contextlib.redirect_stdout(stdout):
                exit_code = report.main([str(summary), "--json"])

        self.assertEqual(exit_code, 0)
        data = json.loads(stdout.getvalue())
        self.assertEqual(data["variants"][0]["lutLayerCount"], 1)
        self.assertEqual(data["variants"][0]["lutLayers"][0]["reason"], "color-grade-preserved")


if __name__ == "__main__":
    unittest.main()
