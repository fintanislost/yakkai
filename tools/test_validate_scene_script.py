import re
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parent / "validate-scene.sh"


class ValidateSceneScriptTests(unittest.TestCase):
    def test_default_artifact_root_is_repo_local_and_overridable(self):
        text = SCRIPT.read_text(encoding="utf-8")

        self.assertIn('OUTDIR="${YAKKAI_VALIDATE_OUTDIR:-smoke-tests/artifacts/tmp/yakkai-debug}"', text)
        self.assertNotRegex(text, re.compile(r'^OUTDIR="/tmp/yakkai-debug"$', re.MULTILINE))

    def test_empty_log_abort_reports_display_sandbox_hint(self):
        text = SCRIPT.read_text(encoding="utf-8")

        self.assertIn('[ "$HARNESS_STATUS" -eq 134 ] && [ ! -s "$LOG" ]', text)
        self.assertIn("Qt/Wayland display access may be blocked", text)

    def test_scene_script_warning_depends_on_actual_gap_count(self):
        text = SCRIPT.read_text(encoding="utf-8")

        self.assertIn('if [ "$SCRIPT_GAPS" -gt 0 ]; then', text)
        self.assertNotIn('if [ "$SCRIPT_VISIBLE" -gt 0 ]; then', text)


if __name__ == "__main__":
    unittest.main()
