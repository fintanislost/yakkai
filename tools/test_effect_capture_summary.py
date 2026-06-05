import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class EffectCaptureSummaryTests(unittest.TestCase):
    def run_summary(self, manifest_data):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps(manifest_data), encoding="utf-8")

            return subprocess.run(
                [sys.executable, "tools/effect-capture-summary.py", str(manifest)],
                cwd=Path(__file__).resolve().parents[1],
                check=True,
                text=True,
                capture_output=True,
            )

    def test_groups_lut_color_classes_by_disposition(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = root / "manifest.json"
            manifest.write_text(
                json.dumps(
                    {
                        "sceneId": "unit-scene",
                        "captures": [
                            {
                                "stage": "effect-input",
                                "layer": {
                                    "layerId": 82,
                                    "layerName": "WALL",
                                    "policy": {
                                        "keepEffects": True,
                                        "strippedEffects": False,
                                        "reason": "lut-only-effect",
                                    },
                                    "candidateEffectClass": "regular-lut-only",
                                    "candidateChainShape": "lut-only",
                                    "candidateMixFamilies": ["lut"],
                                },
                            }
                        ],
                        "strippedCandidates": [
                            {
                                "layerId": 405,
                                "layerName": "ARONA_CROP_SHEET",
                                "policy": {
                                    "keepEffects": False,
                                    "strippedEffects": True,
                                    "reason": "puppet-alpha-strip",
                                },
                                "candidateEffectClass": "protected-puppet-lut",
                                "candidateChainShape": "protected-puppet-mixed",
                                "candidateMixFamilies": ["lut", "pulse"],
                                "candidateBlockedReason": "protected-puppet-path",
                            },
                            {
                                "layerId": 512,
                                "layerName": "Character LUT",
                                "policy": {
                                    "keepEffects": False,
                                    "strippedEffects": True,
                                    "reason": "puppet-alpha-strip",
                                },
                                "candidateEffectClass": "mixed-puppet-lut",
                                "candidateChainShape": "puppet-mixed",
                                "candidateMixFamilies": ["lut"],
                                "candidateBlockedReason": "puppet-layer",
                                "debugProbe": {
                                    "requested": True,
                                    "overrodePolicy": True,
                                    "reason": "layer-id-probe",
                                },
                            },
                            {
                                "layerId": 365,
                                "layerName": "Composite Grade",
                                "policy": {
                                    "keepLayer": False,
                                    "keepEffects": False,
                                    "strippedEffects": True,
                                    "reason": "puppet-alpha-strip",
                                },
                                "candidateEffectClass": "composelayer-color-grade",
                                "candidateChainShape": "color-grade-composelayer",
                                "candidateMixFamilies": ["color-grade"],
                                "candidateBlockedReason": "composelayer-carrier",
                            },
                        ],
                    }
                ),
                encoding="utf-8",
            )

            completed = subprocess.run(
                [sys.executable, "tools/effect-capture-summary.py", str(manifest)],
                cwd=Path(__file__).resolve().parents[1],
                check=True,
                text=True,
                capture_output=True,
            )

        output = completed.stdout
        self.assertIn("lut-color-class-counts:", output)
        self.assertIn("  - regular-lut-only: 1", output)
        self.assertIn("  - protected-puppet-lut: 1", output)
        self.assertIn("  - mixed-puppet-lut: 1", output)
        self.assertIn("  - composelayer-color-grade: 1", output)
        self.assertIn("lut-color-disposition-counts:", output)
        self.assertIn("  - allowed: 1", output)
        self.assertIn("  - protected: 1", output)
        self.assertIn("  - probe-only: 1", output)
        self.assertIn("  - stripped: 1", output)
        self.assertIn("class=protected-puppet-lut disposition=protected", output)
        self.assertIn("class=mixed-puppet-lut disposition=probe-only", output)

    def test_groups_blur_probe_candidates_by_shape_and_disposition(self):
        completed = self.run_summary(
            {
                "sceneId": "3476236738",
                "captures": [
                    {
                        "stage": "final-publish",
                        "layer": {
                            "layerId": 137,
                            "layerName": "桌左",
                            "policy": {
                                "keepEffects": False,
                                "strippedEffects": True,
                                "reason": "puppet-alpha-strip",
                            },
                            "candidateEffectClass": "regular-blur-only",
                            "candidateChainShape": "blur-only",
                            "candidateMixFamilies": ["blur"],
                            "candidateBlockedReason": "no-water-effect-family",
                            "debugProbe": {
                                "requested": True,
                                "overrodePolicy": True,
                                "reason": "high-risk-layer-id-probe",
                            },
                        },
                    }
                ],
                "strippedCandidates": [
                    {
                        "layerId": 137,
                        "layerName": "桌左",
                        "policy": {
                            "keepEffects": False,
                            "strippedEffects": True,
                            "reason": "puppet-alpha-strip",
                        },
                        "candidateEffectClass": "regular-blur-only",
                        "candidateChainShape": "blur-only",
                        "candidateMixFamilies": ["blur"],
                        "candidateBlockedReason": "no-water-effect-family",
                        "debugProbe": {
                            "requested": True,
                            "overrodePolicy": True,
                            "reason": "high-risk-layer-id-probe",
                        },
                    },
                    {
                        "layerId": 365,
                        "layerName": "可调整组合层",
                        "policy": {
                            "keepLayer": False,
                            "keepEffects": False,
                            "strippedEffects": True,
                            "reason": "puppet-alpha-strip",
                        },
                        "candidateEffectClass": "composelayer-color-grade",
                        "candidateChainShape": "blur-color-grade-composelayer",
                        "candidateMixFamilies": ["blur", "color-grade"],
                        "candidateBlockedReason": "composelayer-carrier",
                    }
                ],
            }
        )

        output = completed.stdout
        self.assertIn("high-risk-disposition-counts:", output)
        self.assertIn("  - probe-only: 1", output)
        self.assertIn("  - stripped: 1", output)
        self.assertIn("high-risk-shape-counts:", output)
        self.assertIn("  - blur-only: 1", output)
        self.assertIn("  - blur-color-grade-composelayer: 1", output)
        self.assertIn("high-risk-probe-layers:", output)
        self.assertIn(
            "137:桌左 families=blur class=regular-blur-only "
            "disposition=probe-only reason=puppet-alpha-strip "
            "shape=blur-only blocked=no-water-effect-family",
            output,
        )
        self.assertEqual(output.count("137:桌左 families=blur"), 1)
        self.assertIn(
            "365:可调整组合层 families=blur,color-grade class=composelayer-color-grade "
            "disposition=stripped reason=puppet-alpha-strip "
            "shape=blur-color-grade-composelayer blocked=composelayer-carrier",
            output,
        )

    def test_protected_puppet_inventory_reports_weighted_secondary_slots(self):
        completed = self.run_summary(
            {
                "sceneId": "3228578419",
                "protectedPuppetDiagnostics": [
                    {
                        "layerId": 405,
                        "layerName": "ARONA_CROP_SHEET",
                        "candidateEffectClass": "protected-puppet-lut",
                        "candidateChainShape": "protected-puppet-mixed",
                        "candidateChecks": {"isProtectedPuppetPath": True},
                        "puppetAnimationLayers": [
                            {
                                "animationId": 469,
                                "visibleAndWeighted": True,
                                "activeBoneSlots": [3],
                            }
                        ],
                        "publish": {
                            "puppetCutoutSlotCoverage": [
                                {
                                    "slot": 3,
                                    "active": True,
                                    "boneName": "ribbon",
                                    "parentSlot": 0,
                                    "parentBoneName": "root",
                                    "primaryVertexCount": 785,
                                    "primaryTriangleCount": 1453,
                                    "weightedVertexCount": 402,
                                    "weightedTriangleCount": 773,
                                    "secondaryOnly": False,
                                    "simulationMetadataPresent": True,
                                    "simulationMetadataValid": True,
                                    "simulationTargetPointPresent": True,
                                    "simulationTargetPoint": [0.0, 200.0, 0.0],
                                    "simulationTargetMassPresent": True,
                                    "simulationTargetMass": 100.0,
                                    "simulationPhysicsActive": False,
                                },
                                {
                                    "slot": 13,
                                    "active": False,
                                    "boneName": "",
                                    "parentSlot": 3,
                                    "parentBoneName": "ribbon",
                                    "primaryVertexCount": 0,
                                    "primaryTriangleCount": 0,
                                    "weightedVertexCount": 409,
                                    "weightedTriangleCount": 786,
                                    "secondaryOnly": True,
                                    "simulationMetadataPresent": False,
                                },
                                {
                                    "slot": 14,
                                    "active": False,
                                    "boneName": "ribbon-tip",
                                    "parentSlot": 3,
                                    "parentBoneName": "ribbon",
                                    "primaryVertexCount": 14,
                                    "primaryTriangleCount": 20,
                                    "weightedVertexCount": 14,
                                    "weightedTriangleCount": 20,
                                    "secondaryOnly": False,
                                    "simulationMetadataPresent": True,
                                    "simulationMetadataValid": True,
                                    "simulationTargetPointPresent": True,
                                    "simulationTargetPoint": [0.0, 200.0, 0.0],
                                    "simulationTargetMassPresent": True,
                                    "simulationTargetMass": 100.0,
                                    "simulationPhysicsActive": True,
                                    "simulatedInactive": True,
                                },
                            ],
                        },
                    }
                ],
            }
        )

        output = completed.stdout
        self.assertIn("protected-puppet-cutout-inventory=1", output)
        self.assertIn(
            "3*:ribbon[root#0]:primary=785v/1453t:weighted=402v/773t"
            ":sim=yes:simValid=yes:simPhysics=no:simTp=0/200/0:simTm=100",
            output,
        )
        self.assertIn(
            "13:unnamed[ribbon#3]:primary=0v/0t:weighted=409v/786t:secondary-only:sim=no",
            output,
        )
        self.assertIn(
            "14:ribbon-tip[ribbon#3]:primary=14v/20t:weighted=14v/20t"
            ":sim=yes:simValid=yes:simPhysics=yes:simTp=0/200/0:simTm=100:simInactive=yes",
            output,
        )


if __name__ == "__main__":
    unittest.main()
