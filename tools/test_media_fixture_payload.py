import json
import shutil
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import media_fixture_payload


class MediaFixturePayloadTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path.cwd() / "tmp" / "yakkai-media-fixture-payload-test"
        shutil.rmtree(self.root, ignore_errors=True)
        self.root.mkdir(parents=True, exist_ok=True)

    def test_extracts_instalock_metadata_and_album_art_for_scene_properties(self):
        fixture = (
            Path.cwd()
            / "native"
            / "scene_harness"
            / "tests"
            / "fixtures"
            / "media"
            / "instalock.mp3"
        )

        scene_properties = media_fixture_payload.build_scene_properties(
            fixture,
            self.root,
            {
                "timeofday": "1",
                "mediaintegration": "1",
                "media": True,
            },
        )

        self.assertEqual(scene_properties["timeofday"], {"value": "1"})
        self.assertEqual(scene_properties["mediaintegration"], {"value": "1"})
        self.assertEqual(scene_properties["media"], {"value": True})

        media = scene_properties["__yakkaiMedia"]
        self.assertTrue(media["available"])
        self.assertTrue(media["playing"])
        self.assertEqual(media["title"], "Instalock")
        self.assertEqual(media["artist"], "WYLTK")
        self.assertEqual(media["album"], "Instalock")
        self.assertAlmostEqual(media["duration"], 157.648922, places=3)
        self.assertEqual(media["position"], 42)

        album_art = Path(media["albumArtPath"])
        self.assertTrue(album_art.exists())
        self.assertEqual(album_art.parent, self.root)
        self.assertEqual(album_art.suffix, ".png")

        encoded = json.dumps(scene_properties)
        self.assertIn("__yakkaiMedia", encoded)
        self.assertIn(str(album_art), encoded)

    def test_cli_property_parser_preserves_numeric_property_values_as_strings(self):
        self.assertEqual(
            media_fixture_payload.parse_property("timeofday=1"),
            ("timeofday", "1"),
        )
        self.assertEqual(
            media_fixture_payload.parse_property("media=true"),
            ("media", True),
        )

    def test_position_ratio_derives_position_from_media_duration(self):
        fixture = (
            Path.cwd()
            / "native"
            / "scene_harness"
            / "tests"
            / "fixtures"
            / "media"
            / "instalock.mp3"
        )

        scene_properties = media_fixture_payload.build_scene_properties(
            fixture,
            self.root,
            position_ratio=0.5,
        )

        media = scene_properties["__yakkaiMedia"]
        self.assertAlmostEqual(media["position"], media["duration"] * 0.5, places=4)

    def test_position_ratio_rejects_values_outside_unit_range(self):
        fixture = (
            Path.cwd()
            / "native"
            / "scene_harness"
            / "tests"
            / "fixtures"
            / "media"
            / "instalock.mp3"
        )

        with self.assertRaisesRegex(ValueError, "position_ratio"):
            media_fixture_payload.build_scene_properties(
                fixture,
                self.root,
                position_ratio=1.5,
            )

    def test_can_build_paused_media_state_with_fixed_clock(self):
        fixture = (
            Path.cwd()
            / "native"
            / "scene_harness"
            / "tests"
            / "fixtures"
            / "media"
            / "instalock.mp3"
        )

        scene_properties = media_fixture_payload.build_scene_properties(
            fixture,
            self.root,
            playing=False,
            clock_time="09:22",
        )

        media = scene_properties["__yakkaiMedia"]
        self.assertTrue(media["available"])
        self.assertFalse(media["playing"])
        self.assertEqual(media["clockTime"], "09:22")


if __name__ == "__main__":
    unittest.main()
