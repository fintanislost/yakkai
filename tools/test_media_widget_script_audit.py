import json
import tempfile
import unittest
from pathlib import Path

from tools.media_widget_script_audit import audit_scene, classify_script


class MediaWidgetScriptAuditTests(unittest.TestCase):
    def test_classifies_media_runtime_apis(self):
        script = """
        export function mediaPlaybackChanged(event) {
            thisLayer.visible = event.state !== MediaPlaybackEvent.PLAYBACK_STOPPED;
            thisObject.getAnimation().play();
            const parent = thisLayer.getParent();
            const layer = thisScene.getLayer("Artist Name R");
            engine.setTimeout(() => { thisLayer.visible = true; }, 50);
            return layer.getTransformMatrix().m[12] > 100;
        }
        """
        usage = classify_script(script)
        self.assertIn("mediaPlaybackChanged", usage["callbacks"])
        self.assertIn("thisLayer.visible", usage["sideEffects"])
        self.assertIn("thisObject.getAnimation", usage["apis"])
        self.assertIn("thisLayer.getParent", usage["apis"])
        self.assertIn("thisScene.getLayer", usage["apis"])
        self.assertIn("getTransformMatrix", usage["apis"])
        self.assertIn("engine.setTimeout", usage["apis"])

    def test_audit_extracts_nested_layer_and_effect_scripts(self):
        scene = {
            "objects": [
                {
                    "id": 10,
                    "name": "Media Root",
                    "visible": {
                        "value": False,
                        "script": "export function mediaPlaybackChanged(event) { thisLayer.visible = event.state == 1; }",
                    },
                    "color": {
                        "value": "1 1 1",
                        "script": "export function mediaThumbnailChanged(event) { return event.primaryColor; }",
                    },
                    "effects": [
                        {
                            "id": 20,
                            "passes": [
                                {
                                    "id": 21,
                                    "constantshadervalues": {
                                        "alpha": {
                                            "value": 0,
                                            "script": "export function update() { return 1; }",
                                        }
                                    },
                                }
                            ],
                        }
                    ],
                }
            ]
        }
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "scene.json"
            path.write_text(json.dumps(scene), encoding="utf-8")
            result = audit_scene(path)
        fields = {(entry["layerId"], entry["field"]) for entry in result["scripts"]}
        self.assertIn((10, "visible"), fields)
        self.assertIn((10, "color"), fields)
        self.assertIn((10, "effects[20].passes[21].constantshadervalues.alpha"), fields)


if __name__ == "__main__":
    unittest.main()
