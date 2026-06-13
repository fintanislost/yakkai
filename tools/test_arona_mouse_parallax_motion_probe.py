import argparse
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from PIL import Image

import tools.arona_mouse_parallax_motion_probe as motion_probe


def _config(output_root: Path | None = None) -> motion_probe.MotionProbeConfig:
    root = Path("/repo")
    return motion_probe.MotionProbeConfig(
        repo_root=root,
        harness=root / "build/native/scene_harness/yakkai_scene_harness",
        source=root / "scene.pkg",
        assets=root / "assets",
        output_root=output_root or root / "smoke-tests/artifacts/tmp/arona-mouse-parallax-motion",
        scene_properties_json='{"timeofday":{"value":"1"}}',
    )


class AronaMouseParallaxMotionProbeTests(unittest.TestCase):
    def test_build_sequence_command_includes_timeline_capture_and_debug_outputs(self):
        config = _config()

        command = motion_probe.build_sequence_command(config)

        self.assertIn("--debug-mouse-timeline", command)
        self.assertEqual(command[command.index("--debug-mouse-timeline") + 1], config.timeline)
        self.assertIn("--capture-dir", command)
        self.assertEqual(
            Path(command[command.index("--capture-dir") + 1]),
            config.output_root / "sequence" / "frames",
        )
        self.assertIn("--capture-sequence", command)
        self.assertEqual(command[command.index("--capture-sequence") + 1], config.capture_sequence)
        self.assertIn("--debug-effect-captures", command)
        self.assertEqual(
            Path(command[command.index("--debug-effect-captures") + 1]),
            config.output_root / "sequence" / "effect-captures",
        )
        self.assertIn("--debug-effect-capture-delay-ms", command)
        self.assertIn("--hide-info-overlay", command)
        self.assertIn("--scene-properties-json", command)

    def test_build_record_command_includes_timeline_record_and_review_output(self):
        config = _config()

        command = motion_probe.build_record_command(config)

        self.assertIn("--debug-mouse-timeline", command)
        self.assertEqual(command[command.index("--debug-mouse-timeline") + 1], config.timeline)
        self.assertIn("--debug-effect-captures", command)
        self.assertEqual(
            Path(command[command.index("--debug-effect-captures") + 1]),
            config.output_root / "record-effect-captures",
        )
        self.assertIn("--record", command)
        self.assertEqual(Path(command[command.index("--record") + 1]), config.output_root / "review-live.mp4")
        self.assertIn("--record-duration-ms", command)
        self.assertEqual(command[command.index("--record-duration-ms") + 1], str(config.record_duration_ms))
        self.assertIn("--record-fps", command)
        self.assertEqual(command[command.index("--record-fps") + 1], str(config.record_fps))
        self.assertNotIn("--capture-dir", command)
        self.assertNotIn("--capture-sequence", command)

    def test_write_motion_report_uses_manifest_frames_mp4_and_contact_sheet(self):
        with tempfile.TemporaryDirectory() as temp:
            output_root = Path(temp)
            frames_dir = output_root / "sequence" / "frames"
            captures_dir = output_root / "sequence" / "effect-captures"
            frames_dir.mkdir(parents=True)
            captures_dir.mkdir(parents=True)
            for index, color in enumerate(((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0))):
                Image.new("RGB", (32, 18), color).save(frames_dir / f"frame-{index:04d}.png")
            manifest = {
                "mouseParallax": {
                    "inputSource": "synthetic-timeline",
                    "timeline": [
                        {"timeMs": 0, "position": [0.5, 0.5]},
                        {"timeMs": 1000, "position": [0.0, 0.5]},
                    ],
                }
            }
            (captures_dir / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            (output_root / "review-live.mp4").write_bytes(b"not-a-real-mp4")
            config = _config(output_root)

            motion_probe.write_motion_report(config)

            report = json.loads((output_root / "motion-report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["timeline"], config.timeline)
            self.assertEqual(report["windowSize"], config.window_size)
            self.assertEqual(report["frameCount"], 4)
            self.assertEqual(report["mp4Path"], str(output_root / "review-live.mp4"))
            self.assertEqual(report["sequenceDirectory"], str(output_root / "sequence"))
            self.assertEqual(report["manifestMouseParallaxInputSource"], "synthetic-timeline")
            self.assertEqual(report["manifestTimeline"], manifest["mouseParallax"]["timeline"])
            self.assertTrue((output_root / "review-contact-sheet.png").exists())
            markdown = (output_root / "motion-report.md").read_text(encoding="utf-8")
            self.assertIn("motion should ease from center to left", markdown)
            self.assertIn("ribbon/character and background should not move as a single flat layer", markdown)

    def test_run_sequence_clears_stale_sequence_artifacts_before_command(self):
        with tempfile.TemporaryDirectory() as temp:
            output_root = Path(temp)
            config = _config(output_root)
            stale_frame = output_root / "sequence" / "frames" / "stale.png"
            stale_manifest = output_root / "sequence" / "effect-captures" / "manifest.json"
            stale_frame.parent.mkdir(parents=True)
            stale_manifest.parent.mkdir(parents=True)
            stale_frame.write_bytes(b"old")
            stale_manifest.write_text("{}", encoding="utf-8")

            def fake_run(command, command_path, stdout_path, stderr_path):
                self.assertFalse(stale_frame.exists())
                self.assertFalse(stale_manifest.exists())
                self.assertTrue((output_root / "sequence" / "frames").is_dir())
                self.assertTrue((output_root / "sequence" / "effect-captures").is_dir())
                command_path.write_text("command\n", encoding="utf-8")
                stdout_path.write_text("", encoding="utf-8")
                stderr_path.write_text("", encoding="utf-8")
                return 0

            with mock.patch.object(motion_probe, "_run_command", side_effect=fake_run):
                self.assertEqual(motion_probe.run_sequence(config), 0)

    def test_run_record_clears_stale_record_artifacts_before_command(self):
        with tempfile.TemporaryDirectory() as temp:
            output_root = Path(temp)
            config = _config(output_root)
            stale_record = output_root / "review-live.mp4"
            stale_manifest = output_root / "record-effect-captures" / "manifest.json"
            stale_manifest.parent.mkdir(parents=True)
            stale_record.write_bytes(b"old")
            stale_manifest.write_text("{}", encoding="utf-8")

            def fake_run(command, command_path, stdout_path, stderr_path):
                self.assertFalse(stale_record.exists())
                self.assertFalse(stale_manifest.exists())
                self.assertTrue((output_root / "record-effect-captures").is_dir())
                command_path.write_text("command\n", encoding="utf-8")
                stdout_path.write_text("", encoding="utf-8")
                stderr_path.write_text("", encoding="utf-8")
                return 0

            with mock.patch.object(motion_probe, "_run_command", side_effect=fake_run):
                self.assertEqual(motion_probe.run_record(config), 0)

    def test_config_from_args_wires_cli_options(self):
        parser = motion_probe.build_parser()
        args = parser.parse_args(
            [
                "--source",
                "/repo/custom.pkg",
                "--assets",
                "/repo/assets",
                "--harness",
                "/repo/harness",
                "--output-root",
                "/repo/out",
                "--scene-id",
                "scene",
                "--window-size",
                "1920x1080",
                "--timeline",
                "0:0.5,0.5;1000:1,0.5",
                "--capture-sequence",
                "0:3:33",
                "--record-duration-ms",
                "1234",
                "--record-fps",
                "24",
                "--scene-properties-json",
                "{}",
                "--skip-record",
            ]
        )

        config = motion_probe.config_from_args(args)

        self.assertEqual(config.source, Path("/repo/custom.pkg"))
        self.assertEqual(config.harness, Path("/repo/harness"))
        self.assertEqual(config.output_root, Path("/repo/out"))
        self.assertEqual(config.scene_id, "scene")
        self.assertEqual(config.window_size, "1920x1080")
        self.assertEqual(config.timeline, "0:0.5,0.5;1000:1,0.5")
        self.assertEqual(config.capture_sequence, "0:3:33")
        self.assertEqual(config.record_duration_ms, 1234)
        self.assertEqual(config.record_fps, 24)
        self.assertEqual(config.scene_properties_json, "{}")
        self.assertTrue(args.skip_record)

    def test_parser_rejects_non_positive_record_options(self):
        parser = motion_probe.build_parser()
        args = parser.parse_args(["--source", "/repo/scene.pkg", "--assets", "/repo/assets"])
        args.record_duration_ms = 0

        with self.assertRaises(argparse.ArgumentTypeError):
            motion_probe.config_from_args(args)


if __name__ == "__main__":
    unittest.main()
