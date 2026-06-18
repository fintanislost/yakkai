import json
import shutil
import sys
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

import arona_media_parity_fixture as fixture


class AronaMediaParityFixtureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path.cwd() / "tmp" / "arona-media-parity-fixture-test"
        shutil.rmtree(self.root, ignore_errors=True)
        self.root.mkdir(parents=True, exist_ok=True)
        self.references = self.root / "refs"
        self.references.mkdir()
        Image.new("RGBA", (20, 10), (10, 20, 30, 255)).save(self.references / "playing.png")
        Image.new("RGBA", (18, 8), (30, 20, 10, 255)).save(self.references / "paused.png")

    def write_fixture(self) -> Path:
        path = self.root / "fixture.json"
        path.write_text(
            json.dumps(
                {
                    "sceneId": "3228578419",
                    "renderSize": [40, 20],
                    "references": [
                        {
                            "name": "playing",
                            "reference": "playing.png",
                            "candidateCropRect": [0, 5, 20, 15],
                            "regions": [
                                {
                                    "name": "album-art",
                                    "purpose": "Album art footprint",
                                    "windowsPixelRect": [2, 2, 10, 8],
                                    "yakkaiPixelRect": [3, 1, 11, 7],
                                    "sampleRects": [
                                        {
                                            "name": "frame-edge",
                                            "purpose": "Frame tint sample",
                                            "windowsPixelRect": [2, 2, 6, 8],
                                            "yakkaiPixelRect": [3, 1, 7, 7],
                                        }
                                    ],
                                    "featureDetection": {
                                        "kind": "bright",
                                        "minLuma": 180,
                                        "minAlpha": 1,
                                    },
                                }
                            ],
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        return path

    def test_writes_comparator_regions_from_pixel_rectangles(self):
        spec = fixture.load_fixture(self.write_fixture())
        output = self.root / "out"

        summary = fixture.write_fixture_outputs(
            spec,
            reference_root=self.references,
            output_dir=output,
        )

        regions = json.loads((output / "regions" / "playing.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["references"][0]["referenceSize"], [20, 10])
        self.assertEqual(regions["regions"][0]["windowsRect"], [0.1, 0.2, 0.5, 0.8])
        self.assertEqual(regions["regions"][0]["yakkaiRect"], [0.15, 0.1, 0.55, 0.7])
        self.assertEqual(regions["regions"][0]["sampleRects"][0]["name"], "frame-edge")
        self.assertEqual(regions["regions"][0]["sampleRects"][0]["purpose"], "Frame tint sample")
        self.assertEqual(regions["regions"][0]["sampleRects"][0]["windowsRect"], [0.1, 0.2, 0.3, 0.8])
        self.assertEqual(regions["regions"][0]["sampleRects"][0]["yakkaiRect"], [0.15, 0.1, 0.35, 0.7])
        self.assertEqual(regions["regions"][0]["featureDetection"]["kind"], "bright")

    def test_rejects_bad_sample_rectangles(self):
        spec_path = self.write_fixture()
        data = json.loads(spec_path.read_text(encoding="utf-8"))
        data["references"][0]["regions"][0]["sampleRects"][0]["windowsPixelRect"] = [2, 2, 2, 8]
        spec_path.write_text(json.dumps(data), encoding="utf-8")

        with self.assertRaisesRegex(ValueError, "sample frame-edge windowsPixelRect"):
            fixture.load_fixture(spec_path)

    def test_crops_candidate_frame_to_reference_size(self):
        spec = fixture.load_fixture(self.write_fixture())
        candidate = self.root / "candidate.png"
        Image.new("RGBA", (40, 20), (0, 0, 0, 255)).save(candidate)
        output = self.root / "out"

        summary = fixture.write_fixture_outputs(
            spec,
            reference_root=self.references,
            output_dir=output,
            candidate_frame=candidate,
        )

        with Image.open(output / "crops" / "playing.png") as crop:
            self.assertEqual(crop.size, (20, 10))
        self.assertEqual(summary["references"][0]["candidateCrop"], "crops/playing.png")

    def test_rejects_candidate_frame_with_wrong_render_size(self):
        spec = fixture.load_fixture(self.write_fixture())
        candidate = self.root / "candidate-small.png"
        Image.new("RGBA", (39, 20), (0, 0, 0, 255)).save(candidate)

        with self.assertRaisesRegex(ValueError, "candidate frame size"):
            fixture.write_fixture_outputs(
                spec,
                reference_root=self.references,
                output_dir=self.root / "out",
                candidate_frame=candidate,
            )

    def test_filters_references_by_name(self):
        path = self.root / "fixture-filter.json"
        path.write_text(
            json.dumps(
                {
                    "sceneId": "3228578419",
                    "renderSize": [40, 20],
                    "references": [
                        {
                            "name": "playing",
                            "reference": "playing.png",
                            "candidateCropRect": [0, 5, 20, 15],
                            "regions": [
                                {
                                    "name": "album-art",
                                    "windowsPixelRect": [2, 2, 10, 8],
                                    "yakkaiPixelRect": [2, 2, 10, 8],
                                }
                            ],
                        },
                        {
                            "name": "paused",
                            "reference": "paused.png",
                            "candidateCropRect": [5, 6, 23, 14],
                            "regions": [
                                {
                                    "name": "clock",
                                    "windowsPixelRect": [3, 1, 12, 7],
                                    "yakkaiPixelRect": [3, 1, 12, 7],
                                }
                            ],
                        },
                    ],
                }
            ),
            encoding="utf-8",
        )
        spec = fixture.load_fixture(path)

        summary = fixture.write_fixture_outputs(
            spec,
            reference_root=self.references,
            output_dir=self.root / "out",
            reference_names={"paused"},
        )

        self.assertEqual([row["name"] for row in summary["references"]], ["paused"])
        self.assertFalse((self.root / "out" / "regions" / "playing.json").exists())
        self.assertTrue((self.root / "out" / "regions" / "paused.json").exists())

    def test_cli_accepts_repeated_reference_filters(self):
        args = fixture.parse_args(
            [
                "--output-dir",
                str(self.root / "out"),
                "--reference",
                "duringPlayback",
                "--reference",
                "noPlaybackClock",
            ]
        )

        self.assertEqual(args.references, ["duringPlayback", "noPlaybackClock"])


if __name__ == "__main__":
    unittest.main()
