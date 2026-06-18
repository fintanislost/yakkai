import json
import shutil
import sys
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

import media_text_diagnostics as diagnostics


class MediaTextDiagnosticsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path.cwd() / "tmp" / "yakkai-media-text-diagnostics-test"
        shutil.rmtree(self.root, ignore_errors=True)
        self.root.mkdir(parents=True, exist_ok=True)

    def test_writes_crops_contact_sheet_and_report_for_visible_generated_text(self):
        capture = self.root / "capture.png"
        image = Image.new("RGBA", (100, 100), (15, 20, 25, 255))
        draw = ImageDraw.Draw(image)
        draw.rectangle((40, 40, 60, 60), fill=(220, 230, 240, 255))
        image.save(capture)

        manifest = self.root / "manifest.json"
        manifest.write_text(
            json.dumps(
                {
                    "sceneId": "media-text-test",
                    "sceneOrtho": [100, 100],
                    "generatedTextDiagnostics": [
                        {
                            "layerId": 501,
                            "layerName": "Artist Name",
                            "text": "Mitsukiyo",
                            "textureName": "__yakkai_generated_text/501",
                            "font": "fonts/Roboto.ttf",
                            "rasterizer": "qt",
                            "fontLoaded": True,
                            "fontFamily": "Roboto",
                            "fontLoadStatus": "loaded",
                            "horizontalAlign": "left",
                            "verticalAlign": "bottom",
                            "pointSize": 38,
                            "effectivePixelSize": 114,
                            "parentId": 500,
                            "parentChain": [
                                {"layerId": 500, "layerName": "Media Parent"}
                            ],
                            "cardSize": [20, 20],
                            "color": [1, 1, 1],
                            "alpha": 1,
                            "localBounds": [-10, -10, 10, 10],
                            "worldBounds": [40, 40, 60, 60],
                            "alphaBounds": [2, 2, 18, 18],
                            "visibility": "visible-in-frame",
                            "classificationReason": "world bounds overlap orthographic viewport",
                        },
                        {
                            "layerId": 502,
                            "layerName": "Disabled Variant",
                            "text": "hidden",
                            "textureName": "__yakkai_generated_text/502",
                            "parentId": 500,
                            "parentChain": [],
                            "cardSize": [20, 20],
                            "color": [1, 1, 1],
                            "alpha": 1,
                            "localBounds": [-10, -10, 10, 10],
                            "worldBounds": [None, None, None, None],
                            "alphaBounds": [0, 0, 0, 0],
                            "visibility": "collapsed",
                            "classificationReason": "world bounds have zero area",
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )

        output_dir = self.root / "report"
        report = diagnostics.build_report(manifest, capture, output_dir)
        outputs = diagnostics.write_outputs(report, output_dir)

        self.assertEqual(report["sceneId"], "media-text-test")
        self.assertEqual(report["capture"], str(capture))
        self.assertEqual(len(report["texts"]), 2)
        text = report["texts"][0]
        self.assertEqual(text["layerId"], 501)
        self.assertEqual(text["rasterizer"], "qt")
        self.assertTrue(text["fontLoaded"])
        self.assertEqual(text["fontFamily"], "Roboto")
        self.assertEqual(text["fontLoadStatus"], "loaded")
        self.assertEqual(text["horizontalAlign"], "left")
        self.assertEqual(text["verticalAlign"], "bottom")
        self.assertEqual(text["pointSize"], 38)
        self.assertEqual(text["effectivePixelSize"], 114)
        self.assertEqual(text["cropRect"], [32, 32, 68, 68])
        self.assertEqual(text["status"], "crop-written")
        self.assertTrue(Path(text["cropPath"]).exists())
        self.assertEqual(report["texts"][1]["status"], "not-visible")
        self.assertTrue(Path(outputs["json"]).exists())
        self.assertTrue(Path(outputs["markdown"]).exists())
        self.assertTrue(Path(outputs["contactSheet"]).exists())
        markdown = Path(outputs["markdown"]).read_text(encoding="utf-8")
        self.assertIn("align", markdown)
        self.assertIn("`left/bottom`", markdown)
        self.assertIn("`38/114`", markdown)


if __name__ == "__main__":
    unittest.main()
