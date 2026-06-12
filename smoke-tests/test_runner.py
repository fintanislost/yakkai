import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from datetime import datetime, timezone
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import runner


class RunnerCoreTests(unittest.TestCase):
    def test_default_artifacts_path_is_repo_local(self):
        args = runner.build_parser().parse_args([])

        self.assertEqual(args.artifacts, "smoke-tests/artifacts/tmp/yakkai-smoke")

    def test_expand_path_replaces_home_and_manifest_tokens(self):
        paths = {
            "workshop": "${HOME}/Steam/workshop",
            "baselines": "smoke-tests/baselines",
        }
        expanded = runner.expand_manifest_path("${workshop}/123/scene.pkg", paths, Path("/repo"), {"HOME": "/home/test"})
        self.assertEqual(expanded, Path("/home/test/Steam/workshop/123/scene.pkg"))

    def test_expand_path_rejects_cyclic_manifest_tokens(self):
        paths = {"a": "${b}", "b": "${a}"}
        with self.assertRaises(ValueError):
            runner.expand_manifest_path("${a}/scene.pkg", paths, Path("/repo"), {"HOME": "/home/test"})

    def test_expand_path_rejects_self_expanding_manifest_token(self):
        paths = {"a": "${a}/more"}
        with self.assertRaises(ValueError):
            runner.expand_manifest_path("${a}", paths, Path("/repo"), {"HOME": "/home/test"})

    def test_select_scenes_by_suite(self):
        manifest = {
            "scenes": [
                {"id": "a", "gates": ["quick"], "required": True},
                {"id": "b", "gates": ["deep"], "required": False},
            ]
        }
        self.assertEqual([s["id"] for s in runner.select_scenes(manifest, "quick")], ["a"])
        self.assertEqual([s["id"] for s in runner.select_scenes(manifest, "deep")], ["b"])

    def test_expand_manifest_scenes_turns_base_with_variants_into_cases(self):
        manifest = {
            "scenes": [
                {
                    "id": "3228578419",
                    "name": "Sleeping Arona",
                    "source": "${workshop}/3228578419/scene.pkg",
                    "gates": ["quick", "deep", "release"],
                    "required": True,
                    "captures": [{"name": "still-8000", "timeMs": 8000, "baseline": "3228578419/stills/still-8000.png"}],
                    "sequences": [{"name": "motion-8000", "baselineDir": "3228578419/sequences/motion-8000"}],
                    "thresholds": {"rmseReview": 0.035},
                    "expectations": {"motion": True},
                    "variants": [
                        {
                            "id": "3228578419-day",
                            "name": "Sleeping Arona - Day",
                            "gates": ["quick", "deep", "release"],
                            "baselinePrefix": "3228578419-day",
                            "scenePropertyOverrides": {"timeofday": "1"},
                        },
                        {
                            "id": "3228578419-night",
                            "name": "Sleeping Arona - Night",
                            "gates": ["deep"],
                            "baselinePrefix": "3228578419-night",
                            "scenePropertyOverrides": {"timeofday": "3"},
                        },
                    ],
                },
                {
                    "id": "3327063360",
                    "name": "Shiroko Night Video",
                    "gates": ["quick", "deep", "release"],
                },
            ]
        }

        cases = runner.expand_manifest_scenes(manifest)

        self.assertEqual([case["id"] for case in cases], ["3228578419-day", "3228578419-night", "3327063360"])
        day = cases[0]
        self.assertEqual(day["sourceSceneId"], "3228578419")
        self.assertEqual(day["source"], "${workshop}/3228578419/scene.pkg")
        self.assertEqual(day["required"], True)
        self.assertEqual(day["thresholds"], {"rmseReview": 0.035})
        self.assertEqual(day["expectations"], {"motion": True})
        self.assertEqual(day["scenePropertyOverrides"], {"timeofday": "1"})
        self.assertEqual(day["captures"][0]["baseline"], "3228578419-day/stills/still-8000.png")
        self.assertEqual(day["sequences"][0]["baselineDir"], "3228578419-day/sequences/motion-8000")

    def test_scene_properties_json_merges_over_project_defaults(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            scene_dir = root / "3228578419"
            scene_dir.mkdir()
            source = scene_dir / "scene.pkg"
            source.write_text("scene", encoding="utf-8")
            (scene_dir / "project.json").write_text(
                json.dumps({
                    "general": {
                        "properties": {
                            "timeofday": {"type": "combo", "value": "0"},
                            "weather": {"type": "combo", "value": "0"},
                        }
                    }
                }),
                encoding="utf-8",
            )

            merged = runner.scene_properties_json_for_scene(
                {"id": "3228578419-day", "scenePropertyOverrides": {"timeofday": "1"}},
                source,
            )

        self.assertEqual(
            json.loads(merged),
            {
                "timeofday": {"type": "combo", "value": "1"},
                "weather": {"type": "combo", "value": "0"},
            },
        )
        self.assertEqual(merged, json.dumps(json.loads(merged), separators=(",", ":"), sort_keys=True))

    def test_scene_properties_json_returns_none_without_overrides(self):
        self.assertIsNone(runner.scene_properties_json_for_scene({"id": "plain"}, Path("/repo/plain/scene.pkg")))

    def test_scene_properties_json_rejects_non_object_overrides(self):
        with self.assertRaises(ValueError):
            runner.scene_properties_json_for_scene({"id": "bad", "scenePropertyOverrides": ["timeofday", "1"]}, Path("/repo/scene.pkg"))

    def test_scene_result_metadata_uses_manifest_properties_for_web_project(self):
        manifest = {"paths": {"workshop": "/workshop"}}
        scene = {
            "id": "1509243786",
            "projectType": "web",
            "source": "${workshop}/1509243786/index.html",
            "scenePropertiesJson": '{"rate":{"value":50}}',
        }

        source_scene_id, scene_properties_json = runner.scene_result_metadata(
            scene,
            manifest,
            Path("/repo"),
            assets_override=None,
            workshop_override=None,
        )

        self.assertEqual(source_scene_id, "1509243786")
        self.assertEqual(scene_properties_json, '{"rate":{"value":50}}')

    def test_scene_result_metadata_loads_project_defaults_for_web_project(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "workshop" / "1509243786" / "index.html"
            source.parent.mkdir(parents=True)
            source.write_text("<html></html>", encoding="utf-8")
            (source.parent / "project.json").write_text(
                json.dumps({"general": {"properties": {"wallpapermode": {"value": 1}}}}),
                encoding="utf-8",
            )
            manifest = {"paths": {"workshop": str(root / "workshop")}}
            scene = {
                "id": "1509243786",
                "projectType": "web",
                "source": "${workshop}/1509243786/index.html",
            }

            source_scene_id, scene_properties_json = runner.scene_result_metadata(
                scene,
                manifest,
                root,
                assets_override=None,
                workshop_override=None,
            )

        self.assertEqual(source_scene_id, "1509243786")
        self.assertEqual(scene_properties_json, '{"wallpapermode":{"value":1}}')

    def test_scene_result_metadata_applies_property_overrides_for_web_project(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "workshop" / "1509243786" / "index.html"
            source.parent.mkdir(parents=True)
            source.write_text("<html></html>", encoding="utf-8")
            (source.parent / "project.json").write_text(
                json.dumps({
                    "general": {
                        "properties": {
                            "DateAlignment": {"type": "combo", "value": "right"},
                            "DateX": {"type": "slider", "value": 59},
                            "DateY": {"type": "slider", "value": 56},
                        }
                    }
                }),
                encoding="utf-8",
            )
            manifest = {"paths": {"workshop": str(root / "workshop")}}
            scene = {
                "id": "1509243786",
                "projectType": "web",
                "source": "${workshop}/1509243786/index.html",
                "scenePropertyOverrides": {"DateAlignment": "left", "DateX": 61},
            }

            source_scene_id, scene_properties_json = runner.scene_result_metadata(
                scene,
                manifest,
                root,
                assets_override=None,
                workshop_override=None,
            )

        self.assertEqual(source_scene_id, "1509243786")
        self.assertEqual(
            json.loads(scene_properties_json),
            {
                "DateAlignment": {"type": "combo", "value": "left"},
                "DateX": {"type": "slider", "value": 61},
                "DateY": {"type": "slider", "value": 56},
            },
        )
        self.assertEqual(scene_properties_json, json.dumps(json.loads(scene_properties_json), separators=(",", ":"), sort_keys=True))

    def test_load_project_scene_properties_treats_non_object_shapes_as_empty(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "scene.pkg"
            source.write_text("scene", encoding="utf-8")
            project = source.parent / "project.json"

            for payload in (["not", "object"], {"general": []}, {"general": {"properties": []}}):
                project.write_text(json.dumps(payload), encoding="utf-8")
                self.assertEqual(runner.load_project_scene_properties(source), {})

    def test_apply_scene_property_overrides_does_not_mutate_defaults(self):
        defaults = {"timeofday": {"value": "0"}}

        merged = runner.apply_scene_property_overrides(defaults, {"timeofday": "1"})

        self.assertEqual(defaults, {"timeofday": {"value": "0"}})
        self.assertEqual(merged, {"timeofday": {"value": "1"}})

    def test_select_scenes_uses_expanded_variants_and_excludes_template_base(self):
        manifest = {
            "scenes": [
                {
                    "id": "3228578419",
                    "name": "Sleeping Arona",
                    "gates": ["quick", "deep", "release"],
                    "variants": [
                        {"id": "3228578419-day", "name": "Day", "gates": ["quick", "deep", "release"], "baselinePrefix": "3228578419-day"},
                        {"id": "3228578419-sunset", "name": "Sunset", "gates": ["deep"], "baselinePrefix": "3228578419-sunset"},
                        {"id": "3228578419-night", "name": "Night", "gates": ["deep"], "baselinePrefix": "3228578419-night"},
                    ],
                }
            ]
        }

        self.assertEqual([scene["id"] for scene in runner.select_scenes(manifest, "quick")], ["3228578419-day"])
        self.assertEqual(
            [scene["id"] for scene in runner.select_scenes(manifest, "deep")],
            ["3228578419-day", "3228578419-sunset", "3228578419-night"],
        )
        self.assertEqual([scene["id"] for scene in runner.select_scenes(manifest, "release")], ["3228578419-day"])

    def test_apply_baseline_prefix_rejects_absolute_or_parent_paths(self):
        with self.assertRaises(ValueError):
            runner.baseline_path_with_prefix("/tmp/outside.png", "variant")
        with self.assertRaises(ValueError):
            runner.baseline_path_with_prefix("../outside.png", "variant")
        self.assertEqual(
            runner.baseline_path_with_prefix("3228578419/stills/still-8000.png", "3228578419-day"),
            "3228578419-day/stills/still-8000.png",
        )

    def test_classify_rmse_result(self):
        thresholds = {"rmseReview": 0.01, "rmseFail": 0.05}
        self.assertEqual(runner.classify_rmse(0.001, thresholds), "pass")
        self.assertEqual(runner.classify_rmse(0.02, thresholds), "review")
        self.assertEqual(runner.classify_rmse(0.2, thresholds), "fail")

    def test_worst_status_uses_result_order(self):
        self.assertEqual(runner.worst_status("pass", "review"), "review")
        self.assertEqual(runner.worst_status("review", "skip"), "skip")
        self.assertEqual(runner.worst_status("skip", "fail"), "fail")
        self.assertEqual(runner.worst_status("fail", "pass"), "fail")

    def test_worst_status_rejects_unknown_statuses(self):
        with self.assertRaises(ValueError):
            runner.worst_status("pass", "unknown")
        with self.assertRaises(ValueError):
            runner.worst_status("unknown", "pass")

    def test_merged_thresholds_preserves_defaults_and_applies_scene_overrides(self):
        manifest = {
            "defaults": {
                "thresholds": {
                    "rmseReview": 0.01,
                    "rmseFail": 0.05,
                    "minUniqueColors": 50,
                }
            }
        }
        scene = {"thresholds": {"rmseFail": 0.12}}
        self.assertEqual(
            runner.merged_thresholds(manifest, scene),
            {"rmseReview": 0.01, "rmseFail": 0.12, "minUniqueColors": 50},
        )

    def test_merged_capture_size_preserves_defaults_and_applies_scene_overrides(self):
        manifest = {"defaults": {"captureSize": {"width": 1280, "height": 720}}}
        self.assertEqual(runner.merged_capture_size(manifest, {}), {"width": 1280, "height": 720})
        self.assertEqual(
            runner.merged_capture_size(manifest, {"captureSize": {"height": 900}}),
            {"width": 1280, "height": 900},
        )

    def test_merged_capture_size_rejects_invalid_values(self):
        with self.assertRaises(ValueError):
            runner.merged_capture_size({"defaults": {"captureSize": {"width": 0, "height": 720}}}, {})

    def test_review_exits_zero_without_strict(self):
        summary = runner.RunSummary()
        summary.add("review")
        self.assertEqual(summary.exit_code(strict=False, require_assets=False), 0)
        self.assertEqual(summary.exit_code(strict=True, require_assets=False), 1)

    def test_required_skip_fails_when_assets_are_required(self):
        summary = runner.RunSummary()
        summary.add("skip", required=True)
        self.assertEqual(summary.exit_code(strict=False, require_assets=False), 0)
        self.assertEqual(summary.exit_code(strict=False, require_assets=True), 1)

    def test_promote_rejects_paths_outside_artifact_root(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaises(ValueError):
                runner.ensure_relative_to(root / "run", root / "other" / "file.png")

    def test_manifest_loads_valid_json(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "scenes.json"
            path.write_text(json.dumps({"version": 1, "scenes": []}), encoding="utf-8")
            self.assertEqual(runner.load_manifest(path)["version"], 1)

    def test_timestamped_artifact_dir_creates_unique_directory_under_base(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            first = runner.timestamped_artifact_dir(base)
            second = runner.timestamped_artifact_dir(base)

            self.assertTrue(first.is_dir())
            self.assertTrue(second.is_dir())
            self.assertEqual(first.parent, base)
            self.assertEqual(second.parent, base)
            self.assertNotEqual(first, second)

    def test_timestamped_artifact_dir_retries_on_collision(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            now = datetime(2026, 5, 26, 12, 34, 56, 123456, tzinfo=timezone.utc)
            first_path = base / "20260526T123456123456Z"
            first_path.mkdir()

            created = runner.timestamped_artifact_dir(base, now=now)

            self.assertEqual(created, base / "20260526T123456123456Z-1")
            self.assertTrue(created.is_dir())

    def test_select_imagemagick_prefers_magick_when_available(self):
        commands = runner.ImageMagickCommands.from_path({"magick": "/usr/bin/magick"})
        self.assertEqual(commands.identify, ["/usr/bin/magick", "identify"])
        self.assertEqual(commands.compare, ["/usr/bin/magick", "compare"])

    def test_select_imagemagick_uses_legacy_tools(self):
        commands = runner.ImageMagickCommands.from_path({
            "identify": "/usr/bin/identify",
            "convert": "/usr/bin/convert",
            "compare": "/usr/bin/compare",
        })
        self.assertEqual(commands.identify, ["/usr/bin/identify"])
        self.assertEqual(commands.convert, ["/usr/bin/convert"])
        self.assertEqual(commands.compare, ["/usr/bin/compare"])

    def test_clear_shader_cache_removes_spvs01_dirs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stale = root / "abc" / "spvs01"
            stale.mkdir(parents=True)
            (stale / "shader.spv").write_text("stale", encoding="utf-8")
            removed = runner.clear_shader_cache(root)
            self.assertEqual(removed, [stale])
            self.assertFalse(stale.exists())

    def test_build_harness_base_command_uses_scene_backend_source_assets_and_fill(self):
        command = runner.build_harness_base_command(
            Path("/repo/build/harness"),
            {"backend": "wallpaper", "fill": "fit"},
            Path("/repo/scene.pkg"),
            Path("/repo/assets"),
            {"width": 1280, "height": 720},
        )
        self.assertEqual(
            command,
            [
                "/repo/build/harness",
                "--backend",
                "wallpaper",
                "--source",
                "/repo/scene.pkg",
                "--assets",
                "/repo/assets",
                "--fill",
                "fit",
                "--hide-info-overlay",
                "--window-size",
                "1280x720",
            ],
        )

    def test_build_harness_base_command_adds_scene_properties_json(self):
        command = runner.build_harness_base_command(
            Path("/repo/build/harness"),
            {"backend": "paper", "fill": "crop"},
            Path("/repo/scene.pkg"),
            Path("/repo/assets"),
            {"width": 1280, "height": 720},
            '{"timeofday":{"value":"1"}}',
        )

        self.assertIn("--scene-properties-json", command)
        self.assertEqual(command[command.index("--scene-properties-json") + 1], '{"timeofday":{"value":"1"}}')

    def test_build_harness_base_command_rejects_invalid_harness_args(self):
        for harness_args in ("--debug-synthetic-audio", ["--debug-synthetic-audio", 128]):
            with self.subTest(harness_args=harness_args):
                with self.assertRaisesRegex(ValueError, "scene bad harnessArgs must be a list of strings"):
                    runner.build_harness_base_command(
                        Path("/repo/build/harness"),
                        {"id": "bad", "backend": "paper", "harnessArgs": harness_args},
                        Path("/repo/scene.pkg"),
                        Path("/repo/assets"),
                        {"width": 1280, "height": 720},
                    )

    def test_run_scene_captures_fails_invalid_harness_args_shape(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "workshop" / "1509243786" / "index.html"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.parent.mkdir(parents=True)
            source.write_text("<html></html>", encoding="utf-8")
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": str(root / "workshop"),
                },
            }
            scene = {
                "id": "1509243786",
                "projectType": "web",
                "source": "${workshop}/1509243786/index.html",
                "harnessArgs": "--debug-synthetic-audio",
                "captures": [{"timeMs": 6000, "baseline": "1509243786/stills/still-6000.png"}],
                "sequences": [],
            }

            status, log_path, actuals, notes = runner.run_scene_captures(
                manifest=manifest,
                scene=scene,
                root=root,
                run_dir=run_dir,
                assets_override=None,
                workshop_override=None,
            )

        self.assertEqual(status, "fail")
        self.assertIsNone(log_path)
        self.assertEqual(actuals, [])
        self.assertEqual(notes, ["scene 1509243786 harnessArgs must be a list of strings"])

    def test_project_type_for_scene_rejects_unsupported_project_type(self):
        with self.assertRaisesRegex(ValueError, "unsupported projectType for bad: app"):
            runner.project_type_for_scene({"id": "bad", "projectType": "app"})

    def test_backend_for_project_type_defaults_scene_to_paper(self):
        self.assertEqual(runner.backend_for_project_type("scene", {"id": "scene"}), "paper")

    def test_backend_for_project_type_preserves_manifest_override_for_video(self):
        self.assertEqual(
            runner.backend_for_project_type("video", {"id": "video", "backend": "custom-video"}),
            "custom-video",
        )

    def test_describe_return_code_labels_signal_exits(self):
        self.assertEqual(runner.describe_return_code(0), "exit 0")
        self.assertEqual(runner.describe_return_code(7), "exit 7")
        self.assertEqual(runner.describe_return_code(-5), "signal 5 SIGTRAP")

    def test_run_command_appends_command_and_exit_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            log_path = Path(temp) / "logs" / "command.log"
            code = "import sys; marker='CHILD_STD'+'OUT'; print(marker); print('CHILD_STD'+'ERR', file=sys.stderr); sys.exit(7)"

            returncode = runner.run_command([sys.executable, "-c", code], log_path, 5)
            text = log_path.read_text(encoding="utf-8")

            self.assertEqual(returncode, 7)
            self.assertIn("COMMAND:", text)
            self.assertIn("CHILD_STDOUT", text)
            self.assertIn("CHILD_STDERR", text)
            self.assertIn("EXIT: exit 7", text)
            self.assertIn("elapsedSeconds=", text)
            self.assertLess(text.index("COMMAND:"), text.index("\nCHILD_STDOUT\n"))
            self.assertLess(text.index("TIMEOUT_SECONDS:"), text.index("\nCHILD_STDOUT\n"))

    def test_run_command_writes_combined_output_and_returns_exit_code(self):
        with tempfile.TemporaryDirectory() as temp:
            log_path = Path(temp) / "logs" / "command.log"
            code = "import sys; print('CHILD_'+'OUT'); print('CHILD_'+'ERR', file=sys.stderr); sys.exit(7)"

            returncode = runner.run_command([sys.executable, "-c", code], log_path, 5)
            text = log_path.read_text(encoding="utf-8")

            self.assertEqual(returncode, 7)
            self.assertIn("\nCHILD_OUT\n", text)
            self.assertIn("\nCHILD_ERR\n", text)
            self.assertIn("EXIT: exit 7", text)

    def test_run_command_returns_124_and_logs_timeout(self):
        with tempfile.TemporaryDirectory() as temp:
            log_path = Path(temp) / "logs" / "timeout.log"
            command = [sys.executable, "-c", "import time; time.sleep(1)"]

            returncode = runner.run_command(command, log_path, 0.01)

            log = log_path.read_text(encoding="utf-8")
            self.assertEqual(returncode, 124)
            self.assertIn("TIMEOUT after 0.01s:", log)
            self.assertIn(sys.executable, log)

    def test_run_command_returns_127_and_logs_missing_command(self):
        with tempfile.TemporaryDirectory() as temp:
            log_path = Path(temp) / "logs" / "missing.log"
            command = ["/path/to/missing-yakkai-command"]

            returncode = runner.run_command(command, log_path, 5)

            log = log_path.read_text(encoding="utf-8")
            self.assertEqual(returncode, 127)
            self.assertIn("COMMAND NOT FOUND:", log)
            self.assertIn(command[0], log)

    def test_parse_imagemagick_rmse_normalized_value(self):
        self.assertAlmostEqual(runner.parse_rmse("1922.4 (0.029334)"), 0.029334)
        self.assertAlmostEqual(runner.parse_rmse("0 (0)"), 0.0)

    def test_motion_status_requires_minimum_motion(self):
        thresholds = {"minMotionRmse": 0.004, "maxStaticMotionRmse": 0.01}
        self.assertEqual(runner.classify_motion([0.006, 0.005], True, thresholds), "pass")
        self.assertEqual(runner.classify_motion([0.001, 0.001], True, thresholds), "fail")
        self.assertEqual(runner.classify_motion([0.02, 0.001], False, thresholds), "fail")
        self.assertEqual(runner.classify_motion([0.002, 0.001], False, thresholds), "pass")

    def test_log_scan_fails_shader_and_material_errors(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "harness.log"
            path.write_text("shader compile failed\nmaterial faild\n", encoding="utf-8")
            failures = runner.scan_log_failures(path)
            self.assertIn("shader compile failed", failures)
            self.assertIn("material faild", failures)

    def test_dataclass_to_json_serializes_nested_scene_result(self):
        result = runner.SceneResult(
            id="3228578419-day",
            name="Sleeping Arona - Day",
            status="review",
            required=True,
            log=None,
            sourceSceneId="3228578419",
            scenePropertiesJson='{"timeofday":{"value":"1"}}',
            frames=[
                runner.FrameResult(
                    name="frame-00008000ms",
                    status="review",
                    actual="/tmp/frame.png",
                    baseline=None,
                    diff=None,
                    metrics={"baselineMissing": True},
                )
            ],
            notes=["missing baseline"],
        )

        self.assertEqual(
            runner.dataclass_to_json(result),
            {
                "id": "3228578419-day",
                "name": "Sleeping Arona - Day",
                "status": "review",
                "required": True,
                "log": None,
                "sourceSceneId": "3228578419",
                "scenePropertiesJson": '{"timeofday":{"value":"1"}}',
                "frames": [
                    {
                        "name": "frame-00008000ms",
                        "status": "review",
                        "actual": "/tmp/frame.png",
                        "baseline": None,
                        "diff": None,
                        "metrics": {"baselineMissing": True},
                    }
                ],
                "notes": ["missing baseline"],
            },
        )

    def test_baseline_for_actual_maps_stills_and_sequence_frames(self):
        scene = {
            "captures": [{"timeMs": 8000, "baseline": "scene/stills/still-8000.png"}],
            "sequences": [{"name": "motion-8000", "baselineDir": "scene/sequences/motion-8000"}],
        }
        baseline_root = Path("/repo/smoke-tests/baselines")

        self.assertEqual(
            runner.baseline_for_actual(
                scene,
                Path("/tmp/run/scene/captures/stills/frame-00008000ms.png"),
                baseline_root,
            ),
            baseline_root / "scene/stills/still-8000.png",
        )
        self.assertEqual(
            runner.baseline_for_actual(
                scene,
                Path("/tmp/run/scene/captures/motion-8000/frame-0001.png"),
                baseline_root,
            ),
            baseline_root / "scene/sequences/motion-8000/frame-0001.png",
        )

    def test_baseline_candidates_include_temporal_window_for_sequence_frames(self):
        scene = {
            "sequences": [
                {
                    "name": "video-20000",
                    "frames": 5,
                    "baselineDir": "scene/sequences/video-20000",
                    "temporalToleranceFrames": 2,
                }
            ],
        }
        baseline_root = Path("/repo/smoke-tests/baselines")

        candidates = runner.baseline_candidates_for_actual(
            scene,
            Path("/tmp/run/scene/captures/video-20000/frame-0001.png"),
            baseline_root,
        )

        self.assertEqual(
            candidates,
            [
                baseline_root / "scene/sequences/video-20000/frame-0000.png",
                baseline_root / "scene/sequences/video-20000/frame-0001.png",
                baseline_root / "scene/sequences/video-20000/frame-0002.png",
                baseline_root / "scene/sequences/video-20000/frame-0003.png",
            ],
        )

    def test_baseline_comparison_skip_reason_marks_temporal_sequence_edges(self):
        scene = {
            "sequences": [
                {
                    "name": "video-20000",
                    "frames": 10,
                    "baselineDir": "scene/sequences/video-20000",
                    "temporalToleranceFrames": 3,
                }
            ],
        }

        self.assertEqual(
            runner.baseline_comparison_skip_reason(
                scene,
                Path("/tmp/run/scene/captures/video-20000/frame-0001.png"),
            ),
            "temporalEdge",
        )
        self.assertEqual(
            runner.baseline_comparison_skip_reason(
                scene,
                Path("/tmp/run/scene/captures/video-20000/frame-0005.png"),
            ),
            None,
        )
        self.assertEqual(
            runner.baseline_comparison_skip_reason(
                scene,
                Path("/tmp/run/scene/captures/video-20000/frame-0008.png"),
            ),
            "temporalEdge",
        )

    def test_run_scene_captures_uses_task3_capture_filenames(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "scene.pkg"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.write_text("scene", encoding="utf-8")
            (source.parent / "project.json").write_text(
                json.dumps({"general": {"properties": {"timeofday": {"value": "0"}}}}),
                encoding="utf-8",
            )
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": "workshop",
                }
            }
            scene = {
                "id": "scene",
                "source": str(source),
                "captures": [{"timeMs": 8000, "baseline": "scene/stills/still-8000.png"}],
                "sequences": [{"name": "motion", "startMs": 8000, "frames": 2, "intervalMs": 33}],
                "scenePropertyOverrides": {"timeofday": "1"},
            }
            timeouts = []

            def fake_run(command, log_path, timeout_seconds):
                timeouts.append(timeout_seconds)
                self.assertIn("--scene-properties-json", command)
                properties = json.loads(command[command.index("--scene-properties-json") + 1])
                self.assertEqual(properties["timeofday"]["value"], "1")
                capture_dir = Path(command[command.index("--capture-dir") + 1])
                capture_dir.mkdir(parents=True)
                if "--capture-times-ms" in command:
                    (capture_dir / "frame-00008000ms.png").write_text("still", encoding="utf-8")
                else:
                    (capture_dir / "frame-0000.png").write_text("a", encoding="utf-8")
                    (capture_dir / "frame-0001.png").write_text("b", encoding="utf-8")
                log_path.write_text("ok", encoding="utf-8")
                return 0

            with mock.patch.object(runner, "run_command", side_effect=fake_run):
                status, log_path, actuals, notes = runner.run_scene_captures(
                    manifest=manifest,
                    scene=scene,
                    root=root,
                    run_dir=run_dir,
                    assets_override=None,
                    workshop_override=None,
                )

        self.assertEqual(status, "pass")
        self.assertEqual(notes, [])
        self.assertEqual(log_path, run_dir / "scene" / "harness.log")
        self.assertEqual([path.name for path in actuals], ["frame-00008000ms.png", "frame-0000.png", "frame-0001.png"])
        self.assertEqual(timeouts, [78, 79])

    def test_run_scene_captures_uses_video_backend_for_video_project(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "workshop" / "2478419118" / "shiroko.mp4"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.parent.mkdir(parents=True)
            source.write_text("video", encoding="utf-8")
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": str(root / "workshop"),
                },
            }
            scene = {
                "id": "2478419118",
                "projectType": "video",
                "source": "${workshop}/2478419118/shiroko.mp4",
                "captures": [{"timeMs": 5000, "baseline": "2478419118/stills/still-5000.png"}],
                "sequences": [],
            }

            def fake_run(command, log_path, timeout_seconds):
                self.assertIn("--backend", command)
                self.assertEqual(command[command.index("--backend") + 1], "video")
                self.assertNotIn("--scene-properties-json", command)
                capture_dir = Path(command[command.index("--capture-dir") + 1])
                capture_dir.mkdir(parents=True)
                (capture_dir / "frame-00005000ms.png").write_text("still", encoding="utf-8")
                log_path.write_text("ok", encoding="utf-8")
                return 0

            with mock.patch.object(runner, "run_command", side_effect=fake_run):
                status, log_path, actuals, notes = runner.run_scene_captures(
                    manifest=manifest,
                    scene=scene,
                    root=root,
                    run_dir=run_dir,
                    assets_override=None,
                    workshop_override=None,
                )

        self.assertEqual(status, "pass")
        self.assertEqual(notes, [])
        self.assertEqual([path.name for path in actuals], ["frame-00005000ms.png"])

    def test_run_scene_captures_uses_web_backend_and_properties_for_web_project(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "workshop" / "1509243786" / "index.html"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.parent.mkdir(parents=True)
            source.write_text("<html></html>", encoding="utf-8")
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": str(root / "workshop"),
                },
            }
            scene = {
                "id": "1509243786",
                "projectType": "web",
                "source": "${workshop}/1509243786/index.html",
                "scenePropertiesJson": '{"rate":{"value":50}}',
                "captures": [{"timeMs": 6000, "baseline": "1509243786/stills/still-6000.png"}],
                "sequences": [],
            }

            def fake_run(command, log_path, timeout_seconds):
                self.assertIn("--backend", command)
                self.assertEqual(command[command.index("--backend") + 1], "web")
                self.assertIn("--scene-properties-json", command)
                self.assertEqual(command[command.index("--scene-properties-json") + 1], '{"rate":{"value":50}}')
                capture_dir = Path(command[command.index("--capture-dir") + 1])
                capture_dir.mkdir(parents=True)
                (capture_dir / "frame-00006000ms.png").write_text("still", encoding="utf-8")
                log_path.write_text("ok", encoding="utf-8")
                return 0

            with mock.patch.object(runner, "run_command", side_effect=fake_run):
                status, log_path, actuals, notes = runner.run_scene_captures(
                    manifest=manifest,
                    scene=scene,
                    root=root,
                    run_dir=run_dir,
                    assets_override=None,
                    workshop_override=None,
                )

        self.assertEqual(status, "pass")
        self.assertEqual(notes, [])
        self.assertEqual([path.name for path in actuals], ["frame-00006000ms.png"])

    def test_run_scene_captures_appends_manifest_harness_args(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "workshop" / "1509243786" / "index.html"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.parent.mkdir(parents=True)
            source.write_text("<html></html>", encoding="utf-8")
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": str(root / "workshop"),
                },
            }
            harness_args = ["--debug-synthetic-audio", "--debug-synthetic-audio-bins", "128"]
            scene = {
                "id": "1509243786",
                "projectType": "web",
                "source": "${workshop}/1509243786/index.html",
                "harnessArgs": harness_args,
                "captures": [{"timeMs": 6000, "baseline": "1509243786/stills/still-6000.png"}],
                "sequences": [],
            }

            def fake_run(command, log_path, timeout_seconds):
                capture_arg_index = command.index("--capture-dir")
                self.assertEqual(command[capture_arg_index - len(harness_args):capture_arg_index], harness_args)
                capture_dir = Path(command[capture_arg_index + 1])
                capture_dir.mkdir(parents=True)
                (capture_dir / "frame-00006000ms.png").write_text("still", encoding="utf-8")
                log_path.write_text("ok", encoding="utf-8")
                return 0

            with mock.patch.object(runner, "run_command", side_effect=fake_run):
                status, log_path, actuals, notes = runner.run_scene_captures(
                    manifest=manifest,
                    scene=scene,
                    root=root,
                    run_dir=run_dir,
                    assets_override=None,
                    workshop_override=None,
                )

        self.assertEqual(status, "pass")
        self.assertEqual(notes, [])
        self.assertEqual([path.name for path in actuals], ["frame-00006000ms.png"])

    def test_run_scene_captures_writes_live_review_video(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "workshop" / "893418273" / "index.html"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.parent.mkdir(parents=True)
            source.write_text("<html></html>", encoding="utf-8")
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": str(root / "workshop"),
                },
            }
            harness_args = ["--capture-exit-mode", "immediate", "--debug-synthetic-audio"]
            scene = {
                "id": "893418273-web-audio-candidate",
                "projectType": "web",
                "source": "${workshop}/893418273/index.html",
                "harnessArgs": harness_args,
                "captures": [],
                "sequences": [],
                "reviewVideo": {
                    "name": "audio-live",
                    "durationMs": 10000,
                    "fps": 30,
                    "startDelayMs": 5000,
                },
            }

            def fake_run(command, log_path, timeout_seconds):
                record_index = command.index("--record")
                self.assertEqual(command[record_index - len(harness_args):record_index], harness_args)
                output = Path(command[record_index + 1])
                output.parent.mkdir(parents=True)
                output.write_bytes(b"mp4")
                self.assertEqual(command[record_index + 2:record_index + 8], [
                    "--record-duration-ms",
                    "10000",
                    "--record-fps",
                    "30",
                    "--record-start-delay-ms",
                    "5000",
                ])
                log_path.write_text("ok", encoding="utf-8")
                self.assertGreaterEqual(timeout_seconds, 85)
                return 0

            with mock.patch.object(runner, "run_command", side_effect=fake_run):
                status, log_path, actuals, notes = runner.run_scene_captures(
                    manifest=manifest,
                    scene=scene,
                    root=root,
                    run_dir=run_dir,
                    assets_override=None,
                    workshop_override=None,
                )

            clip = run_dir / "893418273-web-audio-candidate" / "review-clips" / "audio-live.mp4"
            self.assertEqual(status, "pass")
            self.assertEqual(log_path, run_dir / "893418273-web-audio-candidate" / "audio-live.log")
            self.assertEqual(actuals, [])
            self.assertEqual(notes, [])
            self.assertEqual(clip.read_bytes(), b"mp4")

    def test_run_scene_captures_uses_project_defaults_for_web_project(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "workshop" / "1509243786" / "index.html"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.parent.mkdir(parents=True)
            source.write_text("<html></html>", encoding="utf-8")
            (source.parent / "project.json").write_text(
                json.dumps({"general": {"properties": {"wallpapermode": {"value": 1}}}}),
                encoding="utf-8",
            )
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": str(root / "workshop"),
                },
            }
            scene = {
                "id": "1509243786",
                "projectType": "web",
                "source": "${workshop}/1509243786/index.html",
                "captures": [{"timeMs": 6000, "baseline": "1509243786/stills/still-6000.png"}],
                "sequences": [],
            }

            def fake_run(command, log_path, timeout_seconds):
                self.assertIn("--backend", command)
                self.assertEqual(command[command.index("--backend") + 1], "web")
                self.assertIn("--scene-properties-json", command)
                self.assertEqual(command[command.index("--scene-properties-json") + 1], '{"wallpapermode":{"value":1}}')
                capture_dir = Path(command[command.index("--capture-dir") + 1])
                capture_dir.mkdir(parents=True)
                (capture_dir / "frame-00006000ms.png").write_text("still", encoding="utf-8")
                log_path.write_text("ok", encoding="utf-8")
                return 0

            with mock.patch.object(runner, "run_command", side_effect=fake_run):
                status, log_path, actuals, notes = runner.run_scene_captures(
                    manifest=manifest,
                    scene=scene,
                    root=root,
                    run_dir=run_dir,
                    assets_override=None,
                    workshop_override=None,
                )

        self.assertEqual(status, "pass")
        self.assertEqual(notes, [])
        self.assertEqual([path.name for path in actuals], ["frame-00006000ms.png"])

    def test_run_scene_captures_reuses_precomputed_scene_properties_json(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "scene.pkg"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.write_text("scene", encoding="utf-8")
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": "workshop",
                },
            }
            scene = {
                "id": "scene",
                "source": str(source),
                "captures": [{"timeMs": 8000, "baseline": "scene/stills/still-8000.png"}],
                "sequences": [],
            }
            precomputed = '{"timeofday":{"value":"1"}}'

            def fake_run(command, log_path, timeout_seconds):
                self.assertIn("--scene-properties-json", command)
                self.assertEqual(command[command.index("--scene-properties-json") + 1], precomputed)
                capture_dir = Path(command[command.index("--capture-dir") + 1])
                capture_dir.mkdir(parents=True)
                (capture_dir / "frame-00008000ms.png").write_text("still", encoding="utf-8")
                log_path.write_text("ok", encoding="utf-8")
                return 0

            with (
                mock.patch.object(runner, "run_command", side_effect=fake_run),
                mock.patch.object(
                    runner,
                    "scene_properties_json_for_scene",
                    side_effect=AssertionError("scene properties should be precomputed"),
                ),
            ):
                status, log_path, actuals, notes = runner.run_scene_captures(
                    manifest=manifest,
                    scene=scene,
                    root=root,
                    run_dir=run_dir,
                    assets_override=None,
                    workshop_override=None,
                    scene_properties_json=precomputed,
                )

        self.assertEqual(status, "pass")
        self.assertEqual(notes, [])
        self.assertEqual(log_path, run_dir / "scene" / "harness.log")
        self.assertEqual([path.name for path in actuals], ["frame-00008000ms.png"])

    def test_run_scene_captures_fails_invalid_scene_property_overrides_shape(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "scene.pkg"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.write_text("scene", encoding="utf-8")
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": "workshop",
                },
            }
            scene = {
                "id": "scene",
                "source": str(source),
                "captures": [],
                "sequences": [],
                "scenePropertyOverrides": ["bad"],
            }

            status, log_path, actuals, notes = runner.run_scene_captures(
                manifest=manifest,
                scene=scene,
                root=root,
                run_dir=run_dir,
                assets_override=None,
                workshop_override=None,
            )

        self.assertEqual(status, "fail")
        self.assertIsNone(log_path)
        self.assertEqual(actuals, [])
        self.assertEqual(notes, ["scene scene scenePropertyOverrides must be an object"])

    def test_run_scene_captures_fails_invalid_web_scene_property_overrides_shape(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "workshop" / "1509243786" / "index.html"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.parent.mkdir(parents=True)
            source.write_text("<html></html>", encoding="utf-8")
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": str(root / "workshop"),
                },
            }
            scene = {
                "id": "1509243786",
                "projectType": "web",
                "source": "${workshop}/1509243786/index.html",
                "captures": [],
                "sequences": [],
                "scenePropertyOverrides": ["bad"],
            }

            status, log_path, actuals, notes = runner.run_scene_captures(
                manifest=manifest,
                scene=scene,
                root=root,
                run_dir=run_dir,
                assets_override=None,
                workshop_override=None,
            )

        self.assertEqual(status, "fail")
        self.assertIsNone(log_path)
        self.assertEqual(actuals, [])
        self.assertEqual(notes, ["scene 1509243786 scenePropertyOverrides must be an object"])

    def test_run_scene_captures_fails_missing_still_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "scene.pkg"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.write_text("scene", encoding="utf-8")
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": "workshop",
                },
            }
            scene = {
                "id": "scene",
                "source": str(source),
                "captures": [{"timeMs": 8000, "baseline": "scene/stills/still-8000.png"}],
                "sequences": [],
            }

            def fake_run(command, log_path, timeout_seconds):
                Path(command[command.index("--capture-dir") + 1]).mkdir(parents=True)
                log_path.write_text("ok", encoding="utf-8")
                return 0

            with mock.patch.object(runner, "run_command", side_effect=fake_run):
                status, log_path, actuals, notes = runner.run_scene_captures(
                    manifest=manifest,
                    scene=scene,
                    root=root,
                    run_dir=run_dir,
                    assets_override=None,
                    workshop_override=None,
                )

        self.assertEqual(status, "fail")
        self.assertEqual(actuals, [])
        self.assertEqual(notes, ["missing still captures: frame-00008000ms.png"])

    def test_run_scene_captures_fails_partial_sequence_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "scene.pkg"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.write_text("scene", encoding="utf-8")
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": "workshop",
                },
            }
            scene = {
                "id": "scene",
                "source": str(source),
                "captures": [],
                "sequences": [{"name": "motion", "startMs": 8000, "frames": 3, "intervalMs": 33}],
            }

            def fake_run(command, log_path, timeout_seconds):
                capture_dir = Path(command[command.index("--capture-dir") + 1])
                capture_dir.mkdir(parents=True)
                (capture_dir / "frame-0000.png").write_text("a", encoding="utf-8")
                (capture_dir / "frame-0002.png").write_text("c", encoding="utf-8")
                log_path.write_text("ok", encoding="utf-8")
                return 0

            with mock.patch.object(runner, "run_command", side_effect=fake_run):
                status, log_path, actuals, notes = runner.run_scene_captures(
                    manifest=manifest,
                    scene=scene,
                    root=root,
                    run_dir=run_dir,
                    assets_override=None,
                    workshop_override=None,
                )

        self.assertEqual(status, "fail")
        self.assertEqual(log_path, run_dir / "scene" / "motion.log")
        self.assertEqual([path.name for path in actuals], ["frame-0000.png", "frame-0002.png"])
        self.assertEqual(notes, ["missing sequence motion frames: frame-0001.png"])

    def test_run_scene_captures_reports_failing_sequence_log(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            assets = root / "assets"
            source = root / "scene.pkg"
            run_dir = root / "run"
            harness.write_text("harness", encoding="utf-8")
            source.write_text("scene", encoding="utf-8")
            assets.mkdir()
            manifest = {
                "defaults": {"captureSize": {"width": 1280, "height": 720}},
                "paths": {
                    "harness": str(harness),
                    "assets": str(assets),
                    "baselines": "baselines",
                    "workshop": "workshop",
                }
            }
            scene = {
                "id": "scene",
                "source": str(source),
                "captures": [{"timeMs": 8000, "baseline": "scene/stills/still-8000.png"}],
                "sequences": [{"name": "motion", "startMs": 8000, "frames": 2, "intervalMs": 33}],
            }

            def fake_run(command, log_path, timeout_seconds):
                capture_dir = Path(command[command.index("--capture-dir") + 1])
                capture_dir.mkdir(parents=True)
                log_path.write_text("log", encoding="utf-8")
                if "--capture-times-ms" in command:
                    (capture_dir / "frame-00008000ms.png").write_text("still", encoding="utf-8")
                    return 0
                return 139

            with mock.patch.object(runner, "run_command", side_effect=fake_run):
                status, log_path, actuals, notes = runner.run_scene_captures(
                    manifest=manifest,
                    scene=scene,
                    root=root,
                    run_dir=run_dir,
                    assets_override=None,
                    workshop_override=None,
                )

        self.assertEqual(status, "fail")
        self.assertEqual(log_path, run_dir / "scene" / "motion.log")
        self.assertEqual([path.name for path in actuals], ["frame-00008000ms.png"])
        self.assertEqual(notes, ["harness sequence motion exited 139"])

    def test_evaluate_png_marks_metric_failure_as_frame_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            actual = Path(temp) / "frame.png"
            actual.write_bytes(b"not really a png")
            commands = runner.ImageMagickCommands(["identify"], ["convert"], ["compare"])
            thresholds = {"minGrayStddev": 0.01, "minUniqueColors": 50, "rmseReview": 0.01, "rmseFail": 0.05}

            with mock.patch.object(runner, "image_dimensions", side_effect=RuntimeError("bad image")):
                frame = runner.evaluate_png(
                    commands=commands,
                    actual=actual,
                    baseline=None,
                    diff=None,
                    thresholds=thresholds,
                    expected_dimensions="1280x720",
                )

        self.assertEqual(frame.status, "fail")
        self.assertEqual(frame.metrics["error"], "bad image")

    def test_evaluate_png_marks_dimension_mismatch_as_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            actual = Path(temp) / "frame.png"
            actual.write_bytes(b"png")
            commands = runner.ImageMagickCommands(["identify"], ["convert"], ["compare"])
            thresholds = {"minGrayStddev": 0.01, "minUniqueColors": 50, "rmseReview": 0.01, "rmseFail": 0.05}

            with (
                mock.patch.object(runner, "image_dimensions", return_value="1600x900"),
                mock.patch.object(runner, "gray_stddev", return_value=0.5),
                mock.patch.object(runner, "unique_colors", return_value=100),
            ):
                frame = runner.evaluate_png(
                    commands=commands,
                    actual=actual,
                    baseline=None,
                    diff=None,
                    thresholds=thresholds,
                    expected_dimensions="1280x720",
                )

        self.assertEqual(frame.status, "fail")
        self.assertTrue(frame.metrics["dimensionMismatch"])
        self.assertEqual(frame.metrics["expectedDimensions"], "1280x720")

    def test_evaluate_png_marks_compare_failure_as_frame_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            actual = Path(temp) / "frame.png"
            baseline = Path(temp) / "baseline.png"
            actual.write_bytes(b"png")
            baseline.write_bytes(b"png")
            commands = runner.ImageMagickCommands(["identify"], ["convert"], ["compare"])
            thresholds = {"minGrayStddev": 0.01, "minUniqueColors": 50, "rmseReview": 0.01, "rmseFail": 0.05}

            with (
                mock.patch.object(runner, "image_dimensions", return_value="1280x720"),
                mock.patch.object(runner, "gray_stddev", return_value=0.5),
                mock.patch.object(runner, "unique_colors", return_value=100),
                mock.patch.object(runner, "compare_rmse", side_effect=RuntimeError("compare failed")),
            ):
                frame = runner.evaluate_png(
                    commands=commands,
                    actual=actual,
                    baseline=baseline,
                    diff=None,
                    thresholds=thresholds,
                    expected_dimensions="1280x720",
                )

        self.assertEqual(frame.status, "fail")
        self.assertEqual(frame.metrics["compareError"], "compare failed")

    def test_evaluate_png_uses_best_temporal_baseline_candidate(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            actual = root / "captures" / "video-20000" / "frame-0003.png"
            baseline_a = root / "baselines" / "frame-0002.png"
            baseline_b = root / "baselines" / "frame-0003.png"
            actual.parent.mkdir(parents=True)
            baseline_a.parent.mkdir(parents=True)
            actual.write_bytes(b"actual")
            baseline_a.write_bytes(b"a")
            baseline_b.write_bytes(b"b")
            commands = runner.ImageMagickCommands(["identify"], ["convert"], ["compare"])
            thresholds = {"minGrayStddev": 0.01, "minUniqueColors": 50, "rmseReview": 0.05, "rmseFail": 0.2}

            def fake_compare(_commands, expected, _actual, _diff=None):
                return {baseline_a: 0.02, baseline_b: 0.08}[expected]

            with (
                mock.patch.object(runner, "image_dimensions", return_value="1280x720"),
                mock.patch.object(runner, "gray_stddev", return_value=0.5),
                mock.patch.object(runner, "unique_colors", return_value=100),
                mock.patch.object(runner, "compare_rmse", side_effect=fake_compare),
            ):
                frame = runner.evaluate_png(
                    commands=commands,
                    actual=actual,
                    baseline=baseline_b,
                    baseline_candidates=[baseline_a, baseline_b],
                    diff=None,
                    thresholds=thresholds,
                    expected_dimensions="1280x720",
                )

        self.assertEqual(frame.status, "pass")
        self.assertEqual(frame.metrics["rmse"], 0.02)
        self.assertEqual(frame.metrics["matchedBaseline"], str(baseline_a))
        self.assertEqual(frame.metrics["baselineFrameOffset"], -1)

    def test_evaluate_png_skips_baseline_comparison_when_requested(self):
        with tempfile.TemporaryDirectory() as temp:
            actual = Path(temp) / "frame.png"
            baseline = Path(temp) / "baseline.png"
            actual.write_bytes(b"actual")
            baseline.write_bytes(b"baseline")
            commands = runner.ImageMagickCommands(["identify"], ["convert"], ["compare"])
            thresholds = {"minGrayStddev": 0.01, "minUniqueColors": 50, "rmseReview": 0.01, "rmseFail": 0.05}

            with (
                mock.patch.object(runner, "image_dimensions", return_value="1280x720"),
                mock.patch.object(runner, "gray_stddev", return_value=0.5),
                mock.patch.object(runner, "unique_colors", return_value=100),
                mock.patch.object(runner, "compare_rmse") as compare_rmse,
            ):
                frame = runner.evaluate_png(
                    commands=commands,
                    actual=actual,
                    baseline=baseline,
                    baseline_candidates=[baseline],
                    diff=None,
                    thresholds=thresholds,
                    expected_dimensions="1280x720",
                    skip_baseline_reason="temporalEdge",
                )

        self.assertEqual(frame.status, "pass")
        self.assertEqual(frame.metrics["baselineComparisonSkipped"], "temporalEdge")
        compare_rmse.assert_not_called()

    def test_evaluate_png_reports_missing_baseline_before_skip(self):
        with tempfile.TemporaryDirectory() as temp:
            actual = Path(temp) / "frame.png"
            baseline = Path(temp) / "missing.png"
            actual.write_bytes(b"actual")
            commands = runner.ImageMagickCommands(["identify"], ["convert"], ["compare"])
            thresholds = {"minGrayStddev": 0.01, "minUniqueColors": 50, "rmseReview": 0.01, "rmseFail": 0.05}

            with (
                mock.patch.object(runner, "image_dimensions", return_value="1280x720"),
                mock.patch.object(runner, "gray_stddev", return_value=0.5),
                mock.patch.object(runner, "unique_colors", return_value=100),
            ):
                frame = runner.evaluate_png(
                    commands=commands,
                    actual=actual,
                    baseline=baseline,
                    baseline_candidates=[baseline],
                    diff=None,
                    thresholds=thresholds,
                    expected_dimensions="1280x720",
                    skip_baseline_reason="temporalEdge",
                )

        self.assertEqual(frame.status, "review")
        self.assertTrue(frame.metrics["baselineMissing"])
        self.assertNotIn("baselineComparisonSkipped", frame.metrics)

    def test_write_review_clip_skips_when_ffmpeg_missing(self):
        with tempfile.TemporaryDirectory() as temp:
            frames_dir = Path(temp) / "frames"
            frames_dir.mkdir()
            (frames_dir / "frame-0000.png").write_bytes(b"png")

            clip = runner.write_review_clip(None, frames_dir, Path(temp) / "clip.mp4")

        self.assertIsNone(clip)

    def test_review_clip_framerate_uses_sequence_interval(self):
        self.assertEqual(runner.review_clip_framerate_for_interval_ms(100), "10")
        self.assertEqual(runner.review_clip_framerate_for_interval_ms(40), "25")
        self.assertEqual(runner.review_clip_framerate_for_interval_ms(33), "1000/33")

    def test_review_clip_framerate_rejects_non_positive_interval(self):
        with self.assertRaisesRegex(ValueError, "intervalMs must be positive"):
            runner.review_clip_framerate_for_interval_ms(0)

    def test_write_review_clip_invokes_ffmpeg_for_sequence_frames(self):
        with tempfile.TemporaryDirectory() as temp:
            frames_dir = Path(temp) / "frames"
            output = Path(temp) / "clips" / "motion.mp4"
            frames_dir.mkdir()
            (frames_dir / "frame-0000.png").write_bytes(b"png")

            with mock.patch.object(runner.subprocess, "run") as run:
                run.return_value = mock.Mock(returncode=0, stdout="", stderr="")
                clip = runner.write_review_clip("/usr/bin/ffmpeg", frames_dir, output, fps=24)

        self.assertEqual(clip, str(output))
        run.assert_called_once()
        command = run.call_args.args[0]
        self.assertEqual(command[:6], ["/usr/bin/ffmpeg", "-y", "-framerate", "24", "-pattern_type", "glob"])
        self.assertIn(str(frames_dir / "frame-*.png"), command)
        self.assertEqual(command[-1], str(output))

    def test_main_encodes_review_clip_at_sequence_interval(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run_dir = root / "artifacts"
            manifest_path = root / "manifest.json"
            manifest_path.write_text(
                json.dumps({
                    "version": 1,
                    "defaults": {
                        "captureSize": {"width": 1280, "height": 720},
                        "thresholds": {
                            "rmseReview": 0.015,
                            "rmseFail": 0.08,
                            "minGrayStddev": 0.01,
                            "minUniqueColors": 50,
                            "minMotionRmse": 0.002,
                            "maxStaticMotionRmse": 0.01,
                        },
                    },
                    "paths": {
                        "harness": "build/native/scene_harness/yakkai_scene_harness",
                        "assets": "assets",
                        "workshop": "workshop",
                        "baselines": "smoke-tests/baselines",
                    },
                    "scenes": [
                        {
                            "id": "web",
                            "name": "Web Candidate",
                            "source": "${workshop}/web/index.html",
                            "projectType": "web",
                            "gates": ["deep"],
                            "sequences": [
                                {
                                    "name": "audio-motion",
                                    "startMs": 5000,
                                    "frames": 2,
                                    "intervalMs": 100,
                                    "baselineDir": "web/sequences/audio-motion",
                                }
                            ],
                            "expectations": {"motion": True},
                        }
                    ],
                }),
                encoding="utf-8",
            )

            def fake_run_scene_captures(**kwargs):
                frames_dir = kwargs["run_dir"] / "web" / "captures" / "audio-motion"
                frames_dir.mkdir(parents=True)
                frames = [frames_dir / "frame-0000.png", frames_dir / "frame-0001.png"]
                for frame in frames:
                    frame.write_bytes(b"png")
                return "pass", kwargs["run_dir"] / "web" / "harness.log", frames, []

            commands = runner.ImageMagickCommands(["identify"], ["convert"], ["compare"])
            with (
                mock.patch.object(runner, "repo_root", return_value=root),
                mock.patch.object(runner, "require_imagemagick", return_value=commands),
                mock.patch.object(runner, "find_ffmpeg", return_value="/usr/bin/ffmpeg"),
                mock.patch.object(runner, "clear_shader_cache", return_value=[]),
                mock.patch.object(runner, "timestamped_artifact_dir", return_value=run_dir),
                mock.patch.object(runner, "run_scene_captures", side_effect=fake_run_scene_captures),
                mock.patch.object(
                    runner,
                    "evaluate_png",
                    return_value=runner.FrameResult(
                        name="frame",
                        status="pass",
                        actual="frame.png",
                        baseline=None,
                        diff=None,
                        metrics={},
                    ),
                ),
                mock.patch.object(runner, "compare_rmse", return_value=0.01),
                mock.patch.object(runner, "write_review_clip", return_value=str(run_dir / "web" / "review-clips" / "audio-motion.mp4")) as write_review_clip,
                redirect_stdout(io.StringIO()),
                redirect_stderr(io.StringIO()),
            ):
                exit_code = runner.main(["--manifest", "manifest.json", "--suite", "deep"])

        self.assertEqual(exit_code, 0)
        write_review_clip.assert_called_once()
        self.assertEqual(write_review_clip.call_args.kwargs["fps"], "10")

    def test_promote_copies_only_candidate_pngs_under_baseline_root(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run_dir = root / "run"
            baseline_root = root / "baselines"
            candidate = run_dir / "333" / "captures" / "stills" / "frame-00008000ms.png"
            ignored_text = run_dir / "333" / "captures" / "stills" / "frame.txt"
            candidate.parent.mkdir(parents=True)
            candidate.write_bytes(b"png")
            ignored_text.write_text("not a png", encoding="utf-8")
            summary = {
                "writeCandidates": True,
                "scenes": [
                    {
                        "id": "333",
                        "frames": [
                            {
                                "actual": str(candidate),
                                "baseline": str(baseline_root / "333" / "stills" / "still-8000.png"),
                            },
                            {
                                "actual": str(ignored_text),
                                "baseline": str(baseline_root / "333" / "stills" / "ignored.txt"),
                            },
                        ],
                    }
                ],
            }
            summary_path = run_dir / "summary.json"
            summary_path.parent.mkdir(parents=True, exist_ok=True)
            summary_path.write_text(json.dumps(summary), encoding="utf-8")

            promoted = runner.promote_baselines(run_dir, baseline_root)

            self.assertEqual(promoted, [baseline_root / "333" / "stills" / "still-8000.png"])
            self.assertEqual((baseline_root / "333" / "stills" / "still-8000.png").read_bytes(), b"png")
            self.assertFalse((baseline_root / "333" / "stills" / "ignored.txt").exists())

    def test_promote_requires_write_candidates_summary(self):
        with tempfile.TemporaryDirectory() as temp:
            run_dir = Path(temp) / "run"
            run_dir.mkdir()
            (run_dir / "summary.json").write_text(json.dumps({"writeCandidates": False}), encoding="utf-8")

            with self.assertRaises(ValueError):
                runner.promote_baselines(run_dir, Path(temp) / "baselines")

    def test_promote_rejects_failed_candidate_run(self):
        with tempfile.TemporaryDirectory() as temp:
            run_dir = Path(temp) / "run"
            run_dir.mkdir()
            (run_dir / "summary.json").write_text(
                json.dumps({"writeCandidates": True, "counts": {"fail": 1}, "scenes": []}),
                encoding="utf-8",
            )

            with self.assertRaises(ValueError):
                runner.promote_baselines(run_dir, Path(temp) / "baselines")

    def test_promote_rejects_failed_scene_or_frame(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run_dir = root / "run"
            candidate = run_dir / "333" / "captures" / "frame.png"
            candidate.parent.mkdir(parents=True)
            candidate.write_bytes(b"png")
            (run_dir / "summary.json").write_text(
                json.dumps(
                    {
                        "writeCandidates": True,
                        "counts": {"fail": 0},
                        "scenes": [
                            {
                                "id": "333",
                                "status": "pass",
                                "frames": [
                                    {
                                        "status": "fail",
                                        "actual": str(candidate),
                                        "baseline": str(root / "baselines" / "333" / "frame.png"),
                                    }
                                ],
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            with self.assertRaises(ValueError):
                runner.promote_baselines(run_dir, root / "baselines")

    def test_promote_rejects_baseline_paths_outside_baseline_root(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run_dir = root / "run"
            baseline_root = root / "baselines"
            candidate = run_dir / "333" / "captures" / "frame.png"
            candidate.parent.mkdir(parents=True)
            candidate.write_bytes(b"png")
            summary = {
                "writeCandidates": True,
                "scenes": [
                    {
                        "frames": [
                            {
                                "actual": str(candidate),
                                "baseline": str(root / "outside" / "frame.png"),
                            }
                        ]
                    }
                ],
            }
            run_dir.mkdir(exist_ok=True)
            (run_dir / "summary.json").write_text(json.dumps(summary), encoding="utf-8")

            with self.assertRaises(ValueError):
                runner.promote_baselines(run_dir, baseline_root)

    def test_main_list_does_not_call_dependency_or_cache_helpers(self):
        with (
            mock.patch.object(runner, "require_imagemagick") as require_imagemagick,
            mock.patch.object(runner, "find_ffmpeg") as find_ffmpeg,
            mock.patch.object(runner, "clear_shader_cache") as clear_shader_cache,
            redirect_stdout(io.StringIO()),
            redirect_stderr(io.StringIO()),
        ):
            exit_code = runner.main(["--suite", "quick", "--list"])

        self.assertEqual(exit_code, 0)
        require_imagemagick.assert_not_called()
        find_ffmpeg.assert_not_called()
        clear_shader_cache.assert_not_called()

    def test_main_dry_run_keep_shader_cache_does_not_clear_shader_cache(self):
        commands = runner.ImageMagickCommands(
            identify=["/usr/bin/magick", "identify"],
            convert=["/usr/bin/magick"],
            compare=["/usr/bin/magick", "compare"],
        )
        with (
            mock.patch.object(runner, "require_imagemagick", return_value=commands),
            mock.patch.object(runner, "find_ffmpeg", return_value="/usr/bin/ffmpeg"),
            mock.patch.object(runner, "clear_shader_cache") as clear_shader_cache,
            redirect_stdout(io.StringIO()),
            redirect_stderr(io.StringIO()),
        ):
            exit_code = runner.main(["--suite", "quick", "--dry-run", "--keep-shader-cache"])

        self.assertEqual(exit_code, 0)
        clear_shader_cache.assert_not_called()

    def test_main_missing_imagemagick_returns_1(self):
        with (
            mock.patch.object(runner, "require_imagemagick", side_effect=RuntimeError("missing ImageMagick")),
            mock.patch.object(runner, "find_ffmpeg") as find_ffmpeg,
            mock.patch.object(runner, "clear_shader_cache") as clear_shader_cache,
            redirect_stdout(io.StringIO()),
            redirect_stderr(io.StringIO()),
        ):
            exit_code = runner.main(["--suite", "quick", "--dry-run"])

        self.assertEqual(exit_code, 1)
        find_ffmpeg.assert_not_called()
        clear_shader_cache.assert_not_called()

    def test_validate_coverage_matrix_rejects_unknown_status(self):
        matrix = {
            "version": 1,
            "buckets": [
                {
                    "id": "bad",
                    "name": "Bad Bucket",
                    "minimumStatus": "candidate",
                    "coverage": [{"sceneId": "1", "status": "mystery", "source": "x", "notes": "bad"}],
                }
            ],
        }
        manifest = {"scenes": []}

        errors = runner.validate_coverage_matrix(matrix, manifest)

        self.assertIn("bucket bad scene 1 has unknown status mystery", errors)

    def test_validate_coverage_matrix_requires_active_manifest_entries(self):
        matrix = {"version": 1, "buckets": []}
        manifest = {"scenes": [{"id": "3228578419", "name": "Sleeping Arona"}]}

        errors = runner.validate_coverage_matrix(matrix, manifest)

        self.assertIn("active scene 3228578419 source scene 3228578419 is missing from coverage matrix", errors)

    def test_coverage_validation_accepts_variant_source_scene_id(self):
        matrix = {
            "buckets": [
                {
                    "id": "alpha-sensitive-effects",
                    "minimumStatus": "active",
                    "coverage": [{"sceneId": "3228578419", "status": "active"}],
                }
            ]
        }
        manifest = {
            "scenes": [
                {
                    "id": "arona-template",
                    "sourceSceneId": "3228578419",
                    "name": "Sleeping Arona Template",
                    "variants": [
                        {"id": "3228578419-day", "name": "Day", "gates": ["quick"], "baselinePrefix": "3228578419-day"},
                    ],
                }
            ]
        }

        self.assertEqual(runner.validate_coverage_matrix(matrix, manifest), [])

    def test_validate_coverage_matrix_requires_active_status_for_manifest_sources(self):
        matrix = {
            "version": 1,
            "buckets": [
                {
                    "id": "scene-script-bindings",
                    "name": "SceneScript Bindings And Runtime",
                    "minimumStatus": "candidate",
                    "coverage": [
                        {
                            "sceneId": "3326873240",
                            "status": "candidate",
                            "source": "doc",
                            "notes": "stale after manifest promotion",
                        }
                    ],
                }
            ],
        }
        manifest = {
            "version": 1,
            "paths": {},
            "scenes": [
                {
                    "id": "3326873240",
                    "name": "Elaina Template",
                    "source": "${workshop}/3326873240/scene.pkg",
                    "variants": [
                        {
                            "id": "3326873240-day",
                            "name": "Day",
                            "gates": ["deep"],
                            "baselinePrefix": "3326873240-day",
                        }
                    ],
                }
            ],
        }

        self.assertEqual(
            runner.validate_coverage_matrix(matrix, manifest),
            [
                "active scene source 3326873240 is present in smoke manifest but best coverage status is candidate"
            ],
        )

    def test_manifest_includes_elaina_time_variants_as_deep_candidates(self):
        manifest_path = Path(__file__).resolve().parent / "scenes.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

        cases = runner.expand_manifest_scenes(manifest)
        elaina_cases = [case for case in cases if case.get("sourceSceneId") == "3326873240"]

        self.assertEqual(
            [case["id"] for case in elaina_cases],
            [
                "3326873240-morning",
                "3326873240-day",
                "3326873240-dusk",
                "3326873240-night",
                "3326873240-gradient",
            ],
        )
        self.assertEqual([case.get("gates") for case in elaina_cases], [["deep"]] * 5)
        self.assertEqual(
            [case["scenePropertyOverrides"] for case in elaina_cases],
            [
                {"timevarying": False, "display": "0"},
                {"timevarying": False, "display": "1"},
                {"timevarying": False, "display": "2"},
                {"timevarying": False, "display": "3"},
                {"timevarying": False, "display": "4"},
            ],
        )
        self.assertEqual(
            [case["captures"][0]["baseline"] for case in elaina_cases],
            [
                "3326873240-morning/stills/still-10000.png",
                "3326873240-day/stills/still-10000.png",
                "3326873240-dusk/stills/still-10000.png",
                "3326873240-night/stills/still-10000.png",
                "3326873240-gradient/stills/still-10000.png",
            ],
        )

    def test_manifest_scopes_arona_night_review_threshold_to_variant(self):
        manifest_path = Path(__file__).resolve().parent / "scenes.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

        cases = runner.expand_manifest_scenes(manifest)
        day = next(case for case in cases if case.get("id") == "3228578419-day")
        night = next(case for case in cases if case.get("id") == "3228578419-night")

        self.assertEqual(day["thresholds"]["rmseReview"], 0.035)
        self.assertEqual(night["thresholds"]["rmseReview"], 0.04)

    def test_manifest_includes_fluorescent_beach_as_deep_overlay_video_fixture(self):
        manifest_path = Path(__file__).resolve().parent / "scenes.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

        cases = runner.expand_manifest_scenes(manifest)
        scene = next((case for case in cases if case.get("id") == "2788691565"), None)

        self.assertIsNotNone(scene)
        self.assertEqual(scene["name"], "Girl and Fluorescent Beach")
        self.assertEqual(scene["gates"], ["deep"])
        self.assertFalse(scene["required"])
        self.assertEqual(scene["features"], ["overlay-video-texture", "water-effects", "particles"])
        self.assertEqual(
            scene["sequences"],
            [
                {
                    "name": "motion-10000",
                    "startMs": 10000,
                    "frames": 60,
                    "intervalMs": 33,
                    "baselineDir": "2788691565/sequences/motion-10000",
                    "temporalToleranceFrames": 4,
                }
            ],
        )

    def test_manifest_includes_alya_as_deep_scene_script_text_fixture(self):
        manifest_path = Path(__file__).resolve().parent / "scenes.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

        cases = runner.expand_manifest_scenes(manifest)
        scene = next((case for case in cases if case.get("id") == "3301291394"), None)

        self.assertIsNotNone(scene)
        self.assertEqual(scene["name"], "Alya Clock and Date")
        self.assertEqual(scene["gates"], ["deep"])
        self.assertFalse(scene["required"])
        self.assertEqual(scene["features"], ["scene-script", "generated-text", "user-properties"])
        self.assertEqual(
            scene["captures"],
            [
                {
                    "name": "still-10000",
                    "timeMs": 10000,
                    "baseline": "3301291394/stills/still-10000.png",
                }
            ],
        )

    def test_manifest_includes_web_and_video_candidates_as_deep_fixtures(self):
        manifest_path = Path(__file__).resolve().parent / "scenes.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

        cases = {case["id"]: case for case in runner.expand_manifest_scenes(manifest)}

        rain = cases.get("779812076")
        visualizer = cases.get("893418273")
        timedrift = cases.get("874499201")
        cwav = cases.get("1509243786")

        self.assertIsNotNone(rain)
        self.assertEqual(rain["name"], "Rain Drops")
        self.assertEqual(rain["projectType"], "web")
        self.assertEqual(rain["gates"], ["deep"])
        self.assertEqual(rain["features"], ["web-wallpaper", "web-video", "canvas-animation"])
        self.assertEqual(rain["source"], "${workshop}/779812076/index.html")
        self.assertEqual(rain["sequences"][0]["baselineDir"], "779812076/sequences/rain-10000")
        self.assertNotIn("reviewVideo", rain)

        self.assertIsNotNone(visualizer)
        self.assertEqual(visualizer["name"], "Audio Visualizer")
        self.assertEqual(visualizer["projectType"], "web")
        self.assertEqual(visualizer["gates"], ["deep"])
        self.assertIn("--debug-synthetic-audio", visualizer["harnessArgs"])
        self.assertEqual(visualizer["features"], ["web-wallpaper", "audio-visualizer", "synthetic-audio"])
        self.assertEqual(visualizer["sequences"][0]["baselineDir"], "893418273/sequences/audio-10000")
        self.assertGreaterEqual(visualizer["thresholds"]["rmseReview"], 0.18)
        self.assertGreaterEqual(visualizer["thresholds"]["rmseFail"], 0.26)
        self.assertEqual(visualizer["thresholds"]["minMotionRmse"], 0.004)
        self.assertNotIn("reviewVideo", visualizer)

        self.assertIsNotNone(timedrift)
        self.assertEqual(timedrift["name"], "TIMDRIFT II Mountains Clouds")
        self.assertEqual(timedrift["projectType"], "video")
        self.assertEqual(timedrift["gates"], ["deep"])
        self.assertEqual(timedrift["features"], ["we-video-wallpaper", "video-motion"])
        self.assertEqual(
            timedrift["source"],
            "${workshop}/874499201/TIMEDRIFT II - DOLOMITES 4K.mp4",
        )
        self.assertEqual(timedrift["sequences"][0]["baselineDir"], "874499201/sequences/video-90000")
        self.assertNotIn("reviewVideo", timedrift)

        self.assertIsNone(cwav)

    def test_coverage_bucket_summary_accepts_candidate_for_phase_one(self):
        matrix = {
            "version": 1,
            "buckets": [
                {
                    "id": "scene-script-bindings",
                    "name": "SceneScript Bindings And Runtime",
                    "minimumStatus": "candidate",
                    "coverage": [{"sceneId": "3301291394", "status": "candidate", "source": "doc", "notes": "script"}],
                }
            ],
        }

        summaries = runner.coverage_bucket_summaries(matrix)

        self.assertEqual(summaries[0]["id"], "scene-script-bindings")
        self.assertEqual(summaries[0]["bestStatus"], "candidate")
        self.assertTrue(summaries[0]["satisfied"])

    def test_format_coverage_markdown_includes_bucket_scene_and_status(self):
        summaries = [
            {
                "id": "main-video-texture",
                "name": "Main Embedded Video Texture",
                "minimumStatus": "active",
                "bestStatus": "active",
                "satisfied": True,
                "sceneIds": ["3327063360"],
            }
        ]

        markdown = runner.format_coverage_markdown(summaries)

        self.assertIn("| main-video-texture | Main Embedded Video Texture | active | active | yes | 3327063360 |", markdown)

    def test_main_coverage_prints_markdown_without_render_dependencies(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "smoke-tests").mkdir()
            manifest = root / "smoke-tests" / "scenes.json"
            matrix = root / "smoke-tests" / "coverage-matrix.json"
            manifest.write_text(json.dumps({"version": 1, "scenes": [{"id": "1"}]}), encoding="utf-8")
            matrix.write_text(json.dumps({
                "version": 1,
                "buckets": [
                    {
                        "id": "bucket",
                        "name": "Bucket",
                        "minimumStatus": "active",
                        "coverage": [{"sceneId": "1", "status": "active", "source": "test", "notes": "ok"}],
                    }
                ],
            }), encoding="utf-8")

            output = io.StringIO()
            with (
                mock.patch.object(runner, "repo_root", return_value=root),
                mock.patch.object(runner, "require_imagemagick") as require_imagemagick,
                mock.patch.object(runner, "find_ffmpeg") as find_ffmpeg,
                redirect_stdout(output),
            ):
                code = runner.main(["--coverage"])

            self.assertEqual(code, 0)
            self.assertIn("| bucket | Bucket | active | active | yes | 1 |", output.getvalue())
            require_imagemagick.assert_not_called()
            find_ffmpeg.assert_not_called()


class WebBackgroundQmlTests(unittest.TestCase):
    def web_background_source(self) -> str:
        return (Path(__file__).resolve().parents[1] / "wallpapers/io.team7.yakkai/contents/ui/WebBackground.qml").read_text(
            encoding="utf-8"
        )

    def test_web_property_delivery_catches_wallpaper_listener_exceptions(self):
        source = self.web_background_source()

        self.assertIn("function deliverToPropertyListener()", source)
        self.assertIn('console.error("[Yakkai] WE web property listener failed", error);', source)
        self.assertIn("return false;", source)

    def test_web_property_delivery_retries_after_failed_apply(self):
        source = self.web_background_source()

        self.assertIn("property int propertyPushAttempts: 0", source)
        self.assertIn("readonly property int maxPropertyPushAttempts:", source)
        self.assertIn("Timer {\n        id: propertyRetryTimer", source)
        self.assertIn("propertyRetryTimer.restart()", source)
        self.assertIn("propertyRetryTimer.stop()", source)

    def test_web_view_defers_initial_load_until_compat_script_is_installed(self):
        source = self.web_background_source()

        self.assertIn("property bool compatScriptInstalled: false", source)
        self.assertIn("function loadWebSource()", source)
        self.assertIn('url: ""', source)
        self.assertIn("root.compatScriptInstalled = true", source)
        self.assertIn("root.loadWebSource()", source)

    def test_web_backend_reports_runtime_property_diagnostics(self):
        background_source = self.web_background_source()
        harness_source = (Path(__file__).resolve().parents[1] / "native/scene_harness/qml/WebWallpaperHarness.qml").read_text(
            encoding="utf-8"
        )

        self.assertIn('property string runtimeDiagnostics: ""', background_source)
        self.assertIn("function updateRuntimeDiagnostics()", background_source)
        self.assertIn("id: runtimeDiagnosticsTimer", background_source)
        self.assertIn("hasShim", background_source)
        self.assertIn("latestUserKeys", background_source)
        self.assertIn("+ \" diagnostics=\" + web.runtimeDiagnostics", harness_source)

    def test_web_background_diagnostics_overlay_is_opt_in(self):
        source = self.web_background_source()

        self.assertIn("property bool showDiagnosticsOverlay: false", source)
        self.assertIn("function shouldShowDiagnosticOverlay()", source)
        self.assertIn("return showDiagnosticsOverlay && diagnosticText.length > 0", source)
        self.assertIn("visible: root.shouldShowDiagnosticOverlay()", source)
        self.assertIn("running: root.pageLoaded && root.showDiagnosticsOverlay && !root.propertiesSent", source)

    def test_web_property_shim_exposes_wallpaper_engine_media_listener_apis(self):
        source = self.web_background_source()

        self.assertIn("signalMediaProperties", source)
        self.assertIn("window.wallpaperRegisterMediaPropertiesListener", source)
        self.assertIn("window.wallpaperRegisterMediaThumbnailListener", source)
        self.assertIn("window.wallpaperRegisterMediaTimelineListener", source)
        self.assertIn("window.wallpaperRegisterMediaPlaybackListener", source)

    def test_web_compat_shim_exposes_wallpaper_engine_viewport_globals(self):
        source = self.web_background_source()

        self.assertIn("function updateWallpaperEngineViewportGlobals()", source)
        self.assertIn("window.width = window.innerWidth;", source)
        self.assertIn("window.height = window.innerHeight;", source)
        self.assertIn('window.addEventListener("resize", updateWallpaperEngineViewportGlobals);', source)

    def test_web_background_synthetic_audio_uses_page_owned_timer(self):
        source = self.web_background_source()
        stop_body = source.split("function stopSyntheticAudio()", 1)[1].split("\n    onWebSourceChanged:", 1)[0]
        load_started_body = source.split("if (loadRequest.status === WebEngineView.LoadStartedStatus)", 1)[1].split(
            "if (loadRequest.status === WebEngineView.LoadSucceededStatus)", 1
        )[0]
        load_failed_body = source.split("if (loadRequest.status === WebEngineView.LoadFailedStatus)", 1)[1].split(
            "root.loadErrorText =", 1
        )[0]

        self.assertIn("function startSyntheticAudio()", source)
        self.assertIn("function stopSyntheticAudio()", source)
        self.assertIn("function scheduleSyntheticAudioRetry()", source)
        self.assertIn("window.__yakkaiSyntheticAudioTimer", source)
        self.assertIn("clearInterval(window.__yakkaiSyntheticAudioTimer)", source)
        self.assertIn("id: syntheticAudioRetryTimer", source)
        self.assertIn("syntheticAudioRetryTimer.restart()", source)
        self.assertIn("syntheticAudioRetryTimer.stop()", source)
        self.assertNotIn("if (!pageLoaded)", stop_body)
        self.assertIn("root.stopSyntheticAudio()", load_started_body)
        self.assertLess(load_started_body.index("root.stopSyntheticAudio()"), load_started_body.index("root.pageLoaded = false"))
        self.assertIn("root.stopSyntheticAudio()", load_failed_body)
        self.assertLess(load_failed_body.index("root.stopSyntheticAudio()"), load_failed_body.index("root.pageLoaded = false"))
        self.assertNotIn("id: syntheticAudioTimer", source)

    def test_web_background_synthetic_audio_phase_is_deterministic_from_origin(self):
        source = self.web_background_source()
        start_synthetic_body = source.split("function startSyntheticAudio()", 1)[1].split("\n    function stopSyntheticAudio()", 1)[0]

        self.assertIn("property real debugSyntheticAudioOriginMs: 0", source)
        self.assertIn("const originMs = debugSyntheticAudioOriginMs > 0 ? debugSyntheticAudioOriginMs : Date.now()", start_synthetic_body)
        self.assertIn("window.__yakkaiSyntheticAudioOriginMs = ${originMs};", start_synthetic_body)
        self.assertIn("function emitSyntheticAudio()", start_synthetic_body)
        self.assertIn("const elapsedIntervals = Math.max(0, (Date.now() - window.__yakkaiSyntheticAudioOriginMs) / ${interval});", start_synthetic_body)
        self.assertIn("const phase = elapsedIntervals * ${phaseRate};", start_synthetic_body)
        self.assertNotIn("window.__yakkaiSyntheticAudioPhase = window.__yakkaiSyntheticAudioPhase || 0", start_synthetic_body)
        self.assertNotIn("window.__yakkaiSyntheticAudioPhase += ${phaseRate}", start_synthetic_body)

    def test_web_background_has_capture_exit_cleanup(self):
        source = self.web_background_source()
        push_properties_body = source.split("function pushProperties()", 1)[1].split("\n    function schedulePropertyRetry()", 1)[0]
        schedule_property_retry_body = source.split("function schedulePropertyRetry()", 1)[1].split("\n    function loadWebSource()", 1)[0]
        schedule_synthetic_retry_body = source.split("function scheduleSyntheticAudioRetry()", 1)[1].split(
            "\n    function startSyntheticAudio()", 1
        )[0]
        start_synthetic_body = source.split("function startSyntheticAudio()", 1)[1].split("\n    function stopSyntheticAudio()", 1)[0]
        prepare_exit_body = source.split("function prepareForCaptureExit()", 1)[1].split("\n    onWebSourceChanged:", 1)[0]
        load_succeeded_body = source.split("if (loadRequest.status === WebEngineView.LoadSucceededStatus)", 1)[1].split(
            "if (loadRequest.status === WebEngineView.LoadFailedStatus)", 1
        )[0]

        self.assertIn("property bool captureExiting: false", source)
        self.assertIn("function prepareForCaptureExit()", source)
        self.assertIn("propertyRetryTimer.stop()", source)
        self.assertIn("runtimeDiagnosticsTimer.stop()", source)
        self.assertIn("stopSyntheticAudio()", source)
        self.assertIn("syntheticAudioRetryTimer.stop()", source)
        self.assertIn("if (captureExiting || !pageLoaded)", push_properties_body)
        self.assertIn("if (captureExiting || !pageLoaded || propertiesSent)", schedule_property_retry_body)
        self.assertIn("if (captureExiting || !pageLoaded || !debugSyntheticAudioEnabled)", schedule_synthetic_retry_body)
        self.assertIn("if (captureExiting || !pageLoaded)", start_synthetic_body)
        self.assertIn("if (root.captureExiting)", load_succeeded_body)
        self.assertIn("window.__yakkaiSyntheticAudioTimer", prepare_exit_body)
        self.assertIn("clearInterval(window.__yakkaiSyntheticAudioTimer)", prepare_exit_body)
        self.assertEqual(prepare_exit_body.count("webView.runJavaScript(`"), 1)
        self.assertIn("audioMuted: root.captureExiting || root.muted", source)

    def test_web_harness_capture_exit_does_not_clear_web_source(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / "native/scene_harness/qml/WebWallpaperHarness.qml").read_text(encoding="utf-8")

        self.assertIn("function prepareForCaptureExit()", source)
        self.assertIn("web.prepareForCaptureExit()", source)
        self.assertNotIn("web.webSource = \"\"", source)

    def test_web_harness_forwards_synthetic_audio_properties(self):
        root = Path(__file__).resolve().parents[1]
        harness_source = (root / "native/scene_harness/qml/WebWallpaperHarness.qml").read_text(encoding="utf-8")
        main_source = (root / "native/scene_harness/qml/Main.qml").read_text(encoding="utf-8")

        self.assertIn("property bool debugSyntheticAudioEnabled: false", harness_source)
        self.assertIn("debugSyntheticAudioEnabled: root.debugSyntheticAudioEnabled", harness_source)
        self.assertIn("debugSyntheticAudioBins: root.debugSyntheticAudioBins", harness_source)
        self.assertIn("debugSyntheticAudioIntervalMs: root.debugSyntheticAudioIntervalMs", harness_source)
        self.assertIn("sceneHarnessDebugSyntheticAudioEnabled", main_source)
        self.assertIn("sceneHarnessDebugSyntheticAudioBins", main_source)
        self.assertIn("sceneHarnessDebugSyntheticAudioIntervalMs", main_source)

    def test_web_harness_arms_synthetic_audio_at_capture_ready_boundary(self):
        root = Path(__file__).resolve().parents[1]
        harness_source = (root / "native/scene_harness/qml/WebWallpaperHarness.qml").read_text(encoding="utf-8")
        settle_trigger_body = harness_source.split("onTriggered:", 1)[1].split("\n    WebBackground", 1)[0]

        self.assertIn("property bool syntheticAudioArmed: false", harness_source)
        self.assertIn("property real syntheticAudioOriginMs: 0", harness_source)
        self.assertIn("debugSyntheticAudioEnabled: root.debugSyntheticAudioEnabled && root.syntheticAudioArmed", harness_source)
        self.assertIn("debugSyntheticAudioOriginMs: root.syntheticAudioOriginMs", harness_source)
        self.assertIn("root.syntheticAudioOriginMs = Date.now()", settle_trigger_body)
        self.assertIn("root.syntheticAudioArmed = true", settle_trigger_body)
        self.assertLess(settle_trigger_body.index("root.syntheticAudioArmed = true"), settle_trigger_body.index("root.firstFrameReady()"))

class SceneHarnessSourceTests(unittest.TestCase):
    def scene_harness_source(self) -> str:
        return (Path(__file__).resolve().parents[1] / "native/scene_harness/src/main.cpp").read_text(encoding="utf-8")

    def test_scene_harness_cli_exposes_synthetic_audio_flags(self):
        source = self.scene_harness_source()

        self.assertIn('"debug-synthetic-audio"', source)
        self.assertIn('"debug-synthetic-audio-bins"', source)
        self.assertIn('"debug-synthetic-audio-interval-ms"', source)
        self.assertIn("sceneHarnessDebugSyntheticAudioEnabled", source)
        self.assertIn("sceneHarnessDebugSyntheticAudioBins", source)
        self.assertIn("sceneHarnessDebugSyntheticAudioIntervalMs", source)
        self.assertIn("invalid --debug-synthetic-audio-bins value", source)
        self.assertIn("invalid --debug-synthetic-audio-interval-ms value", source)

    def test_scene_harness_cli_exposes_capture_exit_mode(self):
        source = self.scene_harness_source()
        request_capture_exit_body = source.split("void requestCaptureExit(", 1)[1].split("\nvoid scheduleSingleCapture", 1)[0]
        capture_exit_option_body = source.split("QCommandLineOption captureExitModeOption(", 1)[1].split("\n    );", 1)[0]
        validation_body = source.split("if (!captureExitMode)", 1)[1].split("if (!debugSyntheticAudioBins", 1)[0]
        debug_capture_rejection_body = source.split(
            "if (*captureExitMode == CaptureExitMode::Immediate && debugEffectCapturesRequested)", 1
        )[1].split("if (debugEffectCapturesRequested && backend", 1)[0]

        self.assertIn('"capture-exit-mode"', source)
        self.assertIn("graceful|immediate", capture_exit_option_body)
        self.assertIn('QStringLiteral("graceful")', capture_exit_option_body)
        self.assertIn("CaptureExitMode::Immediate", source)
        self.assertIn("if (exitMode == CaptureExitMode::Immediate)", request_capture_exit_body)
        self.assertIn("std::_Exit(status)", request_capture_exit_body)
        self.assertLess(request_capture_exit_body.index("std::fflush(stderr);"), request_capture_exit_body.index("std::_Exit(status)"))
        self.assertIn(
            "requestCaptureExit(guardedWindow, saveWindowCapture(guardedWindow, absoluteCapturePath), exitMode)",
            source,
        )
        self.assertIn("requestCaptureExit(guardedWindow, *failed ? 5 : 0, exitMode)", source)
        self.assertIn("invalid --capture-exit-mode", validation_body)
        self.assertIn("--capture-exit-mode immediate cannot be combined with --debug-effect-captures", debug_capture_rejection_body)
        self.assertIn("const CaptureExitMode selectedCaptureExitMode = *captureExitMode;", source)

    def test_scene_harness_cli_exposes_live_recording_flags(self):
        source = self.scene_harness_source()

        self.assertIn('"record"', source)
        self.assertIn('"record-duration-ms"', source)
        self.assertIn('"record-fps"', source)
        self.assertIn('"record-start-delay-ms"', source)
        self.assertIn("scheduleWindowRecording", source)
        self.assertIn("QProcess", source)
        self.assertIn("ffmpeg", source)
        self.assertIn("rawvideo", source)
        self.assertIn("pipe:0", source)
        self.assertIn("QImage::Format_RGBA8888", source)
        self.assertIn("--record cannot be combined with --capture", source)
        self.assertIn("startCaptureTimersAfterFirstFrame(&app, guardedWindow", source)


if __name__ == "__main__":
    unittest.main()
