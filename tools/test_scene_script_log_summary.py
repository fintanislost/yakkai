import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class SceneScriptLogSummaryTests(unittest.TestCase):
    def test_groups_script_gaps_by_kind_api_and_layer(self):
        with tempfile.TemporaryDirectory() as temp:
            log_path = Path(temp) / "validate.log"
            log_path.write_text(
                "\n".join(
                    [
                        "INFO SceneScript gap: layer=140 class=visible api=scene reason=missing-runtime-api message=ReferenceError: 'scene' is not defined",
                        "INFO SceneScript gap: layer=175 class=visible api=undefined.x reason=missing-layer-object-property message=TypeError: cannot read property 'x' of undefined",
                        "INFO SceneScript gap: layer=175 class=media-runtime-only api=MediaPlaybackEvent reason=media-runtime-only message=ReferenceError: 'MediaPlaybackEvent' is not defined",
                        "INFO SceneScript gap: layer=200 class=harmless api=console reason=diagnostic-only message=ReferenceError: 'console' is not defined",
                        "INFO suppressing unsupported media integration image layer: name=Album Cover id=175 image=models/util/solidlayer.json",
                        "INFO QuickJS binding: id=4995 origin=(3383,1555,0)",
                    ]
                ),
                encoding="utf-8",
            )

            completed = subprocess.run(
                [sys.executable, "tools/scene-script-log-summary.py", str(log_path)],
                cwd=Path(__file__).resolve().parents[1],
                check=True,
                text=True,
                capture_output=True,
            )

        output = completed.stdout
        self.assertIn("scene-script-bindings=1", output)
        self.assertIn("scene-script-gap-counts:", output)
        self.assertIn("  - visible: 1", output)
        self.assertIn("  - media-runtime-only: 3", output)
        self.assertIn("  - harmless: 1", output)
        self.assertIn("scene-script-gap-apis:", output)
        self.assertIn("  - scene: 1", output)
        self.assertIn("  - MediaPlaybackEvent: 1", output)
        self.assertIn("  - undefined.x: 1", output)
        self.assertIn("  - media-integration-layer: 1", output)
        self.assertIn("scene-script-gap-layers:", output)
        self.assertIn("  - 140 class=visible api=scene count=1", output)
        self.assertIn("  - 175 class=media-runtime-only api=undefined.x count=1", output)
        self.assertIn("  - 175 class=media-runtime-only api=MediaPlaybackEvent count=1", output)
        self.assertIn("unsupported-media-integration-layers=1", output)

    def test_counts_text_bindings_by_property(self):
        with tempfile.TemporaryDirectory() as temp:
            log_path = Path(temp) / "validate.log"
            log_path.write_text(
                "\n".join(
                    [
                        "INFO QuickJS binding: id=101 text=12:34",
                        "INFO QuickJS binding: id=102 text=June 7",
                    ]
                ),
                encoding="utf-8",
            )

            completed = subprocess.run(
                [sys.executable, "tools/scene-script-log-summary.py", str(log_path)],
                cwd=Path(__file__).resolve().parents[1],
                check=True,
                text=True,
                capture_output=True,
            )

        output = completed.stdout
        self.assertIn("scene-script-bindings=2", output)
        self.assertIn("  - text: 2", output)


if __name__ == "__main__":
    unittest.main()
