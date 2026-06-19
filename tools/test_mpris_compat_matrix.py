import json
import shutil
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mpris_compat_matrix


class FixtureMprisClient:
    def __init__(self, names, properties):
        self.names = names
        self.properties = properties

    def list_names(self):
        return self.names

    def get_property(self, service, property_name):
        value = self.properties.get((service, property_name))
        if isinstance(value, Exception):
            raise value
        return value


def metadata(title="Instalock", art_path=""):
    result = {
        "xesam:title": title,
        "xesam:artist": ["WYLTK"],
        "xesam:album": "Instalock",
        "mpris:length": 157649737,
    }
    if art_path:
        result["mpris:artUrl"] = art_path
    return result


class MprisCompatMatrixTests(unittest.TestCase):
    def setUp(self):
        self.root = Path.cwd() / "tmp" / "yakkai-mpris-compat-matrix-test"
        shutil.rmtree(self.root, ignore_errors=True)
        self.root.mkdir(parents=True, exist_ok=True)

    def test_builds_default_and_exact_service_probes(self):
        client = FixtureMprisClient(
            [
                "org.mpris.MediaPlayer2.Gwenview",
                "org.mpris.MediaPlayer2.vlc",
            ],
            {
                ("org.mpris.MediaPlayer2.Gwenview", "PlaybackStatus"): "Stopped",
                ("org.mpris.MediaPlayer2.Gwenview", "Metadata"): {},
                ("org.mpris.MediaPlayer2.Gwenview", "Position"): 0,
                ("org.mpris.MediaPlayer2.vlc", "PlaybackStatus"): "Playing",
                ("org.mpris.MediaPlayer2.vlc", "Metadata"): metadata(
                    art_path=f"file://{__file__}"
                ),
                ("org.mpris.MediaPlayer2.vlc", "Position"): 42000000,
            },
        )

        matrix = mpris_compat_matrix.build_matrix(client)

        self.assertEqual(
            [probe["name"] for probe in matrix["probes"]],
            [
                "default-selection",
                "exact-service: org.mpris.MediaPlayer2.Gwenview",
                "exact-service: org.mpris.MediaPlayer2.vlc",
            ],
        )
        self.assertEqual(matrix["summary"]["totalProbes"], 3)
        self.assertEqual(matrix["summary"]["okProbes"], 2)
        self.assertEqual(matrix["summary"]["availableProbes"], 2)
        self.assertEqual(matrix["summary"]["playingProbes"], 2)
        self.assertEqual(matrix["summary"]["issueCounts"], {"missing-metadata": 1})

        vlc_probe = matrix["probes"][2]
        self.assertEqual(vlc_probe["classification"]["status"], "Playing")
        self.assertTrue(vlc_probe["classification"]["localAlbumArtExists"])
        self.assertEqual(vlc_probe["snapshot"]["__yakkaiMedia"]["title"], "Instalock")

    def test_explicit_service_list_limits_exact_probes(self):
        client = FixtureMprisClient(
            [
                "org.mpris.MediaPlayer2.Gwenview",
                "org.mpris.MediaPlayer2.vlc",
            ],
            {
                ("org.mpris.MediaPlayer2.Gwenview", "PlaybackStatus"): "Stopped",
                ("org.mpris.MediaPlayer2.Gwenview", "Metadata"): {},
                ("org.mpris.MediaPlayer2.Gwenview", "Position"): 0,
                ("org.mpris.MediaPlayer2.vlc", "PlaybackStatus"): "Paused",
                ("org.mpris.MediaPlayer2.vlc", "Metadata"): metadata(),
                ("org.mpris.MediaPlayer2.vlc", "Position"): 1000000,
            },
        )

        matrix = mpris_compat_matrix.build_matrix(
            client,
            services=["org.mpris.MediaPlayer2.vlc"],
        )

        self.assertEqual(
            [probe["name"] for probe in matrix["probes"]],
            ["default-selection", "exact-service: org.mpris.MediaPlayer2.vlc"],
        )
        self.assertEqual(
            matrix["summary"]["issueCounts"],
            {
                "missing-art": 1,
                "missing-metadata": 1,
                "not-playing": 1,
            },
        )

    def test_classifies_unavailable_and_not_playing_states(self):
        client = FixtureMprisClient(
            ["org.mpris.MediaPlayer2.vlc"],
            {
                ("org.mpris.MediaPlayer2.vlc", "PlaybackStatus"): "Stopped",
                ("org.mpris.MediaPlayer2.vlc", "Metadata"): metadata(),
                ("org.mpris.MediaPlayer2.vlc", "Position"): 0,
            },
        )

        matrix = mpris_compat_matrix.build_matrix(client)

        self.assertEqual(
            matrix["summary"]["issueCounts"],
            {
                "missing-art": 2,
                "not-playing": 2,
            },
        )
        self.assertEqual(matrix["summary"]["statusCounts"], {"Stopped": 2})

    def test_writes_json_and_markdown_artifacts(self):
        matrix = {
            "services": ["org.mpris.MediaPlayer2.vlc"],
            "summary": {
                "totalProbes": 1,
                "okProbes": 1,
                "availableProbes": 1,
                "playingProbes": 1,
                "statusCounts": {"Playing": 1},
                "issueCounts": {},
            },
            "probes": [
                {
                    "name": "exact-service: org.mpris.MediaPlayer2.vlc",
                    "service": "org.mpris.MediaPlayer2.vlc",
                    "classification": {
                        "ok": True,
                        "available": True,
                        "playing": True,
                        "status": "Playing",
                        "title": "Instalock",
                        "artist": "WYLTK",
                        "album": "Instalock",
                        "issues": [],
                    },
                    "snapshot": {"diagnostic": "selected"},
                }
            ],
        }

        mpris_compat_matrix.write_artifacts(matrix, self.root)

        summary_json = self.root / "summary.json"
        summary_md = self.root / "summary.md"
        self.assertTrue(summary_json.exists())
        self.assertTrue(summary_md.exists())
        self.assertEqual(json.loads(summary_json.read_text())["summary"]["okProbes"], 1)
        self.assertIn("exact-service: org.mpris.MediaPlayer2.vlc", summary_md.read_text())
        self.assertIn("Instalock", summary_md.read_text())


if __name__ == "__main__":
    unittest.main()
