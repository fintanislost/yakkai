import re
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parent / "validate-scene.sh"


class ValidateSceneScriptTests(unittest.TestCase):
    def test_default_artifact_root_is_repo_local_and_overridable(self):
        text = SCRIPT.read_text(encoding="utf-8")

        self.assertIn('OUTDIR="${YAKKAI_VALIDATE_OUTDIR:-smoke-tests/artifacts/tmp/yakkai-debug}"', text)
        self.assertNotRegex(text, re.compile(r'^OUTDIR="/tmp/yakkai-debug"$', re.MULTILINE))


if __name__ == "__main__":
    unittest.main()
