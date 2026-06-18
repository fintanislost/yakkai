import json
import shutil
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import media_widget_candidate_inventory as inventory


class MediaWidgetCandidateInventoryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path.cwd() / "tmp" / "media-widget-candidate-inventory-test"
        shutil.rmtree(self.root, ignore_errors=True)
        self.root.mkdir(parents=True, exist_ok=True)

    def write_project(self, scene_id: str, properties: dict) -> Path:
        folder = self.root / scene_id
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "project.json").write_text(
            json.dumps(
                {
                    "title": f"Scene {scene_id}",
                    "type": "scene",
                    "general": {"properties": properties},
                }
            ),
            encoding="utf-8",
        )
        return folder

    def test_classifies_metadata_widget_from_scene_script_signatures(self):
        folder = self.write_project(
            "100",
            {
                "mediaintegration": {"type": "combo", "value": "0"},
                "hidemediaintegration": {"type": "bool", "value": True},
            },
        )
        (folder / "scene.json").write_text(
            json.dumps(
                {
                    "objects": [
                        {
                            "script": "function mediaPropertiesChanged(event) { return event.title; }",
                        },
                        {"name": "$mediaThumbnail"},
                    ]
                }
            ),
            encoding="utf-8",
        )

        row = inventory.inspect_candidate(folder)

        self.assertEqual(row["sceneId"], "100")
        self.assertEqual(row["classification"], "metadata-widget")
        self.assertIn("mediaPropertiesChanged", row["scriptSignatures"])
        self.assertIn("$mediaThumbnail", row["scriptSignatures"])
        self.assertEqual(row["suggestedProperties"]["mediaintegration"], "1")
        self.assertEqual(row["suggestedProperties"]["hidemediaintegration"], False)

    def test_classifies_audio_reactive_separately_from_metadata_widgets(self):
        folder = self.write_project("101", {"music": {"type": "bool", "value": False}})
        (folder / "scene.json").write_text(
            json.dumps({"script": "let b = engine.registerAudioBuffers(64);"}),
            encoding="utf-8",
        )

        row = inventory.inspect_candidate(folder)

        self.assertEqual(row["classification"], "audio-reactive")
        self.assertIn("engine.registerAudioBuffers", row["scriptSignatures"])
        self.assertEqual(row["suggestedProperties"]["music"], True)

    def test_classifies_property_only_media_when_no_scene_script_is_available(self):
        folder = self.write_project(
            "102",
            {"mediaintegrationopacity": {"type": "slider", "value": 1}},
        )

        row = inventory.inspect_candidate(folder)

        self.assertEqual(row["classification"], "property-only-media")
        self.assertIn("mediaintegrationopacity", row["mediaPropertyKeys"])


if __name__ == "__main__":
    unittest.main()
