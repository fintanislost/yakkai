import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "effect-candidate-inventory.py"


class EffectCandidateInventoryTests(unittest.TestCase):
    def run_inventory(self, manifests, *args):
        with tempfile.TemporaryDirectory() as tmp:
            paths = []
            for index, manifest in enumerate(manifests):
                path = Path(tmp) / f"manifest-{index}.json"
                path.write_text(json.dumps(manifest), encoding="utf-8")
                paths.append(path)
            return subprocess.run(
                ["python3", str(TOOL), *map(str, paths), *args],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

    def test_summarizes_candidate_classes_and_dispositions(self):
        manifest = {
            "sceneId": "fixture-a",
            "sceneType": "Puppet",
            "layers": [
                {
                    "layerId": 82,
                    "layerName": "Wall",
                    "candidateEffectClass": "regular-lut-only",
                    "candidateChainShape": "lut-only",
                    "policy": {"reason": "regular-lut-only", "keepEffects": True},
                    "candidateFamilies": ["lut_loader"],
                    "candidateChecks": {"hasLutFamily": True},
                }
            ],
            "strippedCandidates": [
                {
                    "layerId": 137,
                    "layerName": "Desk Blur",
                    "candidateEffectClass": "regular-blur-only",
                    "candidateChainShape": "blur-only",
                    "policy": {"reason": "high-risk-effect", "strippedEffects": True},
                    "candidateFamilies": ["blur_precise_gaussian"],
                    "candidateChecks": {"hasBlurFamily": True},
                }
            ],
            "protectedPuppetDiagnostics": [
                {
                    "layerId": 405,
                    "layerName": "Crop Sheet",
                    "candidateEffectClass": "protected-puppet-lut",
                    "candidateChainShape": "protected-puppet-mixed",
                    "candidateFamilies": ["waterwaves"],
                    "candidateMixFamilies": ["lut", "shake"],
                    "policy": {"reason": "protected-puppet-effect", "keepEffects": True},
                    "candidateChecks": {
                        "isProtectedPuppetPath": True,
                        "isPuppetLayer": True,
                    },
                    "puppetAnimationLayers": [
                        {
                            "animationId": 781,
                            "visibleAndWeighted": True,
                            "activeBoneSlots": [2],
                        }
                    ],
                    "publish": {
                        "routeRisk": "",
                        "finalDisplayRoute": "standalone-puppet-final-display",
                        "puppetCutoutSlotCoverage": [
                            {"slot": 2, "active": True, "vertexCount": 157, "triangleCount": 294}
                        ],
                    },
                }
            ],
        }

        completed = self.run_inventory([manifest])

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("candidate-records=3", completed.stdout)
        self.assertIn("effect-classes:", completed.stdout)
        self.assertIn("  - protected-puppet-lut: 1", completed.stdout)
        self.assertIn("  - regular-blur-only: 1", completed.stdout)
        self.assertIn("  - regular-lut-only: 1", completed.stdout)
        self.assertIn("dispositions:", completed.stdout)
        self.assertIn("  - allowed: 1", completed.stdout)
        self.assertIn("  - protected: 1", completed.stdout)
        self.assertIn("  - stripped: 1", completed.stdout)
        self.assertIn(
            "protected-puppet-lut protected protected-puppet-mixed layer=405 activeSlots=2",
            completed.stdout,
        )

    def test_json_output_contains_normalized_records(self):
        manifest = {
            "sceneId": "fixture-json",
            "strippedCandidates": [
                {
                    "layerId": 53,
                    "layerName": "Utility Blur",
                    "candidateEffectClass": "utility-blur",
                    "candidateChainShape": "blur-utility",
                    "policy": {"reason": "high-risk-effect", "strippedEffects": True},
                    "candidateChecks": {
                        "hasBlurFamily": True,
                        "isUtilityCarrier": True,
                    },
                }
            ],
        }

        completed = self.run_inventory([manifest], "--json")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["summary"]["candidateRecords"], 1)
        record = payload["records"][0]
        self.assertEqual(record["sceneId"], "fixture-json")
        self.assertEqual(record["source"], "stripped")
        self.assertEqual(record["disposition"], "stripped")
        self.assertEqual(record["effectClass"], "utility-blur")
        self.assertTrue(record["checks"]["isUtilityCarrier"])

    def test_infers_useful_class_when_manifest_has_no_explicit_effect_class(self):
        manifest = {
            "sceneId": "fixture-inferred",
            "layers": [
                {
                    "layerId": 124,
                    "layerName": "Window Water",
                    "candidateEffectClass": "none",
                    "candidateRisk": "simple-water",
                    "candidateChainShape": "simple-water",
                    "policy": {"reason": "simple-water-effect", "keepEffects": True},
                    "candidateChecks": {
                        "hasWaterFamily": True,
                        "waterOnly": True,
                    },
                }
            ],
            "strippedCandidates": [
                {
                    "layerId": 219,
                    "layerName": "Audio Bars",
                    "candidateEffectClass": "none",
                    "candidateChainShape": "audio-utility",
                    "policy": {"reason": "puppet-alpha-strip", "strippedEffects": True},
                    "candidateChecks": {"isUtilityCarrier": True},
                }
            ],
        }

        completed = self.run_inventory([manifest])

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("  - audio-utility stripped audio-utility layer=219", completed.stdout)
        self.assertIn("  - simple-water allowed simple-water layer=124", completed.stdout)

    def test_infers_protected_puppet_safe_family_when_effect_class_is_none(self):
        manifest = {
            "sceneId": "fixture-protected",
            "layers": [
                {
                    "layerId": 405,
                    "layerName": "Protected Crop",
                    "candidateEffectClass": "none",
                    "candidateRisk": "protected-puppet-path",
                    "candidateChainShape": "protected-puppet-mixed",
                    "policy": {"reason": "protected-puppet-effect", "keepEffects": True},
                    "candidateFamilies": ["waterwaves"],
                    "candidateMixFamilies": ["pulse", "shake"],
                    "candidateChecks": {
                        "isProtectedPuppetPath": True,
                        "isPuppetLayer": True,
                        "hasWaterFamily": True,
                        "hasLutFamily": False,
                        "hasBlurFamily": False,
                        "hasColorGradingFamily": False,
                    },
                    "puppetAnimationLayers": [
                        {
                            "animationId": 781,
                            "visibleAndWeighted": True,
                            "activeBoneSlots": [2],
                        }
                    ],
                }
            ],
        }

        completed = self.run_inventory([manifest])

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("  - protected-puppet-safe-family: 1", completed.stdout)
        self.assertIn(
            "  - protected-puppet-safe-family protected protected-puppet-mixed layer=405 activeSlots=2",
            completed.stdout,
        )

    def test_infers_water_carrier_classes_when_effect_class_is_none(self):
        manifest = {
            "sceneId": "fixture-water-carriers",
            "strippedCandidates": [
                {
                    "layerId": 239,
                    "layerName": "Water Composition",
                    "candidateEffectClass": "none",
                    "candidateRisk": "composelayer-carrier",
                    "candidateChainShape": "water-composelayer",
                    "candidateFamilies": ["waterripple", "waterflow"],
                    "policy": {"reason": "puppet-alpha-strip", "strippedEffects": True},
                    "candidateChecks": {
                        "hasWaterFamily": True,
                        "waterOnly": True,
                        "isComposelayer": True,
                    },
                },
                {
                    "layerId": 240,
                    "layerName": "Water Utility",
                    "candidateEffectClass": "none",
                    "candidateRisk": "utility-carrier",
                    "candidateChainShape": "water-utility",
                    "candidateFamilies": ["waterflow"],
                    "policy": {"reason": "puppet-alpha-strip", "strippedEffects": True},
                    "candidateChecks": {
                        "hasWaterFamily": True,
                        "waterOnly": True,
                        "isUtilityCarrier": True,
                    },
                },
            ],
        }

        completed = self.run_inventory([manifest])

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("  - composelayer-water-only: 1", completed.stdout)
        self.assertIn("  - utility-water-only: 1", completed.stdout)
        self.assertIn(
            "  - composelayer-water-only stripped water-composelayer layer=239",
            completed.stdout,
        )

    def test_json_output_audits_regular_lut_route_completeness(self):
        manifest = {
            "sceneId": "fixture-lut-route",
            "layers": [
                {
                    "layerId": 82,
                    "layerName": "Wall LUT",
                    "candidateEffectClass": "regular-lut-only",
                    "candidateChainShape": "lut-only",
                    "policy": {
                        "reason": "lut-only-effect",
                        "keepEffects": True,
                        "strippedEffects": False,
                    },
                    "publish": {
                        "routeRisk": "",
                        "finalDisplayRoute": "effect-layer-node-final-publish",
                        "publishFinalOutput": True,
                        "finalNodeUsesOriginalParent": True,
                        "effectInputNodeReset": True,
                        "effectInputMeshKind": "card",
                        "effectFinalMeshKind": "card",
                    },
                    "effectMaterials": [
                        {
                            "shader": "workshop/3165346237/effects/lut_loader",
                            "finalPublishedMaterial": True,
                            "materialOutputCaptureStage": "material-output-1-0",
                            "localMaterialOutputCaptureStage": "material-output-local-1-0",
                            "resolvedOutputRenderTarget": "_rt_default",
                        }
                    ],
                }
            ],
        }

        completed = self.run_inventory([manifest], "--json")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["summary"]["routeAudits"]["route-complete"], 1)
        route_audit = payload["records"][0]["routeAudit"]
        self.assertEqual(route_audit["classification"], "route-complete")
        self.assertEqual(route_audit["materialCount"], 1)
        self.assertEqual(route_audit["finalPublishedMaterialCount"], 1)
        self.assertEqual(route_audit["localMaterialOutputCaptureCount"], 1)
        self.assertEqual(route_audit["missing"], [])

    def test_json_output_flags_regular_lut_route_missing_local_material_capture(self):
        manifest = {
            "sceneId": "fixture-lut-route-missing-local",
            "layers": [
                {
                    "layerId": 82,
                    "layerName": "Wall LUT",
                    "candidateEffectClass": "regular-lut-only",
                    "candidateChainShape": "lut-only",
                    "policy": {
                        "reason": "lut-only-effect",
                        "keepEffects": True,
                        "strippedEffects": False,
                    },
                    "publish": {
                        "routeRisk": "",
                        "finalDisplayRoute": "effect-layer-node-final-publish",
                        "publishFinalOutput": True,
                        "finalNodeUsesOriginalParent": True,
                        "effectInputNodeReset": True,
                        "effectInputMeshKind": "card",
                        "effectFinalMeshKind": "card",
                    },
                    "effectMaterials": [
                        {
                            "shader": "workshop/3165346237/effects/lut_loader",
                            "finalPublishedMaterial": True,
                            "materialOutputCaptureStage": "material-output-1-0",
                            "resolvedOutputRenderTarget": "_rt_default",
                        }
                    ],
                }
            ],
        }

        completed = self.run_inventory([manifest], "--json")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["summary"]["routeAudits"]["missing-local-material-output-capture"], 1)
        route_audit = payload["records"][0]["routeAudit"]
        self.assertEqual(route_audit["classification"], "missing-local-material-output-capture")
        self.assertEqual(route_audit["missing"], ["local-material-output-capture"])

    def test_json_output_flags_composelayer_color_grade_needs_probe_route_evidence(self):
        manifest = {
            "sceneId": "fixture-composelayer-stripped",
            "strippedCandidates": [
                {
                    "layerId": 365,
                    "layerName": "Adjustable Composition",
                    "candidateEffectClass": "composelayer-color-grade",
                    "candidateChainShape": "blur-color-grade-composelayer",
                    "candidateMixFamilies": ["color-grade", "blur"],
                    "candidateChecks": {
                        "hasBlurFamily": True,
                        "hasColorGradingFamily": True,
                        "isComposelayer": True,
                    },
                    "policy": {
                        "reason": "puppet-alpha-strip",
                        "strippedEffects": True,
                    },
                }
            ],
        }

        completed = self.run_inventory([manifest], "--json")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["summary"]["visualGateAudits"]["needs-high-risk-probe-route"], 1)
        visual_gate_audit = payload["records"][0]["visualGateAudit"]
        self.assertEqual(visual_gate_audit["classification"], "needs-high-risk-probe-route")
        self.assertIn("active-probe-publish-route", visual_gate_audit["missing"])

    def test_json_output_marks_composelayer_color_grade_for_human_visual_review(self):
        manifest = {
            "sceneId": "fixture-composelayer-probe",
            "captures": [
                {
                    "stage": "effect-input",
                    "layer": {
                        "layerId": 365,
                        "layerName": "Adjustable Composition",
                        "candidateEffectClass": "composelayer-color-grade",
                        "candidateChainShape": "blur-color-grade-composelayer",
                        "candidateMixFamilies": ["color-grade", "blur"],
                        "candidateChecks": {
                            "hasBlurFamily": True,
                            "hasColorGradingFamily": True,
                            "isComposelayer": True,
                        },
                        "debugProbe": {
                            "requested": True,
                            "overrodePolicy": True,
                            "reason": "high-risk-layer-id-probe",
                        },
                        "policy": {
                            "reason": "puppet-alpha-strip",
                            "strippedEffects": True,
                        },
                        "publish": {
                            "enabled": True,
                            "finalDisplayRoute": "effect-layer-composite-final-publish",
                            "publishFinalOutput": True,
                            "effectFinalMeshKind": "fullscreen-card",
                            "effectInputMeshKind": "card",
                            "effectInputNodeReset": False,
                        },
                        "effectMaterials": [
                            {"shader": "workshop/2795521260/effects/color_grading"},
                            {"shader": "effects/blur_precise_gaussian"},
                            {"shader": "effects/blur_precise_gaussian"},
                        ],
                    },
                }
            ],
        }

        completed = self.run_inventory([manifest], "--json")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["summary"]["visualGateAudits"]["human-visual-review-required"], 1)
        visual_gate_audit = payload["records"][0]["visualGateAudit"]
        self.assertEqual(visual_gate_audit["classification"], "human-visual-review-required")
        self.assertEqual(visual_gate_audit["missing"], [])
        self.assertTrue(visual_gate_audit["requiresHumanReview"])
        self.assertEqual(visual_gate_audit["materialCount"], 3)
        self.assertEqual(visual_gate_audit["finalDisplayRoute"], "effect-layer-composite-final-publish")
        self.assertEqual(visual_gate_audit["effectFinalMeshKind"], "fullscreen-card")

    def test_json_output_marks_utility_blur_for_human_visual_review(self):
        manifest = {
            "sceneId": "fixture-utility-blur-probe",
            "captures": [
                {
                    "stage": "effect-input",
                    "layer": {
                        "layerId": 53,
                        "layerName": "Post-processing Layer==BLUR",
                        "candidateEffectClass": "utility-blur",
                        "candidateChainShape": "blur-utility",
                        "candidateMixFamilies": ["blur"],
                        "candidateChecks": {
                            "hasBlurFamily": True,
                            "isFullscreen": True,
                            "isUtilityCarrier": True,
                        },
                        "debugProbe": {
                            "requested": True,
                            "overrodePolicy": True,
                            "reason": "high-risk-layer-id-probe",
                        },
                        "policy": {
                            "reason": "puppet-alpha-strip",
                            "strippedEffects": True,
                        },
                        "publish": {
                            "enabled": True,
                            "finalDisplayRoute": "effect-layer-fullscreen-final-publish",
                            "publishFinalOutput": True,
                            "effectFinalMeshKind": "fullscreen-card",
                            "effectInputMeshKind": "card",
                            "effectInputNodeReset": True,
                        },
                        "effectMaterials": [
                            {"shader": "effects/blur_precise_gaussian"},
                            {"shader": "effects/blur_precise_gaussian"},
                        ],
                    },
                }
            ],
        }

        completed = self.run_inventory([manifest], "--json")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["summary"]["visualGateAudits"]["human-visual-review-required"], 1)
        visual_gate_audit = payload["records"][0]["visualGateAudit"]
        self.assertEqual(visual_gate_audit["classification"], "human-visual-review-required")
        self.assertEqual(visual_gate_audit["finalDisplayRoute"], "effect-layer-fullscreen-final-publish")
        self.assertTrue(visual_gate_audit["hasBlurMaterial"])
        self.assertTrue(visual_gate_audit["requiresHumanReview"])

    def test_json_output_marks_allowed_composelayer_color_grade_as_production_allowed(self):
        manifest = {
            "sceneId": "fixture-composelayer-allowed",
            "captures": [
                {
                    "stage": "effect-input",
                    "layer": {
                        "layerId": 365,
                        "layerName": "Adjustable Composition",
                        "candidateEffectClass": "composelayer-color-grade",
                        "candidateChainShape": "blur-color-grade-composelayer",
                        "candidateMixFamilies": ["color-grade", "blur"],
                        "candidateChecks": {
                            "hasBlurFamily": True,
                            "hasColorGradingFamily": True,
                            "isComposelayer": True,
                        },
                        "policy": {
                            "reason": "composelayer-color-grade-effect",
                            "keepEffects": True,
                            "strippedEffects": False,
                        },
                        "publish": {
                            "enabled": True,
                            "finalDisplayRoute": "effect-layer-composite-final-publish",
                            "publishFinalOutput": True,
                            "effectFinalMeshKind": "fullscreen-card",
                            "effectInputMeshKind": "card",
                            "effectInputNodeReset": False,
                        },
                        "effectMaterials": [
                            {"shader": "workshop/2795521260/effects/color_grading"},
                            {"shader": "effects/blur_precise_gaussian"},
                            {"shader": "effects/blur_precise_gaussian"},
                        ],
                    },
                }
            ],
        }

        completed = self.run_inventory([manifest], "--json")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["summary"]["visualGateAudits"]["production-allowed"], 1)
        visual_gate_audit = payload["records"][0]["visualGateAudit"]
        self.assertEqual(visual_gate_audit["classification"], "production-allowed")
        self.assertEqual(visual_gate_audit["missing"], [])
        self.assertFalse(visual_gate_audit["requiresHumanReview"])
        self.assertTrue(visual_gate_audit["hasColorGradeMaterial"])

    def test_json_output_marks_allowed_utility_blur_as_production_allowed(self):
        manifest = {
            "sceneId": "fixture-utility-blur-allowed",
            "captures": [
                {
                    "stage": "effect-input",
                    "layer": {
                        "layerId": 53,
                        "layerName": "Post-processing Layer==BLUR",
                        "candidateEffectClass": "utility-blur",
                        "candidateChainShape": "blur-utility",
                        "candidateMixFamilies": ["blur"],
                        "candidateChecks": {
                            "hasBlurFamily": True,
                            "isFullscreen": True,
                            "isUtilityCarrier": True,
                        },
                        "policy": {
                            "reason": "utility-blur-effect",
                            "keepEffects": True,
                            "strippedEffects": False,
                        },
                        "publish": {
                            "enabled": True,
                            "finalDisplayRoute": "effect-layer-fullscreen-final-publish",
                            "publishFinalOutput": True,
                            "effectFinalMeshKind": "fullscreen-card",
                            "effectInputMeshKind": "card",
                            "effectInputNodeReset": True,
                        },
                        "effectMaterials": [
                            {"shader": "effects/blur_precise_gaussian"},
                            {"shader": "effects/blur_precise_gaussian"},
                        ],
                    },
                }
            ],
        }

        completed = self.run_inventory([manifest], "--json")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["summary"]["visualGateAudits"]["production-allowed"], 1)
        visual_gate_audit = payload["records"][0]["visualGateAudit"]
        self.assertEqual(visual_gate_audit["classification"], "production-allowed")
        self.assertEqual(visual_gate_audit["missing"], [])
        self.assertFalse(visual_gate_audit["requiresHumanReview"])

    def test_json_output_marks_allowed_composelayer_water_as_production_allowed(self):
        manifest = {
            "sceneId": "fixture-composelayer-water-allowed",
            "captures": [
                {
                    "stage": "effect-input",
                    "layer": {
                        "layerId": 239,
                        "layerName": "Adjustable Composition Layer",
                        "candidateEffectClass": "composelayer-water-only",
                        "candidateChainShape": "water-composelayer",
                        "candidateFamilies": ["waterripple", "waterflow"],
                        "candidateChecks": {
                            "hasWaterFamily": True,
                            "isComposelayer": True,
                            "waterOnly": True,
                        },
                        "policy": {
                            "reason": "composelayer-water-effect",
                            "keepEffects": True,
                            "strippedEffects": False,
                        },
                        "publish": {
                            "enabled": True,
                            "finalDisplayRoute": "effect-layer-composite-final-publish",
                            "publishFinalOutput": True,
                            "effectFinalMeshKind": "fullscreen-card",
                            "effectInputMeshKind": "card",
                            "effectInputNodeReset": False,
                        },
                        "effectMaterials": [
                            {"shader": "effects/waterripple"},
                            {"shader": "effects/waterflow"},
                        ],
                    },
                }
            ],
        }

        completed = self.run_inventory([manifest], "--json")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["summary"]["visualGateAudits"]["production-allowed"], 1)
        visual_gate_audit = payload["records"][0]["visualGateAudit"]
        self.assertEqual(visual_gate_audit["classification"], "production-allowed")
        self.assertEqual(visual_gate_audit["missing"], [])
        self.assertFalse(visual_gate_audit["requiresHumanReview"])
        self.assertTrue(visual_gate_audit["hasWaterMaterial"])

    def test_probe_capture_suppresses_duplicate_stripped_candidate(self):
        layer = {
            "layerId": 365,
            "layerName": "Adjustable Composition",
            "candidateEffectClass": "composelayer-color-grade",
            "candidateChainShape": "blur-color-grade-composelayer",
            "candidateMixFamilies": ["color-grade", "blur"],
            "candidateChecks": {
                "hasBlurFamily": True,
                "hasColorGradingFamily": True,
                "isComposelayer": True,
            },
            "debugProbe": {
                "requested": True,
                "overrodePolicy": True,
                "reason": "high-risk-layer-id-probe",
            },
            "policy": {
                "reason": "puppet-alpha-strip",
                "strippedEffects": True,
            },
            "publish": {
                "enabled": True,
                "finalDisplayRoute": "effect-layer-composite-final-publish",
                "publishFinalOutput": True,
                "effectFinalMeshKind": "fullscreen-card",
                "effectInputMeshKind": "card",
            },
            "effectMaterials": [
                {"shader": "workshop/2795521260/effects/color_grading"},
            ],
        }
        manifest = {
            "sceneId": "fixture-composelayer-probe-dedupe",
            "captures": [{"stage": "effect-input", "layer": layer}],
            "strippedCandidates": [layer],
        }

        completed = self.run_inventory([manifest], "--json")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["summary"]["candidateRecords"], 1)
        self.assertEqual(payload["summary"]["visualGateAudits"]["human-visual-review-required"], 1)
        self.assertNotIn("incomplete-visual-gate-evidence", payload["summary"]["visualGateAudits"])


if __name__ == "__main__":
    unittest.main()
