import io
import json
import tempfile
import unittest
import zipfile
from pathlib import Path
import sys

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

import arona_layer405_full_pass_align as align


def png_bytes(size, color):
    buffer = io.BytesIO()
    Image.new("RGBA", size, color).save(buffer, format="PNG")
    return buffer.getvalue()


def write_yakkai_capture_tree(root, variant, effect_color, material_color):
    capture_dir = root / variant / "effect-captures" / "3228578419" / "405_ARONA_CROP_SHEET"
    capture_dir.mkdir(parents=True, exist_ok=True)
    Image.new("RGBA", (8, 4), effect_color).save(capture_dir / "effect_input.tga")
    Image.new("RGBA", (8, 4), material_color).save(capture_dir / "material_output_1_0.tga")
    return capture_dir


def make_full_pass_archive(path):
    root = "layer405_full_pass_export"
    captures = []
    for variant, timeofday, base_event in (("day", "1", 100), ("sunset", "2", 200), ("night", "3", 300)):
        captures.append(
            {
                "variant": variant,
                "timeofday": timeofday,
                "variantLabelSource": "capture-session-label",
                "passes": [
                    {
                        "passOrder": 1,
                        "role": "effect-input-before-visible-effects",
                        "replayEventId": base_event,
                        "drawId": base_event + 1,
                        "pixelShaderResourceId": 10,
                        "rtvAfterFile": f"{variant}/event{base_event}_after.png",
                    },
                    {
                        "passOrder": 2,
                        "role": "internal-layer405-pass",
                        "replayEventId": base_event + 20,
                        "drawId": base_event + 21,
                        "pixelShaderResourceId": 20,
                        "rtvAfterFile": f"{variant}/event{base_event + 20}_after.png",
                    },
                ],
            }
        )
    manifest = {
        "status": "complete_full_layer405_pass_export_from_fresh_labeled_rdcs",
        "sceneId": "3228578419",
        "layerId": 405,
        "captures": captures,
    }
    with zipfile.ZipFile(path, "w") as zip_file:
        zip_file.writestr(f"{root}/source_manifest.json", json.dumps(manifest))
        for capture in captures:
            variant = capture["variant"]
            zip_file.writestr(f"{root}/{variant}/event{capture['passes'][0]['replayEventId']}_after.png", png_bytes((8, 4), (10, 20, 30, 255)))
            zip_file.writestr(f"{root}/{variant}/event{capture['passes'][1]['replayEventId']}_after.png", png_bytes((8, 4), (40, 50, 60, 255)))


class AronaLayer405FullPassAlignTests(unittest.TestCase):
    def test_aligns_windows_pass_order_to_yakkai_material_outputs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "full.zip"
            yakkai_root = root / "yakkai"
            output = root / "out"
            make_full_pass_archive(archive)
            for variant in ("day", "sunset", "night"):
                write_yakkai_capture_tree(yakkai_root, variant, (10, 20, 30, 255), (40, 50, 60, 255))

            report = align.align_full_pass_export(archive, yakkai_root, output, max_size=(16, 8))

            day_rows = [row for row in report["rows"] if row["variant"] == "day"]
            self.assertEqual(day_rows[0]["yakkaiStage"], "effect-input")
            self.assertEqual(day_rows[1]["yakkaiStage"], "material-output-1-0")
            self.assertEqual(day_rows[1]["classification"], "close")
            self.assertAlmostEqual(day_rows[1]["rmse"], 0.0, places=6)
            self.assertAlmostEqual(day_rows[1]["deltaCosine"], 1.0, places=6)
            self.assertTrue((output / "alignment.json").is_file())
            self.assertIn("## day", (output / "alignment.md").read_text(encoding="utf-8"))

    def test_reports_missing_yakkai_material_without_failing_archive_intake(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "full.zip"
            yakkai_root = root / "yakkai"
            make_full_pass_archive(archive)
            for variant in ("day", "sunset", "night"):
                write_yakkai_capture_tree(yakkai_root, variant, (10, 20, 30, 255), (40, 50, 60, 255))
            (yakkai_root / "sunset" / "effect-captures" / "3228578419" / "405_ARONA_CROP_SHEET" / "material_output_1_0.tga").unlink()

            report = align.align_full_pass_export(archive, yakkai_root, root / "out", max_size=(16, 8))

            sunset_missing = [
                row for row in report["rows"] if row["variant"] == "sunset" and row["yakkaiStage"] == "material-output-1-0"
            ][0]
            self.assertEqual(sunset_missing["classification"], "missing-yakkai-capture")
            self.assertIn("material_output_1_0.tga", sunset_missing["missingPath"])


if __name__ == "__main__":
    unittest.main()
