import json
import shutil
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import media_widget_matrix


class MediaWidgetMatrixTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path.cwd() / "tmp" / "media-widget-matrix-test"
        shutil.rmtree(self.root, ignore_errors=True)
        self.root.mkdir(parents=True, exist_ok=True)

    def test_builds_scene_properties_from_fixture_and_candidate_properties(self):
        fixture = (
            Path.cwd()
            / "native"
            / "scene_harness"
            / "tests"
            / "fixtures"
            / "media"
            / "instalock.mp3"
        )
        row = {
            "sceneId": "3228578419",
            "title": "Arona",
            "source": "/tmp/scene.pkg",
            "classification": "metadata-widget",
            "suggestedProperties": {
                "timeofday": "1",
                "mediaintegration": "1",
                "hidemediaintegration": False,
            },
        }

        scene_properties = media_widget_matrix.build_media_scene_properties(
            row,
            fixture,
            self.root,
        )

        self.assertEqual(scene_properties["timeofday"], {"value": "1"})
        self.assertEqual(scene_properties["mediaintegration"], {"value": "1"})
        self.assertEqual(scene_properties["hidemediaintegration"], {"value": False})
        self.assertEqual(scene_properties["__yakkaiMedia"]["title"], "Instalock")
        self.assertEqual(scene_properties["__yakkaiMedia"]["artist"], "WYLTK")
        self.assertTrue(Path(scene_properties["__yakkaiMedia"]["albumArtPath"]).exists())

    def test_selects_metadata_candidates_before_property_only_and_audio_reactive(self):
        rows = [
            {"sceneId": "1", "classification": "none"},
            {"sceneId": "2", "classification": "audio-reactive", "source": "/tmp/2.pkg"},
            {"sceneId": "3", "classification": "property-only-media", "source": "/tmp/3.pkg"},
            {"sceneId": "4", "classification": "metadata-widget", "source": "/tmp/4.pkg"},
        ]

        selected = media_widget_matrix.select_candidates(rows, limit=3)

        self.assertEqual([row["sceneId"] for row in selected], ["4", "3", "2"])

    def test_run_command_times_out_and_records_log(self):
        log_path = self.root / "timeout.log"

        exit_code = media_widget_matrix.run_command(
            [
                sys.executable,
                "-c",
                "import time; print('started'); time.sleep(2)",
            ],
            log_path,
            timeout_seconds=0.1,
        )

        self.assertEqual(exit_code, media_widget_matrix.TIMEOUT_EXIT_CODE)
        self.assertIn("timed out", log_path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
