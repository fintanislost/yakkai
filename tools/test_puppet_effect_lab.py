import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

import puppet_effect_lab as lab


class PuppetEffectLabTests(unittest.TestCase):
    def write_json(self, path: Path, payload: dict) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload), encoding="utf-8")

    def write_rgba(self, path: Path, box: tuple[int, int, int, int]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        image = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        draw.rectangle(box, fill=(120, 80, 40, 255))
        image.save(path)

    def test_build_report_classifies_puppet_waterwaves_boundaries(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            layer_dir = root / "3476236738" / "22_unnamed"
            effect_input = layer_dir / "effect_input.tga"
            effect_output = layer_dir / "effect_output.tga"
            final_before = layer_dir / "final_display_before.tga"
            final_after = layer_dir / "final_display_after.tga"
            final_publish = layer_dir / "final_publish.tga"

            self.write_rgba(effect_input, (8, 8, 20, 20))
            self.write_rgba(effect_output, (8, 8, 20, 20))
            self.write_rgba(final_before, (0, 0, 0, 0))
            self.write_rgba(final_after, (16, 16, 30, 30))
            self.write_rgba(final_publish, (16, 16, 30, 30))

            manifest = root / "manifest.json"
            self.write_json(
                manifest,
                {
                    "captures": [
                        {
                            "stage": "effect-input",
                            "path": str(effect_input),
                            "renderTargetInfo": {"width": 32, "height": 32},
                            "layer": {
                                "layerId": 22,
                                "layerName": "Puppet",
                                "candidateChainShape": "puppet-mixed",
                                "candidateFamilies": ["waterwaves"],
                                "candidateMixFamilies": ["opacity"],
                                "materialShaders": ["effects/waterwaves"],
                                "effectNames": ["water waves"],
                                "debugProbe": {
                                    "requested": True,
                                    "maxEffects": 1,
                                    "keptVisibleEffectCount": 1,
                                },
                                "publish": {
                                    "puppetLayer": True,
                                    "standalonePuppetFinalDisplay": True,
                                    "effectInputMeshKind": "puppet-skinned-mesh",
                                    "standaloneFinalMeshKind": "layer-card",
                                    "routeRisk": "",
                                },
                            },
                        },
                        {
                            "stage": "effect-output",
                            "path": str(effect_output),
                            "renderTargetInfo": {"width": 32, "height": 32},
                            "layer": {"layerId": 22, "layerName": "Puppet"},
                        },
                        {
                            "stage": "final-display-before",
                            "path": str(final_before),
                            "renderTargetInfo": {"width": 32, "height": 32},
                            "layer": {"layerId": 22, "layerName": "Puppet"},
                        },
                        {
                            "stage": "final-display-after",
                            "path": str(final_after),
                            "renderTargetInfo": {"width": 32, "height": 32},
                            "layer": {"layerId": 22, "layerName": "Puppet"},
                        },
                        {
                            "stage": "final-publish",
                            "path": str(final_publish),
                            "renderTargetInfo": {"width": 32, "height": 32},
                            "layer": {"layerId": 22, "layerName": "Puppet"},
                        },
                    ]
                },
            )

            report = lab.build_report(manifest, layer_ids=[22])

        self.assertEqual(report["manifest"], str(manifest))
        self.assertEqual(len(report["layers"]), 1)
        layer = report["layers"][0]
        self.assertEqual(layer["layerId"], 22)
        self.assertEqual(layer["firstShader"], "effects/waterwaves")
        self.assertEqual(layer["route"]["classification"], "standalone-puppet-effect-route")
        self.assertEqual(layer["finalCoverage"]["classification"], "final-display-boundary-present")
        self.assertEqual(
            layer["finalCoverage"]["finalDisplayAlignment"]["classification"],
            "final-display-shape-drift",
        )
        comparisons = {item["boundary"]: item for item in layer["boundaryComparisons"]}
        self.assertEqual(
            comparisons["effect-input->effect-output"]["classification"],
            "stable",
        )


if __name__ == "__main__":
    unittest.main()
