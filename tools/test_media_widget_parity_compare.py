import json
import shutil
import sys
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

import media_widget_parity_compare as compare


class MediaWidgetParityCompareTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path.cwd() / "tmp" / "media-widget-parity-test"
        shutil.rmtree(self.root, ignore_errors=True)
        self.root.mkdir(parents=True, exist_ok=True)

    def write_image(self, name: str, color: tuple[int, int, int, int]) -> Path:
        path = self.root / name
        Image.new("RGBA", (20, 10), color).save(path)
        return path

    def test_rmse_is_zero_for_identical_images(self):
        image = Image.new("RGBA", (4, 4), (10, 20, 30, 255))
        self.assertEqual(compare.rmse(image, image), 0.0)

    def test_rmse_increases_for_different_pixels(self):
        left = Image.new("RGBA", (2, 2), (0, 0, 0, 255))
        right = Image.new("RGBA", (2, 2), (10, 0, 0, 255))
        self.assertGreater(compare.rmse(left, right), 0.0)

    def test_normalized_rect_converts_to_pixel_rect(self):
        rect = compare.normalized_rect_to_pixels([0.25, 0.2, 0.75, 0.8], (200, 100))
        self.assertEqual(rect, (50, 20, 150, 80))

    def test_region_metrics_preserve_region_name_and_dimensions(self):
        windows = Image.new("RGBA", (20, 10), (0, 0, 0, 255))
        yakkai = Image.new("RGBA", (20, 10), (20, 0, 0, 255))
        region = {
            "name": "text-block",
            "windowsRect": [0.0, 0.0, 0.5, 1.0],
            "yakkaiRect": [0.0, 0.0, 0.5, 1.0],
        }

        result = compare.compare_region(windows, yakkai, region)

        self.assertEqual(result["name"], "text-block")
        self.assertEqual(result["windowsPixelRect"], [0, 0, 10, 10])
        self.assertEqual(result["yakkaiPixelRect"], [0, 0, 10, 10])
        self.assertGreater(result["rmse"], 0.0)

    def test_region_metrics_include_named_sample_rects(self):
        windows = Image.new("RGBA", (20, 10), (10, 20, 30, 255))
        yakkai = Image.new("RGBA", (20, 10), (15, 18, 40, 255))
        region = {
            "name": "album-art",
            "windowsRect": [0.0, 0.0, 1.0, 1.0],
            "yakkaiRect": [0.0, 0.0, 1.0, 1.0],
            "sampleRects": [
                {
                    "name": "top-frame",
                    "windowsRect": [0.0, 0.0, 0.5, 1.0],
                    "yakkaiRect": [0.0, 0.0, 0.5, 1.0],
                }
            ],
        }

        result = compare.compare_region(windows, yakkai, region)

        self.assertEqual(len(result["samples"]), 1)
        sample = result["samples"][0]
        self.assertEqual(sample["name"], "top-frame")
        self.assertEqual(sample["windowsPixelRect"], [0, 0, 10, 10])
        self.assertEqual(sample["yakkaiPixelRect"], [0, 0, 10, 10])
        self.assertEqual(sample["meanDeltaRgba"], [5.0, -2.0, 10.0, 0.0])
        self.assertGreater(sample["rmse"], 0.0)

    def test_bright_feature_bounds_detect_visible_text_area(self):
        image = Image.new("RGBA", (12, 8), (20, 20, 20, 255))
        for x in range(3, 8):
            for y in range(2, 5):
                image.putpixel((x, y), (240, 240, 240, 255))

        result = compare.detect_bright_feature_bounds(image, min_luma=200)

        self.assertEqual(result["pixelRect"], [3, 2, 8, 5])
        self.assertEqual(result["size"], [5, 3])
        self.assertEqual(result["center"], [5.5, 3.5])
        self.assertGreater(result["coverage"], 0.15)

    def test_region_metrics_include_requested_feature_delta(self):
        windows = Image.new("RGBA", (20, 10), (0, 0, 0, 255))
        yakkai = Image.new("RGBA", (20, 10), (0, 0, 0, 255))
        for x in range(2, 6):
            for y in range(2, 5):
                windows.putpixel((x, y), (255, 255, 255, 255))
        for x in range(5, 9):
            for y in range(4, 7):
                yakkai.putpixel((x, y), (255, 255, 255, 255))
        region = {
            "name": "text-block",
            "windowsRect": [0.0, 0.0, 1.0, 1.0],
            "yakkaiRect": [0.0, 0.0, 1.0, 1.0],
            "featureDetection": {"kind": "bright", "minLuma": 200},
        }

        result = compare.compare_region(windows, yakkai, region)

        self.assertEqual(result["windowsFeatureBounds"]["pixelRect"], [2, 2, 6, 5])
        self.assertEqual(result["yakkaiFeatureBounds"]["pixelRect"], [5, 4, 9, 7])
        self.assertEqual(result["featureDelta"]["center"], [3.0, 2.0])

    def test_integer_alignment_finds_shifted_candidate(self):
        windows = Image.new("RGBA", (24, 16), (0, 0, 0, 255))
        candidate = Image.new("RGBA", (24, 16), (0, 0, 0, 255))
        for x in range(7, 14):
            for y in range(4, 10):
                windows.putpixel((x, y), (240, 240, 240, 255))
                candidate.putpixel((x - 3, y + 2), (240, 240, 240, 255))

        result = compare.find_best_integer_alignment(windows, candidate, max_offset=6)

        self.assertEqual(result["offset"], [3, -2])
        self.assertLess(result["alignedRmse"], result["originalRmse"])

    def test_translate_image_keeps_canvas_size_without_wrapping(self):
        image = Image.new("RGBA", (4, 3), (0, 0, 0, 0))
        image.putpixel((0, 1), (255, 255, 255, 255))

        shifted = compare.translate_image(image, (2, -1))

        self.assertEqual(shifted.size, image.size)
        self.assertEqual(shifted.getpixel((2, 0)), (255, 255, 255, 255))
        self.assertEqual(shifted.getpixel((0, 1)), (0, 0, 0, 0))

    def test_difference_image_keeps_visible_alpha(self):
        left = Image.new("RGBA", (1, 1), (0, 0, 0, 255))
        right = Image.new("RGBA", (1, 1), (20, 0, 0, 255))

        pixel = compare._difference_image(left, right).getpixel((0, 0))

        self.assertGreater(pixel[0], 0)
        self.assertEqual(pixel[3], 255)

    def test_generated_text_diagnostics_are_summarized(self):
        diagnostics = self.root / "media-text-diagnostics.json"
        diagnostics.write_text(
            json.dumps(
                {
                    "texts": [
                        {
                            "layerId": 546,
                            "layerName": "Artist Name R",
                            "text": "TOMMY RICHMAN",
                            "status": "crop-written",
                            "cropRect": [100, 200, 300, 260],
                            "cardSize": [1166.0, 214.0],
                        },
                        {
                            "layerId": 12,
                            "layerName": "Other",
                            "text": "",
                            "status": "not-visible",
                        },
                    ]
                }
            ),
            encoding="utf-8",
        )

        summary = compare.load_text_diagnostics(diagnostics)

        self.assertEqual(len(summary), 1)
        self.assertEqual(summary[0]["layerId"], 546)
        self.assertEqual(summary[0]["text"], "TOMMY RICHMAN")

    def test_report_writes_json_markdown_and_contact_sheet(self):
        windows = self.write_image("windows.png", (0, 0, 0, 255))
        yakkai = self.write_image("yakkai.png", (10, 0, 0, 255))
        regions = self.root / "regions.json"
        regions.write_text(
            json.dumps(
                {
                    "regions": [
                        {
                            "name": "whole-widget",
                            "windowsRect": [0.0, 0.0, 1.0, 1.0],
                            "yakkaiRect": [0.0, 0.0, 1.0, 1.0],
                            "sampleRects": [
                                {
                                    "name": "top-frame",
                                    "purpose": "Frame tint sample",
                                    "windowsRect": [0.0, 0.0, 0.5, 0.5],
                                    "yakkaiRect": [0.0, 0.0, 0.5, 0.5],
                                }
                            ],
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        output = self.root / "report"

        exit_code = compare.main(
            [
                "--windows-reference",
                str(windows),
                "--yakkai-candidate",
                str(yakkai),
                "--regions-json",
                str(regions),
                "--output-dir",
                str(output),
                "--title",
                "Synthetic parity",
                "--dominant-mismatch",
                "layout",
                "--review-note",
                "Text and album art are offset.",
            ]
        )

        self.assertEqual(exit_code, 0)
        self.assertTrue((output / "summary.json").exists())
        self.assertTrue((output / "summary.md").exists())
        self.assertTrue((output / "contact-sheet.png").exists())
        summary = json.loads((output / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["dominantMismatch"], "layout")
        markdown = (output / "summary.md").read_text(encoding="utf-8")
        self.assertIn("Text and album art are offset.", markdown)
        self.assertIn("## Sample Metrics", markdown)
        self.assertIn("top-frame", markdown)

    def test_report_can_write_aligned_candidate(self):
        windows = Image.new("RGBA", (24, 16), (0, 0, 0, 255))
        yakkai = Image.new("RGBA", (24, 16), (0, 0, 0, 255))
        for x in range(7, 14):
            for y in range(4, 10):
                windows.putpixel((x, y), (240, 240, 240, 255))
                yakkai.putpixel((x - 3, y + 2), (240, 240, 240, 255))
        windows_path = self.root / "windows-shift.png"
        yakkai_path = self.root / "yakkai-shift.png"
        windows.save(windows_path)
        yakkai.save(yakkai_path)
        regions = self.root / "regions-shift.json"
        regions.write_text(
            json.dumps(
                {
                    "regions": [
                        {
                            "name": "whole-widget",
                            "windowsRect": [0.0, 0.0, 1.0, 1.0],
                            "yakkaiRect": [0.0, 0.0, 1.0, 1.0],
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        output = self.root / "aligned-report"

        exit_code = compare.main(
            [
                "--windows-reference",
                str(windows_path),
                "--yakkai-candidate",
                str(yakkai_path),
                "--regions-json",
                str(regions),
                "--output-dir",
                str(output),
                "--align-search-radius",
                "6",
            ]
        )

        self.assertEqual(exit_code, 0)
        summary = json.loads((output / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["alignment"]["offset"], [3, -2])
        self.assertTrue((output / "yakkai-candidate-aligned.png").exists())


if __name__ == "__main__":
    unittest.main()
