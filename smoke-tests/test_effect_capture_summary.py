import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SUMMARY = REPO_ROOT / "tools" / "effect-capture-summary.py"


class EffectCaptureSummaryTests(unittest.TestCase):
    def run_summary(self, manifest):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")

            return subprocess.run(
                [sys.executable, str(SUMMARY), str(path)],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

    def test_reports_stripped_candidates_compactly(self):
        manifest = {
            "sceneId": "3476236738",
            "captures": [],
            "passStates": [],
            "strippedCandidates": [
                {
                    "sceneId": "3476236738",
                    "sceneType": "Puppet",
                    "layerId": 101,
                    "layerName": "Water BG",
                    "layerImage": "materials/water.png",
                    "visibleEffectCount": 2,
                    "alpha": 1.0,
                    "policy": {
                        "keepLayer": True,
                        "keepEffects": False,
                        "strippedEffects": True,
                        "forceAlphaOne": False,
                        "reason": "puppet-alpha-strip",
                    },
                    "effectNames": ["waterwaves", "opacity"],
                    "materialShaders": ["effects/waterwaves", "effects/opacity"],
                },
                {
                    "sceneId": "3476236738",
                    "sceneType": "Puppet",
                    "layerId": 102,
                    "layerName": "Utility",
                    "layerImage": "models/util/solidlayer.json",
                    "visibleEffectCount": 1,
                    "alpha": 1.0,
                    "policy": {
                        "keepLayer": False,
                        "keepEffects": False,
                        "strippedEffects": True,
                        "forceAlphaOne": False,
                        "reason": "puppet-alpha-strip",
                    },
                    "effectNames": ["blur"],
                    "materialShaders": ["effects/blur"],
                },
            ],
        }

        completed = self.run_summary(manifest)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("strippedCandidates=2", completed.stdout)
        self.assertIn("stripped-candidate-reasons:", completed.stdout)
        self.assertIn("  - puppet-alpha-strip: 2", completed.stdout)
        self.assertIn("stripped-candidate-layers:", completed.stdout)
        self.assertIn("  - 101:Water BG reason=puppet-alpha-strip risk=unknown shape=unknown families=none mix=none effects=2 shaders=2", completed.stdout)
        self.assertIn("  - 102:Utility reason=puppet-alpha-strip risk=unknown shape=unknown families=none mix=none effects=1 shaders=1", completed.stdout)

    def test_reports_candidate_classification_buckets(self):
        manifest = {
            "sceneId": "3476236738",
            "captures": [],
            "passStates": [],
            "strippedCandidates": [
                {
                    "sceneId": "3476236738",
                    "sceneType": "Puppet",
                    "layerId": 124,
                    "layerName": "Window Water",
                    "layerImage": "materials/window.png",
                    "policy": {"reason": "puppet-alpha-strip"},
                    "effectNames": ["waterflow"],
                    "materialShaders": ["effects/waterflow"],
                    "candidateFamilies": ["waterflow"],
                    "candidateRisk": "simple-water",
                    "candidateBlockedReason": "water-effect-candidate",
                    "candidateChecks": {
                        "hasWaterFamily": True,
                        "waterOnly": True,
                        "isUtilityCarrier": False,
                        "isComposelayer": False,
                        "isFullscreen": False,
                        "isPuppetLayer": False,
                        "isProtectedPuppetPath": False,
                    },
                },
                {
                    "sceneId": "3228578419",
                    "sceneType": "Puppet",
                    "layerId": 405,
                    "layerName": "ARONA_CROP_SHEET",
                    "layerImage": "materials/crop.png",
                    "policy": {"reason": "puppet-alpha-strip"},
                    "effectNames": ["waterwaves", "pulse"],
                    "materialShaders": ["effects/waterwaves", "effects/pulse"],
                    "candidateFamilies": ["waterwaves"],
                    "candidateRisk": "protected-puppet-path",
                    "candidateBlockedReason": "protected-puppet-path",
                    "candidateChecks": {
                        "hasWaterFamily": True,
                        "waterOnly": False,
                        "isUtilityCarrier": False,
                        "isComposelayer": False,
                        "isFullscreen": False,
                        "isPuppetLayer": True,
                        "isProtectedPuppetPath": True,
                    },
                },
            ],
        }

        completed = self.run_summary(manifest)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("stripped-candidate-risks:", completed.stdout)
        self.assertIn("  - protected-puppet-path: 1", completed.stdout)
        self.assertIn("  - simple-water: 1", completed.stdout)
        self.assertIn("stripped-candidate-families:", completed.stdout)
        self.assertIn("  - waterflow: 1", completed.stdout)
        self.assertIn("  - waterwaves: 1", completed.stdout)
        self.assertIn("  - 124:Window Water reason=puppet-alpha-strip risk=simple-water shape=unknown families=waterflow mix=none effects=1 shaders=1", completed.stdout)
        self.assertIn("  - 405:ARONA_CROP_SHEET reason=puppet-alpha-strip risk=protected-puppet-path shape=unknown families=waterwaves mix=none effects=2 shaders=2", completed.stdout)

    def test_reports_candidate_chain_shapes_and_mix_families(self):
        manifest = {
            "sceneId": "3476236738",
            "captures": [],
            "passStates": [],
            "strippedCandidates": [
                {
                    "layerId": 168,
                    "layerName": "Mixed Water",
                    "policy": {"reason": "puppet-alpha-strip"},
                    "effectNames": ["waterwaves", "opacity"],
                    "materialShaders": ["effects/waterwaves", "effects/opacity"],
                    "candidateFamilies": ["waterwaves"],
                    "candidateMixFamilies": ["opacity"],
                    "candidateRisk": "mixed-chain",
                    "candidateChainShape": "water+opacity",
                    "candidateBlockedReason": "water-effect-mixed-chain",
                },
                {
                    "layerId": 277,
                    "layerName": "Audio Bars",
                    "policy": {"reason": "puppet-alpha-strip"},
                    "effectNames": ["waterwaves", "audio"],
                    "materialShaders": ["effects/waterwaves", "effects/audio"],
                    "candidateFamilies": ["waterwaves"],
                    "candidateMixFamilies": ["audio"],
                    "candidateRisk": "utility-carrier",
                    "candidateChainShape": "audio-utility",
                    "candidateBlockedReason": "utility-carrier",
                },
            ],
        }

        completed = self.run_summary(manifest)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("stripped-candidate-chain-shapes:", completed.stdout)
        self.assertIn("  - audio-utility: 1", completed.stdout)
        self.assertIn("  - water+opacity: 1", completed.stdout)
        self.assertIn("stripped-candidate-mix-families:", completed.stdout)
        self.assertIn("  - audio: 1", completed.stdout)
        self.assertIn("  - opacity: 1", completed.stdout)
        self.assertIn(
            "  - 168:Mixed Water reason=puppet-alpha-strip risk=mixed-chain "
            "shape=water+opacity families=waterwaves mix=opacity effects=2 shaders=2",
            completed.stdout,
        )

    def test_reports_high_risk_blur_lut_color_grade_candidates(self):
        manifest = {
            "sceneId": "3228578419",
            "captures": [],
            "passStates": [],
            "strippedCandidates": [
                {
                    "layerId": 53,
                    "layerName": "Background Blur",
                    "policy": {"reason": "puppet-alpha-strip"},
                    "effectNames": ["blur"],
                    "materialShaders": ["effects/blur_precise_gaussian"],
                    "candidateMixFamilies": ["blur"],
                    "candidateRisk": "non-water",
                    "candidateChainShape": "blur-fullscreen",
                },
                {
                    "layerId": 174,
                    "layerName": "Background LUT",
                    "policy": {"reason": "puppet-alpha-strip"},
                    "effectNames": ["lut_loader"],
                    "materialShaders": ["workshop/3165346237/effects/lut_loader"],
                    "candidateMixFamilies": ["lut"],
                    "candidateRisk": "non-water",
                    "candidateChainShape": "lut-only",
                },
                {
                    "layerId": 365,
                    "layerName": "Color Grade Composelayer",
                    "policy": {"reason": "puppet-alpha-strip"},
                    "effectNames": ["color grading"],
                    "materialShaders": ["workshop/2795521260/effects/color_grading"],
                    "candidateMixFamilies": ["color-grade"],
                    "candidateRisk": "non-water",
                    "candidateChainShape": "color-grade-composelayer",
                },
            ],
        }

        completed = self.run_summary(manifest)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("stripped-high-risk-candidates=3", completed.stdout)
        self.assertIn("stripped-high-risk-families:", completed.stdout)
        self.assertIn("  - blur: 1", completed.stdout)
        self.assertIn("  - color-grade: 1", completed.stdout)
        self.assertIn("  - lut: 1", completed.stdout)
        self.assertIn("stripped-high-risk-layers:", completed.stdout)
        self.assertIn(
            "  - 53:Background Blur risk=non-water shape=blur-fullscreen "
            "families=blur effects=1 shaders=1",
            completed.stdout,
        )
        self.assertIn(
            "  - 174:Background LUT risk=non-water shape=lut-only "
            "families=lut effects=1 shaders=1",
            completed.stdout,
        )
        self.assertIn(
            "  - 365:Color Grade Composelayer risk=non-water "
            "shape=color-grade-composelayer families=color-grade effects=1 shaders=1",
            completed.stdout,
        )

    def test_reports_allowed_simple_water_rendered_layers(self):
        manifest = {
            "sceneId": "3476236738",
            "captures": [
                {
                    "stage": "effect-input",
                    "layer": {
                        "layerId": 124,
                        "layerName": "Window Water",
                        "policy": {
                            "keepLayer": True,
                            "keepEffects": True,
                            "strippedEffects": False,
                            "forceAlphaOne": False,
                            "reason": "simple-water-effect",
                        },
                        "effectNames": ["waterflow"],
                        "materialShaders": ["effects/waterflow"],
                        "candidateFamilies": ["waterflow"],
                        "candidateRisk": "simple-water",
                        "candidateBlockedReason": "water-effect-candidate",
                    },
                },
                {
                    "stage": "effect-output",
                    "layer": {
                        "layerId": 124,
                        "layerName": "Window Water",
                        "policy": {
                            "keepLayer": True,
                            "keepEffects": True,
                            "strippedEffects": False,
                            "forceAlphaOne": False,
                            "reason": "simple-water-effect",
                        },
                        "effectNames": ["waterflow"],
                        "materialShaders": ["effects/waterflow"],
                        "candidateFamilies": ["waterflow"],
                        "candidateRisk": "simple-water",
                        "candidateBlockedReason": "water-effect-candidate",
                    },
                },
            ],
            "passStates": [],
            "strippedCandidates": [
                {
                    "layerId": 168,
                    "layerName": "Mixed Water",
                    "policy": {"reason": "puppet-alpha-strip"},
                    "effectNames": ["waterwaves", "opacity"],
                    "materialShaders": ["effects/waterwaves", "effects/opacity"],
                    "candidateFamilies": ["waterwaves"],
                    "candidateRisk": "mixed-chain",
                    "candidateBlockedReason": "water-effect-mixed-chain",
                },
            ],
        }

        completed = self.run_summary(manifest)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("allowed-simple-water-candidates=1", completed.stdout)
        self.assertIn("allowed-simple-water-families:", completed.stdout)
        self.assertIn("  - waterflow: 1", completed.stdout)
        self.assertIn("allowed-simple-water-layers:", completed.stdout)
        self.assertIn("  - 124:Window Water reason=simple-water-effect families=waterflow effects=1 shaders=1", completed.stdout)
        self.assertIn("strippedCandidates=1", completed.stdout)
        self.assertIn("  - mixed-chain: 1", completed.stdout)

    def test_old_manifest_without_stripped_candidates_reports_zero(self):
        completed = self.run_summary({
            "sceneId": "3476236738",
            "captures": [],
            "passStates": [],
        })

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("strippedCandidates=0", completed.stdout)
        self.assertNotIn("stripped-candidate-risks:", completed.stdout)

    def test_reports_protected_puppet_cutout_inventory(self):
        manifest = {
            "captures": [
                {
                    "stage": "effect-output",
                    "layer": {
                        "layerId": 405,
                        "layerName": "Generic Crop Sheet",
                        "candidateChainShape": "protected-puppet-mixed",
                        "candidateEffectClass": "protected-puppet-lut",
                        "candidateFamilies": ["waterwaves"],
                        "candidateMixFamilies": ["lut", "shake"],
                        "candidateChecks": {
                            "isProtectedPuppetPath": True,
                            "isPuppetLayer": True,
                        },
                        "policy": {"reason": "protected-puppet-effect", "keepEffects": True},
                        "puppetAnimationLayers": [
                            {
                                "animationId": 781,
                                "animationName": "Active",
                                "visibleAndWeighted": True,
                                "activeBoneSlots": [2],
                            }
                        ],
                        "publish": {
                            "puppetLayer": True,
                            "puppetCutoutSlotCoverage": [
                                {"slot": 2, "active": True, "vertexCount": 42, "triangleCount": 17},
                                {"slot": 5, "active": False, "vertexCount": 11, "triangleCount": 4},
                            ],
                        },
                    },
                }
            ],
            "strippedCandidates": [],
            "protectedPuppetDiagnostics": [],
        }

        completed = self.run_summary(manifest)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("protected-puppet-cutout-inventory=1", completed.stdout)
        self.assertIn("layer=405", completed.stdout)
        self.assertIn("activeAnimations=781", completed.stdout)
        self.assertIn("activeSlots=2", completed.stdout)
        self.assertIn(
            "slotCoverage=2*:unnamed[none#unknown]:primary=42v/17t:weighted=42v/17t:sim=no,"
            "5:unnamed[none#unknown]:primary=11v/4t:weighted=11v/4t:sim=no",
            completed.stdout,
        )

    def test_summarizes_nested_candidate_layer_shape(self):
        completed = self.run_summary({
            "sceneId": "3476236738",
            "captures": [],
            "passStates": [],
            "strippedCandidates": [
                {
                    "layer": {
                        "layerId": 201,
                        "layerName": "Nested",
                        "policy": {"reason": "nested-reason"},
                        "effectNames": ["opacity"],
                        "materialShaders": ["effects/opacity", "effects/tint"],
                    }
                }
            ],
        })

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("strippedCandidates=1", completed.stdout)
        self.assertIn("  - nested-reason: 1", completed.stdout)
        self.assertIn("  - 201:Nested reason=nested-reason risk=unknown shape=unknown families=none mix=none effects=1 shaders=2", completed.stdout)

    def test_malformed_stripped_candidate_entries_report_unknowns(self):
        completed = self.run_summary({
            "sceneId": "3476236738",
            "captures": [],
            "passStates": [],
            "strippedCandidates": [None, "bad"],
        })

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("strippedCandidates=2", completed.stdout)
        self.assertIn("  - unknown: 2", completed.stdout)
        self.assertIn("  - unknown:unnamed reason=unknown risk=unknown shape=unknown families=none mix=none effects=0 shaders=0", completed.stdout)


if __name__ == "__main__":
    unittest.main()
