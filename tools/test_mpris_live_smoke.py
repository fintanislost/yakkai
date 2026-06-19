import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mpris_live_smoke


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


class MprisLiveSmokeTests(unittest.TestCase):
    def test_sorts_mpris_services_case_insensitively_with_lexical_tiebreak(self):
        self.assertEqual(
            mpris_live_smoke.sorted_mpris_services(
                [
                    "org.example.Other",
                    "org.mpris.MediaPlayer2.zeta",
                    "org.mpris.MediaPlayer2.Alpha",
                    "org.mpris.MediaPlayer2.alpha",
                ]
            ),
            [
                "org.mpris.MediaPlayer2.Alpha",
                "org.mpris.MediaPlayer2.alpha",
                "org.mpris.MediaPlayer2.zeta",
            ],
        )

    def test_selects_first_playing_readable_player(self):
        client = FixtureMprisClient(
            [
                "org.mpris.MediaPlayer2.paused",
                "org.mpris.MediaPlayer2.playing",
            ],
            {
                ("org.mpris.MediaPlayer2.paused", "PlaybackStatus"): "Paused",
                ("org.mpris.MediaPlayer2.playing", "PlaybackStatus"): "Playing",
                ("org.mpris.MediaPlayer2.playing", "Metadata"): {
                    "xesam:title": "Instalock",
                    "xesam:artist": ["WYLTK", "Other"],
                    "xesam:album": "Instalock",
                    "mpris:length": 157649737,
                    "mpris:artUrl": "file:///home/fintan/.cache/vlc/art.png",
                },
                ("org.mpris.MediaPlayer2.playing", "Position"): 42000000,
            },
        )

        snapshot = mpris_live_smoke.collect_snapshot(client)

        self.assertEqual(snapshot["selectedService"], "org.mpris.MediaPlayer2.playing")
        self.assertEqual(snapshot["playbackStatus"], "Playing")
        media = snapshot["__yakkaiMedia"]
        self.assertTrue(media["available"])
        self.assertTrue(media["playing"])
        self.assertEqual(media["title"], "Instalock")
        self.assertEqual(media["artist"], "WYLTK")
        self.assertEqual(media["album"], "Instalock")
        self.assertAlmostEqual(media["duration"], 157.649737, places=6)
        self.assertEqual(media["position"], 42)
        self.assertEqual(media["albumArtPath"], "/home/fintan/.cache/vlc/art.png")

    def test_uses_first_readable_player_when_none_are_playing(self):
        client = FixtureMprisClient(
            [
                "org.mpris.MediaPlayer2.blank",
                "org.mpris.MediaPlayer2.metadata",
            ],
            {
                ("org.mpris.MediaPlayer2.blank", "PlaybackStatus"): "Stopped",
                ("org.mpris.MediaPlayer2.blank", "Metadata"): {},
                ("org.mpris.MediaPlayer2.blank", "Position"): 0,
                ("org.mpris.MediaPlayer2.metadata", "PlaybackStatus"): "Paused",
                ("org.mpris.MediaPlayer2.metadata", "Metadata"): {
                    "xesam:title": "Paused Track"
                },
                ("org.mpris.MediaPlayer2.metadata", "Position"): 0,
            },
        )

        snapshot = mpris_live_smoke.collect_snapshot(client)

        self.assertEqual(snapshot["selectedService"], "org.mpris.MediaPlayer2.metadata")
        self.assertEqual(snapshot["playbackStatus"], "Paused")
        self.assertTrue(snapshot["__yakkaiMedia"]["available"])
        self.assertFalse(snapshot["__yakkaiMedia"]["playing"])

    def test_prefers_playing_metadata_over_paused_metadata(self):
        client = FixtureMprisClient(
            [
                "org.mpris.MediaPlayer2.paused",
                "org.mpris.MediaPlayer2.playing",
            ],
            {
                ("org.mpris.MediaPlayer2.paused", "PlaybackStatus"): "Paused",
                ("org.mpris.MediaPlayer2.paused", "Metadata"): {"xesam:title": "Paused"},
                ("org.mpris.MediaPlayer2.paused", "Position"): 0,
                ("org.mpris.MediaPlayer2.playing", "PlaybackStatus"): "Playing",
                ("org.mpris.MediaPlayer2.playing", "Metadata"): {
                    "xesam:title": "Playing"
                },
                ("org.mpris.MediaPlayer2.playing", "Position"): 0,
            },
        )

        snapshot = mpris_live_smoke.collect_snapshot(client)

        self.assertEqual(snapshot["selectedService"], "org.mpris.MediaPlayer2.playing")
        self.assertEqual(snapshot["__yakkaiMedia"]["title"], "Playing")

    def test_target_service_limits_probe_to_one_provider(self):
        client = FixtureMprisClient(
            [
                "org.mpris.MediaPlayer2.gwenview",
                "org.mpris.MediaPlayer2.vlc",
            ],
            {
                ("org.mpris.MediaPlayer2.gwenview", "PlaybackStatus"): "Playing",
                ("org.mpris.MediaPlayer2.vlc", "PlaybackStatus"): "Paused",
                ("org.mpris.MediaPlayer2.vlc", "Metadata"): {
                    "xesam:title": "Instalock",
                },
                ("org.mpris.MediaPlayer2.vlc", "Position"): 0,
            },
        )

        snapshot = mpris_live_smoke.collect_snapshot(
            client,
            target_service="org.mpris.MediaPlayer2.vlc",
        )

        self.assertEqual(snapshot["selectedService"], "org.mpris.MediaPlayer2.vlc")
        self.assertEqual(snapshot["playbackStatus"], "Paused")
        self.assertEqual(
            [candidate["service"] for candidate in snapshot["candidates"]],
            ["org.mpris.MediaPlayer2.vlc"],
        )

    def test_reports_unavailable_when_no_readable_players_exist(self):
        client = FixtureMprisClient(
            ["org.mpris.MediaPlayer2.broken"],
            {
                ("org.mpris.MediaPlayer2.broken", "PlaybackStatus"): RuntimeError(
                    "timed out"
                ),
            },
        )

        snapshot = mpris_live_smoke.collect_snapshot(client)

        self.assertFalse(snapshot["ok"])
        self.assertIsNone(snapshot["selectedService"])
        self.assertIn("No readable", snapshot["diagnostic"])
        self.assertFalse(snapshot["__yakkaiMedia"]["available"])

    def test_parses_qdbus_metadata_output(self):
        metadata = mpris_live_smoke.parse_qdbus_metadata(
            """
            mpris:artUrl: file:///home/fintan/.cache/vlc/art.png
            mpris:length: 157649737
            xesam:album: Instalock
            xesam:artist: WYLTK
            xesam:title: Instalock
            """
        )

        self.assertEqual(metadata["mpris:length"], 157649737)
        self.assertEqual(metadata["xesam:artist"], ["WYLTK"])
        self.assertEqual(metadata["xesam:title"], "Instalock")

    def test_expectation_checker_reports_all_mismatches(self):
        snapshot = {
            "selectedService": "org.mpris.MediaPlayer2.vlc",
            "playbackStatus": "Paused",
            "__yakkaiMedia": {
                "available": True,
                "playing": False,
                "title": "Wrong",
                "artist": "WYLTK",
                "album": "Instalock",
                "duration": 1.0,
                "position": 0.0,
                "albumArtPath": "/does/not/exist.png",
            },
        }

        failures = mpris_live_smoke.check_expectations(
            snapshot,
            {
                "expect_service": "org.mpris.MediaPlayer2.vlc",
                "expect_status": "Playing",
                "expect_title": "Instalock",
                "expect_artist": "WYLTK",
                "expect_album": "Instalock",
                "require_local_art": True,
            },
        )

        self.assertEqual(
            failures,
            [
                "expected playback status 'Playing', got 'Paused'",
                "expected title 'Instalock', got 'Wrong'",
                "expected local album art path to exist, got '/does/not/exist.png'",
            ],
        )


if __name__ == "__main__":
    unittest.main()
