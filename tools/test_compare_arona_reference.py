import json
import io
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import compare_arona_reference as comparator
import runner as smoke_runner


class AronaReferenceComparatorTests(unittest.TestCase):
    def test_variants_match_we_timeofday_values_and_reference_paths(self):
        self.assertEqual([variant.name for variant in comparator.VARIANTS], ["day", "sunset", "night"])
        self.assertEqual([variant.timeofday for variant in comparator.VARIANTS], ["1", "2", "3"])
        self.assertEqual(
            [variant.reference_relative_path.as_posix() for variant in comparator.VARIANTS],
            ["day/still.png", "sunset/still.png", "night/still.png"],
        )

    def test_scene_properties_json_merges_project_defaults_with_timeofday_override(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "scene.pkg"
            project = source.parent / "project.json"
            source.write_text("scene", encoding="utf-8")
            project.write_text(
                json.dumps(
                    {
                        "general": {
                            "properties": {
                                "timeofday": {"type": "combo", "value": "1"},
                                "rain": {"type": "bool", "value": True},
                            }
                        }
                    }
                ),
                encoding="utf-8",
            )

            payload = json.loads(comparator.scene_properties_json(comparator.VARIANTS[1], source))

        self.assertEqual(payload["timeofday"]["value"], "2")
        self.assertEqual(payload["rain"]["value"], True)

    def test_worst_status_orders_review_between_skip_and_pass(self):
        self.assertEqual(comparator.worst_status("pass", "review"), "review")
        self.assertEqual(comparator.worst_status("review", "skip"), "skip")
        self.assertEqual(comparator.worst_status("skip", "fail"), "fail")
        self.assertEqual(comparator.worst_status("fail", "pass"), "fail")

    def test_montage_command_uses_imagemagick_7_style_magick_binary(self):
        commands = smoke_runner.ImageMagickCommands(
            identify=["/usr/bin/magick", "identify"],
            convert=["/usr/bin/magick"],
            compare=["/usr/bin/magick", "compare"],
        )

        self.assertEqual(comparator.montage_command(commands), ["/usr/bin/magick", "montage"])

    def test_montage_command_uses_legacy_montage_binary(self):
        commands = smoke_runner.ImageMagickCommands(
            identify=["/usr/bin/identify"],
            convert=["/usr/bin/convert"],
            compare=["/usr/bin/compare"],
        )

        with mock.patch.object(comparator.shutil, "which", return_value="/usr/bin/montage"):
            self.assertEqual(comparator.montage_command(commands), ["/usr/bin/montage"])

    def test_parse_args_disables_debug_effect_captures_by_default(self):
        args = comparator.parse_args([])

        self.assertFalse(args.debug_effect_captures)

    def test_parse_args_exposes_debug_effect_captures_flag(self):
        args = comparator.parse_args(["--debug-effect-captures"])

        self.assertTrue(args.debug_effect_captures)

    def test_parse_args_exposes_debug_effect_probe_layers(self):
        args = comparator.parse_args(["--debug-effect-captures", "--debug-effect-probe-layers", "405, 168"])

        self.assertEqual(args.debug_effect_probe_layers, "405, 168")

    def test_parse_args_exposes_debug_effect_probe_max_effects(self):
        args = comparator.parse_args(
            [
                "--debug-effect-captures",
                "--debug-effect-probe-layers",
                "405",
                "--debug-effect-probe-max-effects",
                "2",
            ]
        )

        self.assertEqual(args.debug_effect_probe_max_effects, 2)

    def test_parse_args_exposes_debug_effect_probe_channelmap_slots(self):
        args = comparator.parse_args(
            [
                "--debug-effect-captures",
                "--debug-effect-probe-layers",
                "405",
                "--debug-effect-probe-channelmap-slots",
                "0, 2, 15",
            ]
        )

        self.assertEqual(args.debug_effect_probe_channelmap_slots, "0, 2, 15")

    def test_parse_args_exposes_debug_puppet_animation_layer_overrides(self):
        args = comparator.parse_args(
            [
                "--debug-effect-captures",
                "--debug-puppet-animation-layer-overrides",
                "405:781:paused=false",
            ]
        )

        self.assertEqual(args.debug_puppet_animation_layer_overrides, "405:781:paused=false")

    def test_main_rejects_debug_puppet_animation_layer_overrides_without_effect_captures(self):
        with mock.patch.object(comparator, "compare_all") as compare_all, \
             mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
            code = comparator.main(
                [
                    "--debug-puppet-animation-layer-overrides",
                    "405:781:paused=false",
                ]
            )

        self.assertEqual(code, 1)
        self.assertIn("--debug-puppet-animation-layer-overrides requires --debug-effect-captures", stderr.getvalue())
        compare_all.assert_not_called()

    def test_main_rejects_quarantined_debug_effect_probe_channelmap_slots(self):
        with mock.patch.object(comparator, "compare_all") as compare_all, \
             mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
            code = comparator.main(
                [
                    "--debug-effect-captures",
                    "--debug-effect-probe-layers",
                    "405",
                    "--debug-effect-probe-channelmap-slots",
                    "0,2",
                ]
            )

        self.assertEqual(code, 1)
        self.assertIn("quarantined", stderr.getvalue())
        compare_all.assert_not_called()

    def test_parse_args_exposes_skip_registration_flag(self):
        args = comparator.parse_args(["--skip-registration"])

        self.assertTrue(args.skip_registration)

    def test_write_summary_serializes_result_fields_and_variant_metrics(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "summary.json"
            result = comparator.CompareResult(
                status="review",
                message="compared Arona WE references",
                outputDir="/tmp/out",
                contactSheet="/tmp/out/contact.png",
                registeredContactSheet="/tmp/out/contact-registered.png",
                summary=str(out),
                variants=[
                    comparator.VariantResult(
                        name="day",
                        status="review",
                        reference="/refs/day/still.png",
                        yakkai="/tmp/out/day/yakkai.png",
                        normalizedReference="/tmp/out/day/reference.normalized.png",
                        diff="/tmp/out/day/diff.png",
                        registeredYakkai="/tmp/out/day/yakkai.registered.png",
                        registeredDiff="/tmp/out/day/registered-diff.png",
                        structureReference="/tmp/out/day/reference.structure.png",
                        registeredStructureYakkai="/tmp/out/day/yakkai.registered.structure.png",
                        registeredStructureDiff="/tmp/out/day/registered-structure-diff.png",
                        effectManifest="/tmp/out/day/effect-captures/manifest.json",
                        metrics={
                            "rmse": 0.125,
                            "registeredRmse": 0.075,
                            "registrationStructureRmse": 0.055,
                            "registrationMetric": "grayscale-contrast-320x180",
                            "registrationScale": 1.08,
                            "registrationOffsetX": 16,
                            "registrationOffsetY": -8,
                            "referenceMeanRgb": [0.1, 0.2, 0.3],
                        },
                    ),
                    comparator.VariantResult(
                        name="sunset",
                        status="fail",
                        reference="/refs/sunset/still.png",
                        metrics={"error": "render failed before manifest"},
                    )
                ],
            )

            comparator.write_summary(result, out)
            data = json.loads(out.read_text(encoding="utf-8"))

        self.assertEqual(data["status"], "review")
        self.assertEqual(data["registeredContactSheet"], "/tmp/out/contact-registered.png")
        self.assertEqual(data["variants"][0]["status"], "review")
        self.assertEqual(data["variants"][0]["metrics"]["rmse"], 0.125)
        self.assertEqual(data["variants"][0]["metrics"]["registeredRmse"], 0.075)
        self.assertEqual(data["variants"][0]["metrics"]["registrationStructureRmse"], 0.055)
        self.assertEqual(data["variants"][0]["metrics"]["registrationMetric"], "grayscale-contrast-320x180")
        self.assertEqual(data["variants"][0]["normalizedReference"], "/tmp/out/day/reference.normalized.png")
        self.assertEqual(data["variants"][0]["registeredYakkai"], "/tmp/out/day/yakkai.registered.png")
        self.assertEqual(data["variants"][0]["registeredDiff"], "/tmp/out/day/registered-diff.png")
        self.assertEqual(data["variants"][0]["structureReference"], "/tmp/out/day/reference.structure.png")
        self.assertEqual(data["variants"][0]["registeredStructureYakkai"], "/tmp/out/day/yakkai.registered.structure.png")
        self.assertEqual(data["variants"][0]["registeredStructureDiff"], "/tmp/out/day/registered-structure-diff.png")
        self.assertEqual(data["variants"][0]["effectManifest"], "/tmp/out/day/effect-captures/manifest.json")
        self.assertNotIn("effectManifest", data["variants"][1])

    def test_clear_shader_cache_removes_only_child_spvs01_directories(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            removed_dir = root / "scene-a" / "spvs01"
            kept_sibling = root / "scene-a" / "spvs02"
            kept_nested = root / "scene-b" / "nested" / "spvs01"
            removed_dir.mkdir(parents=True)
            kept_sibling.mkdir()
            kept_nested.mkdir(parents=True)
            (removed_dir / "shader.spv").write_text("stale", encoding="utf-8")

            removed = comparator.clear_shader_cache(root)

            self.assertEqual(removed, [removed_dir])
            self.assertFalse(removed_dir.exists())
            self.assertTrue(kept_sibling.exists())
            self.assertTrue(kept_nested.exists())

    def test_compare_all_clears_shader_cache_before_rendering(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            reference_root = root / "refs"
            for variant in comparator.VARIANTS:
                still = reference_root / variant.reference_relative_path
                still.parent.mkdir(parents=True, exist_ok=True)
                still.write_text("png", encoding="utf-8")
            harness = root / "harness"
            source = root / "scene.pkg"
            assets = root / "assets"
            harness.write_text("harness", encoding="utf-8")
            source.write_text("scene", encoding="utf-8")
            assets.mkdir()
            events = []

            def fake_render(variant, config, *, harness, source, assets):
                events.append(f"render:{variant.name}")
                return comparator.VariantResult(
                    name=variant.name,
                    status="fail",
                    reference=str(config.reference_root / variant.reference_relative_path),
                    metrics={"error": "skip expensive render"},
                )

            with (
                mock.patch.object(comparator, "default_harness", return_value=harness),
                mock.patch.object(comparator, "default_source", return_value=source),
                mock.patch.object(comparator, "default_assets", return_value=assets),
                mock.patch.object(comparator, "clear_shader_cache", side_effect=lambda cache_root: events.append("clear") or []),
                mock.patch.object(comparator, "render_variant", side_effect=fake_render),
                mock.patch.object(comparator.smoke_runner, "require_imagemagick", side_effect=RuntimeError("not needed")),
            ):
                result = comparator.compare_all(comparator.CompareConfig(root, reference_root, root / "out"))

        self.assertEqual(events[:2], ["clear", "render:day"])
        self.assertEqual(result.status, "fail")

    def test_compare_all_fails_before_render_when_shader_cache_clear_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            reference_root = root / "refs"
            for variant in comparator.VARIANTS:
                still = reference_root / variant.reference_relative_path
                still.parent.mkdir(parents=True, exist_ok=True)
                still.write_text("png", encoding="utf-8")
            harness = root / "harness"
            source = root / "scene.pkg"
            assets = root / "assets"
            harness.write_text("harness", encoding="utf-8")
            source.write_text("scene", encoding="utf-8")
            assets.mkdir()

            with (
                mock.patch.object(comparator, "default_harness", return_value=harness),
                mock.patch.object(comparator, "default_source", return_value=source),
                mock.patch.object(comparator, "default_assets", return_value=assets),
                mock.patch.object(comparator, "clear_shader_cache", side_effect=OSError("permission denied")),
                mock.patch.object(comparator, "render_variant") as render_variant,
            ):
                result = comparator.compare_all(comparator.CompareConfig(root, reference_root, root / "out"))

        self.assertEqual(result.status, "fail")
        self.assertIn("failed to clear shader cache", result.message)
        render_variant.assert_not_called()

    def test_missing_reference_root_skips_without_rendering(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            config = comparator.CompareConfig(
                repo_root=root,
                reference_root=root / "missing",
                output_root=root / "out",
                capture_delay_ms=8000,
                width=1280,
                height=720,
            )

            result = comparator.compare_all(config)

            self.assertFalse((root / "out").exists())

        self.assertEqual(result.status, "skip")
        self.assertIn("reference root not found", result.message)
        self.assertEqual(result.variants, [])

    def test_missing_reference_still_skips_without_rendering(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            reference_root = root / "yakkai_arona"
            (reference_root / "day").mkdir(parents=True)
            config = comparator.CompareConfig(
                repo_root=root,
                reference_root=reference_root,
                output_root=root / "out",
                capture_delay_ms=8000,
                width=1280,
                height=720,
            )

            result = comparator.compare_all(config)

            self.assertFalse((root / "out").exists())

        self.assertEqual(result.status, "skip")
        self.assertIn("missing reference stills", result.message)
        self.assertEqual(result.variants, [])

    def test_render_variant_uses_harness_with_expected_scene_properties(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "build/native/scene_harness/yakkai_scene_harness"
            source = root / "steam/workshop/3228578419/scene.pkg"
            assets = root / "steam/common/wallpaper_engine/assets"
            harness.parent.mkdir(parents=True)
            source.parent.mkdir(parents=True)
            assets.mkdir(parents=True)
            harness.write_text("harness", encoding="utf-8")
            source.write_text("scene", encoding="utf-8")
            (source.parent / "project.json").write_text(
                json.dumps(
                    {
                        "general": {
                            "properties": {
                                "timeofday": {"type": "combo", "value": "1"},
                                "rain": {"type": "bool", "value": True},
                            }
                        }
                    }
                ),
                encoding="utf-8",
            )
            out_dir = root / "out"
            commands = []

            def fake_run(command, log_path, timeout_seconds):
                commands.append((command, log_path, timeout_seconds))
                capture = Path(command[command.index("--capture") + 1])
                if not capture.parent.is_dir():
                    raise AssertionError(f"capture parent missing before harness run: {capture.parent}")
                if "--debug-effect-captures" in command:
                    captures_dir = Path(command[command.index("--debug-effect-captures") + 1])
                    captures_dir.mkdir(parents=True, exist_ok=True)
                    (captures_dir / "manifest.json").write_text(
                        json.dumps({"status": "ok", "layers": [], "strippedCandidates": []}),
                        encoding="utf-8",
                    )
                capture.write_text("png", encoding="utf-8")
                return 0

            result = comparator.render_variant(
                comparator.VARIANTS[1],
                comparator.CompareConfig(
                    repo_root=root,
                    reference_root=root / "yakkai_arona",
                    output_root=out_dir,
                    debug_effect_captures=True,
                    debug_effect_probe_layers=(405,),
                    debug_effect_probe_max_effects=2,
                    debug_puppet_animation_layer_overrides="405:781:paused=false",
                ),
                harness=harness,
                source=source,
                assets=assets,
                run_command=fake_run,
            )

        self.assertEqual(result.status, "pass")
        command, log_path, timeout_seconds = commands[0]
        self.assertIn("--capture", command)
        self.assertIn("--capture-delay-ms", command)
        self.assertEqual(command[command.index("--capture-delay-ms") + 1], "8000")
        properties = json.loads(command[command.index("--scene-properties-json") + 1])
        self.assertEqual(properties["timeofday"]["value"], "2")
        self.assertEqual(properties["rain"]["value"], True)
        self.assertEqual(command[command.index("--window-size") + 1], "1280x720")
        self.assertIn("--debug-effect-captures", command)
        debug_dir = Path(command[command.index("--debug-effect-captures") + 1])
        self.assertTrue(str(debug_dir).endswith("sunset/effect-captures"))
        self.assertIn("--debug-effect-probe-layers", command)
        self.assertEqual(command[command.index("--debug-effect-probe-layers") + 1], "405")
        self.assertIn("--debug-effect-probe-max-effects", command)
        self.assertEqual(command[command.index("--debug-effect-probe-max-effects") + 1], "2")
        self.assertIn("--debug-puppet-animation-layer-overrides", command)
        self.assertEqual(
            command[command.index("--debug-puppet-animation-layer-overrides") + 1],
            "405:781:paused=false",
        )
        self.assertTrue(result.effectManifest.endswith("sunset/effect-captures/manifest.json"))
        self.assertTrue(str(log_path).endswith("sunset/harness.log"))
        self.assertEqual(timeout_seconds, 38)

    def test_render_variant_does_not_forward_quarantined_channelmap_slots(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            harness.write_text("harness", encoding="utf-8")
            source = root / "scene.pkg"
            source.write_text("scene", encoding="utf-8")
            assets = root / "assets"
            assets.mkdir()
            out_dir = root / "out"
            commands: list[list[str]] = []

            def fake_run(command, log_path, timeout_seconds):
                commands.append(command)
                Path(command[command.index("--capture") + 1]).parent.mkdir(parents=True, exist_ok=True)
                Path(command[command.index("--capture") + 1]).write_text("png", encoding="utf-8")
                if "--debug-effect-captures" in command:
                    captures_dir = Path(command[command.index("--debug-effect-captures") + 1])
                    captures_dir.mkdir(parents=True, exist_ok=True)
                    (captures_dir / "manifest.json").write_text(
                        json.dumps({"status": "ok", "layers": [], "strippedCandidates": []}),
                        encoding="utf-8",
                    )
                return 0

            result = comparator.render_variant(
                comparator.VARIANTS[0],
                comparator.CompareConfig(
                    repo_root=root,
                    reference_root=root / "yakkai_arona",
                    output_root=out_dir,
                    debug_effect_captures=True,
                    debug_effect_probe_layers=(405,),
                    debug_effect_probe_channelmap_slots=(0, 2),
                ),
                harness=harness,
                source=source,
                assets=assets,
                run_command=fake_run,
            )

        self.assertEqual(result.status, "pass")
        self.assertNotIn("--debug-effect-probe-channelmap-slots", commands[0])

    def test_render_variant_fails_when_harness_output_is_missing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            source = root / "scene.pkg"
            assets = root / "assets"
            harness.write_text("harness", encoding="utf-8")
            source.write_text("scene", encoding="utf-8")
            assets.mkdir()

            result = comparator.render_variant(
                comparator.VARIANTS[0],
                comparator.CompareConfig(root, root / "yakkai_arona", root / "out"),
                harness=harness,
                source=source,
                assets=assets,
                run_command=lambda command, log_path, timeout_seconds: 0,
            )

        self.assertEqual(result.status, "fail")
        self.assertIn("missing yakkai capture", result.metrics["error"])

    def test_render_variant_returns_fail_when_harness_launch_raises(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            harness = root / "harness"
            source = root / "scene.pkg"
            assets = root / "assets"
            harness.write_text("harness", encoding="utf-8")
            source.write_text("scene", encoding="utf-8")
            assets.mkdir()

            result = comparator.render_variant(
                comparator.VARIANTS[0],
                comparator.CompareConfig(root, root / "yakkai_arona", root / "out"),
                harness=harness,
                source=source,
                assets=assets,
                run_command=mock.Mock(side_effect=PermissionError("denied")),
            )

        self.assertEqual(result.status, "fail")
        self.assertIn("failed to launch harness", result.metrics["error"])
        self.assertIn("denied", result.metrics["error"])
        self.assertIn("command", result.metrics)
        self.assertTrue(result.yakkai.endswith("day/yakkai.png"))
        self.assertTrue(result.log.endswith("day/harness.log"))

    def test_write_contact_sheet_excludes_failed_or_missing_comparison_artifacts(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            good = root / "good"
            good.mkdir()
            good_reference = good / "reference.normalized.png"
            good_yakkai = good / "yakkai.png"
            good_diff = good / "diff.png"
            for path in (good_reference, good_yakkai, good_diff):
                path.write_text("png", encoding="utf-8")
            commands = smoke_runner.ImageMagickCommands(["identify"], ["convert"], ["compare"])
            captured = []

            variants = [
                comparator.VariantResult(
                    name="day",
                    status="review",
                    reference="ref",
                    yakkai=str(good_yakkai),
                    normalizedReference=str(good_reference),
                    diff=str(good_diff),
                ),
                comparator.VariantResult(
                    name="sunset",
                    status="fail",
                    reference="ref",
                    yakkai=str(root / "missing-yakkai.png"),
                    normalizedReference=str(root / "missing-reference.png"),
                    diff=str(root / "missing-diff.png"),
                ),
            ]

            with (
                mock.patch.object(comparator, "montage_command", return_value=["montage"]),
                mock.patch.object(comparator, "run_checked", side_effect=lambda command: captured.append(command) or ""),
            ):
                comparator.write_contact_sheet(commands, variants, root / "contact.png")

        self.assertEqual(len(captured), 1)
        self.assertIn(str(good_reference), captured[0])
        self.assertIn(str(good_yakkai), captured[0])
        self.assertIn(str(good_diff), captured[0])
        self.assertNotIn(str(root / "missing-reference.png"), captured[0])

    def test_registration_structure_dimensions_downsample_by_four(self):
        self.assertEqual(comparator.registration_structure_dimensions(1280, 720), "320x180!")
        self.assertEqual(comparator.registration_structure_dimensions(100, 50), "25x12!")

    def test_build_registration_structure_image_uses_luminance_contrast_downsample(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source.png"
            output = root / "structure.png"
            source.write_text("png", encoding="utf-8")
            commands = smoke_runner.ImageMagickCommands(["identify"], ["convert"], ["compare"])
            captured = []

            with mock.patch.object(comparator, "run_checked", side_effect=lambda command: captured.append(command) or ""):
                comparator.build_registration_structure_image(commands, source, output, 1280, 720)

        self.assertEqual(captured[0][:2], ["convert", str(source)])
        self.assertIn("-alpha", captured[0])
        self.assertIn("-colorspace", captured[0])
        self.assertIn("Gray", captured[0])
        self.assertIn("-resize", captured[0])
        self.assertIn("320x180!", captured[0])
        self.assertIn("-auto-level", captured[0])
        self.assertEqual(captured[0][-1], str(output))

    def test_register_yakkai_to_reference_selects_lowest_structure_candidate(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            reference = root / "reference.normalized.png"
            yakkai = root / "yakkai.png"
            reference.write_text("reference", encoding="utf-8")
            yakkai.write_text("yakkai", encoding="utf-8")
            commands = smoke_runner.ImageMagickCommands(["identify"], ["convert"], ["compare"])
            candidates = (
                comparator.RegistrationCandidate(scale=1.0, offset_x=0, offset_y=0),
                comparator.RegistrationCandidate(scale=1.08, offset_x=16, offset_y=-8),
            )
            transformed: list[tuple[comparator.RegistrationCandidate, Path]] = []

            def fake_transform(commands, source, output, width, height, candidate):
                transformed.append((candidate, output))
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_text(f"{candidate.scale}:{candidate.offset_x}:{candidate.offset_y}", encoding="utf-8")

            def fake_structure(commands, source, output, width, height):
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_text(source.read_text(encoding="utf-8"), encoding="utf-8")

            def fake_compare(commands, expected, actual, diff):
                if "structure" in actual.name and actual.read_text(encoding="utf-8").startswith("1.08:"):
                    return 0.055
                if "structure" in actual.name:
                    return 0.090
                if actual.read_text(encoding="utf-8").startswith("1.08:"):
                    return 0.075
                return 0.125

            with (
                mock.patch.object(comparator, "transform_yakkai_for_registration", side_effect=fake_transform),
                mock.patch.object(comparator, "build_registration_structure_image", side_effect=fake_structure),
                mock.patch.object(comparator.smoke_runner, "compare_rmse", side_effect=fake_compare),
            ):
                result = comparator.register_yakkai_to_reference(
                    commands,
                    reference,
                    yakkai,
                    root,
                    1280,
                    720,
                    candidates,
                )

        self.assertEqual(result.candidate, candidates[1])
        self.assertEqual(result.rmse, 0.075)
        self.assertEqual(result.structure_rmse, 0.055)
        self.assertEqual(result.yakkai.name, "yakkai.registered.png")
        self.assertEqual(result.diff.name, "registered-diff.png")
        self.assertEqual(result.structure_reference.name, "reference.structure.png")
        self.assertEqual(result.structure_yakkai.name, "yakkai.registered.structure.png")
        self.assertEqual(result.structure_diff.name, "registered-structure-diff.png")
        self.assertEqual([item[0] for item in transformed][-1], candidates[1])

    def test_compare_variant_images_records_registered_artifacts_and_metrics(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            reference_root = root / "refs"
            reference = reference_root / "day" / "still.png"
            yakkai = root / "out" / "day" / "yakkai.png"
            reference.parent.mkdir(parents=True)
            yakkai.parent.mkdir(parents=True)
            reference.write_text("reference", encoding="utf-8")
            yakkai.write_text("yakkai", encoding="utf-8")
            commands = smoke_runner.ImageMagickCommands(["identify"], ["convert"], ["compare"])
            rendered = comparator.VariantResult(
                name="day",
                status="pass",
                reference=str(reference),
                yakkai=str(yakkai),
            )

            def fake_normalize(commands, source, normalized, dimensions):
                normalized.parent.mkdir(parents=True, exist_ok=True)
                normalized.write_text("normalized", encoding="utf-8")

            registration = comparator.RegistrationResult(
                candidate=comparator.RegistrationCandidate(scale=1.08, offset_x=16, offset_y=-8),
                rmse=0.075,
                structure_rmse=0.055,
                yakkai=yakkai.parent / "yakkai.registered.png",
                diff=yakkai.parent / "registered-diff.png",
                structure_reference=yakkai.parent / "reference.structure.png",
                structure_yakkai=yakkai.parent / "yakkai.registered.structure.png",
                structure_diff=yakkai.parent / "registered-structure-diff.png",
            )
            for path in (
                registration.yakkai,
                registration.diff,
                registration.structure_reference,
                registration.structure_yakkai,
                registration.structure_diff,
            ):
                path.write_text("artifact", encoding="utf-8")

            with (
                mock.patch.object(comparator, "normalize_reference", side_effect=fake_normalize),
                mock.patch.object(comparator, "rgb_means", return_value=[0.1, 0.2, 0.3]),
                mock.patch.object(comparator.smoke_runner, "compare_rmse", return_value=0.125),
                mock.patch.object(comparator, "register_yakkai_to_reference", return_value=registration),
            ):
                result = comparator.compare_variant_images(
                    comparator.VARIANTS[0],
                    rendered,
                    comparator.CompareConfig(root, reference_root, root / "out"),
                    commands,
                )

        self.assertEqual(result.status, "review")
        self.assertEqual(result.metrics["rmse"], 0.125)
        self.assertEqual(result.metrics["registeredRmse"], 0.075)
        self.assertEqual(result.metrics["registrationStructureRmse"], 0.055)
        self.assertEqual(result.metrics["registrationMetric"], "grayscale-contrast-320x180")
        self.assertEqual(result.metrics["registrationScale"], 1.08)
        self.assertEqual(result.metrics["registrationOffsetX"], 16)
        self.assertEqual(result.metrics["registrationOffsetY"], -8)
        self.assertTrue(result.registeredYakkai.endswith("day/yakkai.registered.png"))
        self.assertTrue(result.registeredDiff.endswith("day/registered-diff.png"))
        self.assertTrue(result.structureReference.endswith("day/reference.structure.png"))
        self.assertTrue(result.registeredStructureYakkai.endswith("day/yakkai.registered.structure.png"))
        self.assertTrue(result.registeredStructureDiff.endswith("day/registered-structure-diff.png"))

    def test_write_registered_contact_sheet_excludes_results_without_registered_artifacts(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            good = root / "good"
            good.mkdir()
            good_reference = good / "reference.normalized.png"
            good_yakkai = good / "yakkai.registered.png"
            good_diff = good / "registered-diff.png"
            for path in (good_reference, good_yakkai, good_diff):
                path.write_text("png", encoding="utf-8")
            commands = smoke_runner.ImageMagickCommands(["identify"], ["convert"], ["compare"])
            captured = []

            variants = [
                comparator.VariantResult(
                    name="day",
                    status="review",
                    reference="ref",
                    normalizedReference=str(good_reference),
                    registeredYakkai=str(good_yakkai),
                    registeredDiff=str(good_diff),
                ),
                comparator.VariantResult(
                    name="sunset",
                    status="review",
                    reference="ref",
                    normalizedReference=str(root / "missing-reference.png"),
                    registeredYakkai=str(root / "missing-yakkai.png"),
                    registeredDiff=str(root / "missing-diff.png"),
                ),
            ]

            with (
                mock.patch.object(comparator, "montage_command", return_value=["montage"]),
                mock.patch.object(comparator, "run_checked", side_effect=lambda command: captured.append(command) or ""),
            ):
                comparator.write_registered_contact_sheet(commands, variants, root / "contact-registered.png")

        self.assertEqual(len(captured), 1)
        self.assertIn(str(good_reference), captured[0])
        self.assertIn(str(good_yakkai), captured[0])
        self.assertIn(str(good_diff), captured[0])
        self.assertNotIn(str(root / "missing-reference.png"), captured[0])


if __name__ == "__main__":
    unittest.main()
