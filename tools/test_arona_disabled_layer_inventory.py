import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import tools.arona_disabled_layer_inventory as inventory


def _repo_root():
    return Path(__file__).resolve().parents[1]


def _repo_tempdir():
    root = _repo_root() / "smoke-tests" / "artifacts" / "tmp"
    root.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(dir=root)


class AronaDisabledLayerInventoryTests(unittest.TestCase):
    def test_classifies_literal_false_visual_object(self):
        obj = {
            "id": 306,
            "name": "Shooting_Star_01",
            "visible": False,
            "image": "materials/star.png",
            "effects": [],
        }

        record = inventory.classify_scene_object(obj)

        self.assertEqual(record["visibilityKind"], "authored-off")
        self.assertEqual(record["candidateBucket"], "unknown-visual")
        self.assertEqual(record["reviewPriority"], "review")

    def test_classifies_time_of_day_script(self):
        obj = {
            "id": 240,
            "name": "Blinking Stars_01",
            "visible": {
                "value": False,
                "script": "export function update(value) { value = shared.shownight; return value; }",
            },
            "effects": [],
        }

        record = inventory.classify_scene_object(obj)

        self.assertEqual(record["visibilityKind"], "script-gated")
        self.assertEqual(record["candidateBucket"], "time-of-day")
        self.assertEqual(record["scriptSignals"], ["shared.shownight"])

    def test_classifies_media_runtime_script(self):
        obj = {
            "id": 620,
            "name": "Text Container",
            "visible": {
                "value": True,
                "script": "export function mediaPlaybackChanged(event) { thisLayer.visible = event.state !== MediaPlaybackEvent.PLAYBACK_STOPPED; }",
            },
        }

        record = inventory.classify_scene_object(obj)

        self.assertEqual(record["candidateBucket"], "media-runtime")
        self.assertEqual(record["reviewPriority"], "defer")

    def test_classifies_object_user_bindings_by_name(self):
        cases = [
            (
                {
                    "id": 125,
                    "name": "triangle-Held",
                    "visible": {"user": {"condition": "1", "name": "mousecursor"}, "value": False},
                },
                "ui-user-setting",
                "mousecursor",
            ),
            (
                {
                    "id": 295,
                    "name": "Rain perspective",
                    "visible": {"user": {"condition": "1", "name": "weather"}, "value": False},
                },
                "weather-rain",
                "weather",
            ),
            (
                {
                    "id": 522,
                    "name": "Round Text",
                    "visible": {"user": {"condition": "1", "name": "mediaintegration"}, "value": True},
                },
                "media-runtime",
                "mediaintegration",
            ),
        ]

        for obj, bucket, binding in cases:
            with self.subTest(bucket=bucket):
                record = inventory.classify_scene_object(obj)

                self.assertEqual(record["visibilityKind"], "user-gated")
                self.assertEqual(record["candidateBucket"], bucket)
                self.assertEqual(record["reviewPriority"], "defer")
                self.assertEqual(record["userBinding"], binding)

    def test_classifies_scene_script_utility_as_ignore(self):
        obj = {
            "id": 801,
            "name": "Solid==DAY NIGHT SCRIPT==",
            "visible": False,
            "image": "models/util/solidlayer.json",
            "effects": [{"name": "lut"}],
        }

        record = inventory.classify_scene_object(obj)

        self.assertEqual(record["candidateBucket"], "scene-script-utility")
        self.assertEqual(record["reviewPriority"], "ignore")

    def test_classifies_unknown_gated_visual_for_review(self):
        obj = {
            "id": 469,
            "name": "1st flare",
            "visible": {"user": "lensflare", "value": False},
            "image": "models/workshop/first-lens.json",
        }

        record = inventory.classify_scene_object(obj)

        self.assertEqual(record["candidateBucket"], "unknown-gated")
        self.assertEqual(record["reviewPriority"], "review")

    def test_does_not_classify_rainbow_lens_flare_as_weather(self):
        obj = {
            "id": 485,
            "name": "60-604156_flare17-rainbow-lens-flare-png",
            "visible": {"user": "lensflare", "value": False},
            "image": "models/workshop/lens.json",
        }

        record = inventory.classify_scene_object(obj)

        self.assertEqual(record["candidateBucket"], "unknown-gated")
        self.assertEqual(record["reviewPriority"], "review")

    def test_report_sorts_by_priority_then_layer_id(self):
        scene = {
            "objects": [
                {"id": 30, "name": "Solid==DAY NIGHT SCRIPT==", "visible": False},
                {"id": 20, "name": "Clock", "visible": {"user": "clock", "value": True}},
                {"id": 10, "name": "Off", "visible": False},
            ]
        }

        report = inventory.build_report(scene, {})

        self.assertEqual([layer["layerId"] for layer in report["layers"]], [10, 20, 30])

    def test_joins_manifest_puppet_animation_layers(self):
        scene = {
            "objects": [
                {"id": 405, "name": "ARONA_CROP_SHEET", "visible": True, "puppet": "models/arona.mdl"}
            ]
        }
        manifest = {
            "strippedCandidates": [],
            "protectedPuppetDiagnostics": [],
            "captures": [
                {
                    "stage": "effect-input",
                    "layer": {
                        "layerId": 405,
                        "layerName": "ARONA_CROP_SHEET",
                        "puppetAnimationLayerInventory": [
                            {"animationId": 478, "animationName": "Idle", "visible": False, "activeBoneSlots": []},
                            {"animationId": 781, "animationName": "Arona Drool", "visible": True, "activeBoneSlots": [2]},
                        ],
                    },
                }
            ],
        }

        report = inventory.build_report(scene, {"day": manifest})

        layer = next(item for item in report["layers"] if item["layerId"] == 405)
        self.assertEqual(layer["manifest"]["day"]["puppetAnimations"][0]["animationId"], 478)
        self.assertEqual(layer["manifest"]["day"]["puppetAnimations"][0]["visibilityKind"], "inactive-puppet-animation")

    def test_joins_real_shaped_top_level_manifest_puppet_inventory(self):
        scene = {
            "objects": [
                {"id": 405, "name": "ARONA_CROP_SHEET", "visible": True, "puppet": "models/arona.mdl"}
            ]
        }
        manifest = {
            "strippedCandidates": [],
            "protectedPuppetDiagnostics": [],
            "captures": [],
            "puppetAnimationLayerInventory": [
                {
                    "layerId": 405,
                    "layerName": "ARONA_CROP_SHEET",
                    "puppetAnimationLayers": [
                        {
                            "animationId": 478,
                            "animationName": "Idle",
                            "visible": False,
                            "activeBoneSlotCount": 1,
                            "activeBoneSlots": [7],
                            "visibleAndWeighted": False,
                        },
                        {
                            "animationId": 781,
                            "animationName": "Arona Drool",
                            "visible": True,
                            "activeBoneSlotCount": 1,
                            "activeBoneSlots": [2],
                            "visibleAndWeighted": True,
                        },
                    ],
                }
            ],
        }

        report = inventory.build_report(scene, {"day": manifest})

        layer = next(item for item in report["layers"] if item["layerId"] == 405)
        self.assertEqual(layer["reviewPriority"], "review")
        self.assertEqual(layer["manifest"]["day"]["layerName"], "ARONA_CROP_SHEET")
        self.assertEqual(layer["manifest"]["day"]["puppetAnimations"][0]["animationId"], 478)
        self.assertTrue(layer["manifest"]["day"]["puppetAnimations"][0]["hasWeightedMotion"])

    def test_markdown_renders_priority_sections(self):
        report = {
            "summary": {"review": 1, "defer": 1, "ignore": 0},
            "layers": [
                {
                    "layerId": 306,
                    "name": "Shooting_Star_01",
                    "candidateBucket": "unknown-visual",
                    "reviewPriority": "review",
                    "visibilityKind": "authored-off",
                    "reason": "literal false visual object",
                    "effectsCount": 0,
                    "image": "materials/star.png",
                    "manifest": {},
                },
                {
                    "layerId": 620,
                    "name": "Text Container",
                    "candidateBucket": "media-runtime",
                    "reviewPriority": "defer",
                    "visibilityKind": "script-gated",
                    "reason": "media runtime layer",
                    "effectsCount": 0,
                    "image": "",
                    "manifest": {},
                },
            ],
        }

        markdown = inventory.render_markdown(report)

        self.assertIn("## Review Candidates", markdown)
        self.assertIn("Shooting_Star_01", markdown)
        self.assertIn("## Deferred / Intentional", markdown)
        self.assertIn("Text Container", markdown)
        self.assertIn("priority=review", markdown)
        self.assertIn("effects=0", markdown)
        self.assertIn("image=materials/star.png", markdown)

    def test_cli_prints_default_json(self):
        scene = {"objects": [{"id": 306, "name": "Shooting_Star_01", "visible": False}]}

        with _repo_tempdir() as temp:
            scene_path = Path(temp) / "scene.json"
            scene_path.write_text(json.dumps(scene), encoding="utf-8")

            completed = subprocess.run(
                [
                    sys.executable,
                    str(_repo_root() / "tools" / "arona_disabled_layer_inventory.py"),
                    "--scene",
                    str(scene_path),
                ],
                check=True,
                capture_output=True,
                text=True,
                cwd=_repo_root(),
            )

        payload = json.loads(completed.stdout)
        self.assertEqual(payload["layers"][0]["layerId"], 306)

    def test_cli_writes_json_and_markdown_outputs(self):
        scene = {"objects": [{"id": 306, "name": "Shooting_Star_01", "visible": False}]}

        with _repo_tempdir() as temp:
            temp_path = Path(temp)
            scene_path = temp_path / "scene.json"
            output_json = temp_path / "inventory.json"
            output_md = temp_path / "inventory.md"
            scene_path.write_text(json.dumps(scene), encoding="utf-8")

            subprocess.run(
                [
                    sys.executable,
                    str(_repo_root() / "tools" / "arona_disabled_layer_inventory.py"),
                    "--scene",
                    str(scene_path),
                    "--output-json",
                    str(output_json),
                    "--output-md",
                    str(output_md),
                ],
                check=True,
                capture_output=True,
                text=True,
                cwd=_repo_root(),
            )

            self.assertEqual(json.loads(output_json.read_text(encoding="utf-8"))["summary"]["review"], 1)
            self.assertIn("Shooting_Star_01", output_md.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
