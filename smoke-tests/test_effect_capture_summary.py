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
        self.assertIn("  - 101:Water BG reason=puppet-alpha-strip effects=2 shaders=2", completed.stdout)
        self.assertIn("  - 102:Utility reason=puppet-alpha-strip effects=1 shaders=1", completed.stdout)

    def test_old_manifest_without_stripped_candidates_reports_zero(self):
        completed = self.run_summary({
            "sceneId": "3476236738",
            "captures": [],
            "passStates": [],
        })

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("strippedCandidates=0", completed.stdout)

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
        self.assertIn("  - 201:Nested reason=nested-reason effects=1 shaders=2", completed.stdout)

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
        self.assertIn("  - unknown:unnamed reason=unknown effects=0 shaders=0", completed.stdout)


if __name__ == "__main__":
    unittest.main()
