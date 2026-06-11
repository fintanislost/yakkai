import csv
import io
import json
import tempfile
import unittest
import zipfile
from pathlib import Path
import sys

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

import arona_layer405_fresh_publish_compare as compare


def png_bytes(size, color=(32, 64, 96, 255)):
    buffer = io.BytesIO()
    Image.new("RGBA", size, color).save(buffer, format="PNG")
    return buffer.getvalue()


def csv_bytes(rows):
    buffer = io.StringIO()
    writer = csv.DictWriter(buffer, fieldnames=list(rows[0].keys()))
    writer.writeheader()
    writer.writerows(rows)
    return buffer.getvalue().encode("utf-8")


def write_variant(
    zip_file,
    root,
    variant,
    timeofday,
    event_id,
    draw_id,
    srv_resource,
    write_mask="7",
    image_sizes=None,
):
    variant_root = f"{root}/{variant}"
    image_sizes = image_sizes or {
        "effect-input-before-visible-effects.png": (4160, 2923),
        "prefix-3-after-first-lut-pair.png": (4160, 2923),
        "prefix-7-after-visible-effect-7.png": (4160, 2923),
        "final-publish-input.png": (4160, 2923),
        "default-before-final-publish.png": (2560, 1440),
        "default-after-final-publish.png": (2560, 1440),
    }
    for name, size in image_sizes.items():
        zip_file.writestr(f"{variant_root}/{name}", png_bytes(size))
    metadata = {
        "variant": variant,
        "timeofday": str(timeofday),
        "finalPublish": {
            "drawId": draw_id,
            "replayEventId": event_id,
            "blendState": (
                "enabled=1 color=(SrcAlpha Add InvSrcAlpha) "
                f"alpha=(SrcAlpha Add InvSrcAlpha) writeMask={write_mask}"
            ),
            "renderTargetResourceId": 65,
            "renderTargetFormat": "R8G8B8A8_UNORM",
            "renderTargetDimensions": [2560, 1440],
            "srvs": [
                {
                    "slot": 0,
                    "resourceId": srv_resource,
                    "format": "R8G8B8A8_UNORM",
                    "dimensions": list(image_sizes["final-publish-input.png"]),
                }
            ],
        },
    }
    zip_file.writestr(f"{variant_root}/metadata.json", json.dumps(metadata))
    state = [
        {
            "variant": variant,
            "source_label": f"{variant}.rdc",
            "replay_event": str(event_id),
            "draw_id": str(draw_id),
            "blend0": "enabled=1 color=(SrcAlpha Add InvSrcAlpha) alpha=(SrcAlpha Add InvSrcAlpha) writeMask=7",
            "blend_factor": "1 1 1 1",
            "sample_mask": "4294967295",
            "rt_resource": "65",
            "rt_name": "Swapchain Image 65",
            "rt_width": "2560",
            "rt_height": "1440",
            "rt_format": "R8G8B8A8_UNORM",
            "srv_slot": "0",
            "srv_resource": str(srv_resource),
            "srv_name": f"2D Render Target {srv_resource}",
            "srv_width": "4160",
            "srv_height": "2923",
            "srv_format": "R8G8B8A8_UNORM",
            "lower_ribbon_x": "1536",
            "lower_ribbon_y": "1024",
            "transparent_edge_x": "1280",
            "transparent_edge_y": "0",
        }
    ]
    zip_file.writestr(f"{variant_root}/final_publish_state.csv", csv_bytes(state))
    history = [
        {
            "event": "12",
            "frag": "0",
            "primitive": "1",
            "passed": "1",
            "shader_out_rgba": "0 0 0 0",
            "post_mod_rgba": "0.1 0.2 0.3 1",
            "pre_mod_rgba": "0.0 0.0 0.0 1",
            "flags": "sampleMasked=0",
        },
        {
            "event": str(event_id),
            "frag": "0",
            "primitive": "1",
            "passed": "1",
            "shader_out_rgba": "0.5 0.6 0.7 0.8",
            "post_mod_rgba": "0.6 0.7 0.8 1",
            "pre_mod_rgba": "0.2 0.3 0.4 1",
            "flags": "sampleMasked=0",
        },
    ]
    zip_file.writestr(f"{variant_root}/pixel-history-lower-ribbon.csv", csv_bytes(history))
    zip_file.writestr(f"{variant_root}/pixel-history-transparent-edge.csv", csv_bytes(history))


def make_package(path, omit_variant=None, write_mask="7", capture_overrides=None, image_sizes=None):
    root = "layer405_final_publish_composite_fresh"
    capture_overrides = capture_overrides or {}
    captures = [
        ("day", "1", 621, 1140, 374),
        ("sunset", "2", 824, 1634, 1736),
        ("night", "3", 1199, 2040, 1769),
    ]
    with zipfile.ZipFile(path, "w") as zip_file:
        manifest_captures = []
        for variant, timeofday, event_id, draw_id, srv_resource in captures:
            if variant == omit_variant:
                continue
            capture = {
                "variant": variant,
                "sourceType": "fresh-live-rdc",
                "variantLabelSource": "capture-session-label",
                "variantMappingStatus": "labeled-by-capture-session",
                "timeofday": timeofday,
                "finalPublishReplayEventId": event_id,
                "finalPublishDrawId": draw_id,
                "finalPublishInputResourceId": srv_resource,
            }
            capture.update(capture_overrides.get(variant, {}))
            manifest_captures.append(capture)
        manifest = {
            "status": "complete_fresh_live_rdc_labeled_variants",
            "captures": manifest_captures,
        }
        zip_file.writestr(f"{root}/source_manifest.json", json.dumps(manifest))
        for variant, timeofday, event_id, draw_id, srv_resource in captures:
            if variant == omit_variant:
                continue
            variant_write_mask = write_mask if variant == "day" else "7"
            write_variant(
                zip_file,
                root,
                variant,
                timeofday,
                event_id,
                draw_id,
                srv_resource,
                variant_write_mask,
                image_sizes,
            )


def write_yakkai_effect_manifest(path, before_path, after_path):
    path.parent.mkdir(parents=True, exist_ok=True)
    manifest = {
        "captures": [
            {
                "stage": "default-before-effect",
                "path": str(before_path),
                "layer": {"layerId": 405, "name": "ARONA_CROP_SHEET"},
                "completed": True,
            },
            {
                "stage": "default-after-effect",
                "path": str(after_path),
                "layer": {"layerId": 405, "name": "ARONA_CROP_SHEET"},
                "completed": True,
            },
        ],
        "debugEffectPassStates": [
            {
                "output": "_rt_default",
                "blendMode": "1",
                "blendEnabled": True,
                "colorMask": "RGB",
                "colorMaskBits": 7,
            }
        ],
    }
    path.write_text(json.dumps(manifest), encoding="utf-8")


def add_yakkai_capture(manifest_path, stage, path):
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["captures"].append(
        {
            "stage": stage,
            "path": str(path),
            "layer": {"layerId": 405, "name": "ARONA_CROP_SHEET"},
            "completed": True,
        }
    )
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")


def add_yakkai_material_capture(
    manifest_path,
    stage,
    path,
    shader,
    effect_index,
    final_published=False,
    material_extra=None,
):
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    material = {
        "effectIndex": effect_index,
        "materialIndex": 0,
        "shader": shader,
        "materialOutputCaptureStage": stage,
        "finalPublishedMaterial": final_published,
    }
    if material_extra:
        material.update(material_extra)
    manifest["captures"].append(
        {
            "stage": stage,
            "path": str(path),
            "layer": {
                "layerId": 405,
                "name": "ARONA_CROP_SHEET",
                "effectMaterials": [material],
            },
            "completed": True,
        }
    )
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")


def write_yakkai_summary(root, before_path, after_path, variants=("day", "sunset", "night")):
    summary_variants = []
    for variant in variants:
        manifest_path = root / variant / "effect-captures" / "manifest.json"
        write_yakkai_effect_manifest(manifest_path, before_path, after_path)
        summary_variants.append(
            {
                "name": variant,
                "status": "review",
                "effectManifest": str(manifest_path),
                "yakkai": str(root / variant / "yakkai.png"),
            }
        )
    summary = {"variants": summary_variants}
    (root / "summary.json").write_text(json.dumps(summary), encoding="utf-8")
    return root / "summary.json"


class FreshPublishArchiveTests(unittest.TestCase):
    def test_intake_accepts_complete_fresh_labeled_archive(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / "fresh.zip"
            make_package(archive)

            report = compare.load_windows_package(archive)

            self.assertEqual(report["status"], "complete")
            self.assertEqual([variant["variant"] for variant in report["variants"]], ["day", "sunset", "night"])
            self.assertEqual(report["variants"][0]["timeofday"], "1")
            self.assertEqual(report["variants"][0]["finalPublish"]["writeMask"], 7)
            self.assertEqual(report["variants"][0]["finalPublish"]["writeMaskChannels"], "RGB")
            self.assertEqual(report["variants"][0]["finalPublish"]["srv0Dimensions"], [4160, 2923])
            self.assertEqual(report["variants"][0]["finalPublish"]["renderTargetDimensions"], [2560, 1440])
            self.assertEqual(report["variants"][0]["pixelHistory"]["lowerRibbon"]["finalEvent"], 621)
            self.assertEqual(
                report["variants"][0]["pixelHistory"]["lowerRibbon"]["postModRgba"],
                [0.6, 0.7, 0.8, 1.0],
            )

    def test_intake_rejects_missing_required_variant(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / "fresh.zip"
            make_package(archive, omit_variant="night")

            with self.assertRaisesRegex(compare.FreshPublishError, "missing required variants: night"):
                compare.load_windows_package(archive)

    def test_intake_rejects_non_rgb_write_mask(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / "fresh.zip"
            make_package(archive, write_mask="15")

            with self.assertRaisesRegex(compare.FreshPublishError, "day final publish writeMask expected 7"):
                compare.load_windows_package(archive)

    def test_intake_rejects_manifest_final_publish_mismatches(self):
        cases = [
            ("finalPublishReplayEventId", 622),
            ("finalPublishDrawId", 1141),
            ("finalPublishInputResourceId", 375),
        ]
        for field, value in cases:
            with self.subTest(field=field), tempfile.TemporaryDirectory() as temp:
                archive = Path(temp) / "fresh.zip"
                make_package(archive, capture_overrides={"day": {field: value}})

                with self.assertRaisesRegex(compare.FreshPublishError, f"day {field}"):
                    compare.load_windows_package(archive)

    def test_sample_rgba_scales_default_target_coordinates(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "image.png"
            image = Image.new("RGBA", (4, 4), (0, 0, 0, 255))
            image.putpixel((2, 2), (128, 64, 32, 255))
            image.save(path)

            sample = compare.sample_rgba_at_default_coordinate(
                path,
                [4, 4],
                [8, 8],
            )

            self.assertEqual(sample["imageDimensions"], [4, 4])
            self.assertEqual(sample["samplePixel"], [2, 2])
            self.assertEqual(sample["rgba"], [128 / 255.0, 64 / 255.0, 32 / 255.0, 1.0])

    def test_rgb_delta_metrics_classify_close_match(self):
        metrics = compare.rgb_delta_metrics(
            [0.2, 0.2, 0.2, 1.0],
            [0.5, 0.4, 0.3, 1.0],
            [0.21, 0.19, 0.2, 1.0],
            [0.51, 0.39, 0.3, 1.0],
        )

        self.assertEqual(metrics["classification"], "default-delta-close")
        self.assertLess(metrics["deltaRmse"], 0.02)
        self.assertGreater(metrics["deltaCosine"], 0.99)

    def test_sample_rgba_rejects_invalid_default_coordinate_dimensions(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "image.png"
            Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(path)

            with self.assertRaisesRegex(compare.FreshPublishError, "coordinate dimensions"):
                compare.sample_rgba_at_default_coordinate(path, [0, 0], [0, 8])

            with self.assertRaisesRegex(compare.FreshPublishError, "coordinate must contain"):
                compare.sample_rgba_at_default_coordinate(path, [0], [8, 8])

    def test_rgb_delta_metrics_clamps_cosine(self):
        metrics = compare.rgb_delta_metrics(
            [0.0, 0.0, 0.0, 1.0],
            [1.0, 1.0, 1.0, 1.0],
            [0.0, 0.0, 0.0, 1.0],
            [1.0, 1.0, 1.0, 1.0],
        )

        self.assertEqual(metrics["deltaCosine"], 1.0)

    def test_registration_recovers_layer_local_crop_to_default_target(self):
        local = Image.new("RGBA", (64, 48), (0, 0, 0, 0))
        for y in range(12, 36):
            for x in range(16, 48):
                local.putpixel((x, y), (x * 3 % 255, y * 5 % 255, 160, 255))
        target = local.crop((16, 12, 48, 36)).resize((32, 24), Image.Resampling.BILINEAR)

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            local_path = root / "local.png"
            target_path = root / "target.png"
            local.save(local_path)
            target.save(target_path)

            result = compare.register_layer_local_to_default(local_path, target_path, max_size=64)

            self.assertEqual(result["classification"], "registered-layer-local-to-default")
            self.assertLess(result["rmse"], 0.01)
            self.assertEqual(result["cropPixels"], [16, 12, 32, 24])
            self.assertEqual(result["targetDimensions"], [32, 24])

    def test_registration_refines_bounded_downscaled_crop(self):
        true_crop = [48, 45, 160, 90]
        local = Image.new("RGBA", (256, 180), (0, 0, 0, 0))
        for y in range(true_crop[1], true_crop[1] + true_crop[3]):
            for x in range(true_crop[0], true_crop[0] + true_crop[2]):
                local.putpixel(
                    (x, y),
                    (
                        (x + y) % 255,
                        (x * 2 + y) % 255,
                        (x + y * 2) % 255,
                        255,
                    ),
                )
        target = local.crop(
            (
                true_crop[0],
                true_crop[1],
                true_crop[0] + true_crop[2],
                true_crop[1] + true_crop[3],
            )
        ).resize((320, 180), Image.Resampling.BILINEAR)

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            local_path = root / "local.png"
            target_path = root / "target.png"
            local.save(local_path)
            target.save(target_path)

            result = compare.register_layer_local_to_default(local_path, target_path, max_size=128)

            self.assertEqual(result["classification"], "registered-layer-local-to-default")
            self.assertLess(result["rmse"], 0.02)
            for actual, expected in zip(result["cropPixels"], true_crop):
                self.assertLessEqual(abs(actual - expected), 2)

    def test_registration_reports_weak_match_for_unrelated_images(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            local_path = root / "local.png"
            target_path = root / "target.png"
            Image.new("RGBA", (64, 48), (255, 0, 0, 255)).save(local_path)
            Image.new("RGBA", (32, 24), (0, 255, 0, 255)).save(target_path)

            result = compare.register_layer_local_to_default(local_path, target_path, max_size=64)

            self.assertEqual(result["classification"], "weak-or-unregistered-match")
            self.assertGreater(result["rmse"], 0.5)

    def test_correlates_yakkai_rgb_default_pass_state(self):
        manifest = {
            "debugEffectPassStates": [
                {
                    "output": "_rt_effect_ppong_a",
                    "loadOp": "CLEAR",
                    "depthLoadOp": "DONT_CARE",
                    "colorMask": "RGBA",
                    "colorMaskBits": 15,
                    "blendMode": "1",
                    "blendEnabled": True,
                    "preserveOutput": False,
                    "usesDepth": False,
                    "camera": "local",
                    "nodeId": 100,
                    "materialName": "pulse",
                    "debugPurpose": "effect-pass",
                },
                {
                    "output": "_rt_default",
                    "loadOp": "LOAD",
                    "depthLoadOp": "DONT_CARE",
                    "colorMask": "RGB",
                    "colorMaskBits": 7,
                    "blendMode": "1",
                    "blendEnabled": True,
                    "preserveOutput": True,
                    "usesDepth": False,
                    "camera": "global",
                    "nodeId": 405,
                    "materialName": "ARONA_CROP_SHEET",
                    "debugPurpose": "effect-pass",
                },
            ]
        }

        result = compare.correlate_yakkai_final_publish_state(manifest)

        self.assertEqual(result["classification"], "yakkai-final-publish-rgb-mask")
        self.assertEqual(result["selectedPass"]["output"], "_rt_default")
        self.assertEqual(result["selectedPass"]["colorMaskBits"], 7)
        self.assertEqual(result["selectedPass"]["blendMode"], "1")

    def test_correlates_missing_yakkai_default_pass_state(self):
        manifest = {
            "debugEffectPassStates": [
                {
                    "output": "_rt_effect_ppong_a",
                    "colorMaskBits": 15,
                    "blendMode": "1",
                    "blendEnabled": True,
                },
                {
                    "output": "_rt_default",
                    "colorMaskBits": 7,
                    "blendMode": "0",
                    "blendEnabled": True,
                },
            ]
        }

        result = compare.correlate_yakkai_final_publish_state(manifest)

        self.assertEqual(result["classification"], "missing-yakkai-final-publish-pass-state")
        self.assertIsNone(result["selectedPass"])
        self.assertEqual(result["candidateCount"], 0)

    def test_correlates_yakkai_legacy_pass_states_fallback(self):
        manifest = {
            "passStates": [
                {
                    "output": "_rt_default",
                    "colorMaskBits": 15,
                    "blendMode": 1,
                    "blendEnabled": True,
                }
            ]
        }

        result = compare.correlate_yakkai_final_publish_state(manifest)

        self.assertEqual(result["classification"], "yakkai-final-publish-rgba-mask")
        self.assertEqual(result["selectedPass"]["colorMaskBits"], 15)

    def test_correlates_yakkai_invalid_mask_as_unexpected(self):
        for value in (None, "RGB", {}, []):
            with self.subTest(value=value):
                manifest = {
                    "debugEffectPassStates": [
                        {
                            "output": "_rt_default",
                            "colorMaskBits": value,
                            "blendMode": "1",
                            "blendEnabled": True,
                        }
                    ]
                }

                result = compare.correlate_yakkai_final_publish_state(manifest)

                self.assertEqual(result["classification"], "yakkai-final-publish-unexpected-mask")
                self.assertEqual(result["selectedPass"]["colorMaskBits"], value)

    def test_finds_yakkai_manifest_under_run_root(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest_dir = root / "run" / "variant" / "effects"
            manifest_dir.mkdir(parents=True)
            manifest_path = manifest_dir / "manifest.json"
            manifest_path.write_text(
                json.dumps(
                    {
                        "sceneId": "3228578419",
                        "captures": [
                            {
                                "stage": "final-publish",
                                "layer": {"layerId": 405},
                            }
                        ],
                        "debugEffectPassStates": [
                            {
                                "output": "_rt_default",
                                "colorMaskBits": 7,
                                "blendMode": "1",
                                "blendEnabled": True,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            self.assertEqual(compare.find_yakkai_manifest(root), manifest_path)

    def test_load_yakkai_variant_manifests_from_summary_root(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            before = root / "before.png"
            after = root / "after.png"
            Image.new("RGBA", (4, 4), (10, 20, 30, 255)).save(before)
            Image.new("RGBA", (4, 4), (20, 30, 40, 255)).save(after)
            write_yakkai_summary(root, before, after)

            variants = compare.load_yakkai_variant_manifests(root)

            self.assertIn("day", variants)
            self.assertEqual(
                variants["day"]["manifestPath"],
                str(root / "day" / "effect-captures" / "manifest.json"),
            )
            self.assertEqual(variants["day"]["captures"]["default-before-effect"], str(before))
            self.assertEqual(variants["day"]["captures"]["default-after-effect"], str(after))

    def test_load_yakkai_variant_manifests_includes_final_display_boundary_when_present(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            before = root / "before.png"
            after = root / "after.png"
            final_before = root / "final_before.png"
            final_after = root / "final_after.png"
            for path in (before, after, final_before, final_after):
                Image.new("RGBA", (4, 4), (10, 20, 30, 255)).save(path)
            write_yakkai_summary(root, before, after)
            manifest_path = root / "day" / "effect-captures" / "manifest.json"
            add_yakkai_capture(manifest_path, "final-display-before", final_before)
            add_yakkai_capture(manifest_path, "final-display-after", final_after)

            variants = compare.load_yakkai_variant_manifests(root)

            self.assertEqual(variants["day"]["captures"]["final-display-before"], str(final_before))
            self.assertEqual(variants["day"]["captures"]["final-display-after"], str(final_after))

    def test_load_yakkai_variant_manifests_rejects_missing_default_after_capture(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            before = root / "before.png"
            after = root / "after.png"
            Image.new("RGBA", (4, 4), (10, 20, 30, 255)).save(before)
            write_yakkai_summary(root, before, after)
            manifest_path = root / "day" / "effect-captures" / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["captures"] = [
                capture for capture in manifest["captures"] if capture["stage"] != "default-after-effect"
            ]
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaisesRegex(
                compare.FreshPublishError,
                "day missing layer 405 capture stage default-after-effect",
            ):
                compare.load_yakkai_variant_manifests(root)

    def test_rgb_delta_map_reports_peak_and_sample_magnitude(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            before = root / "before.png"
            after = root / "after.png"
            before_image = Image.new("RGBA", (8, 8), (10, 20, 30, 255))
            after_image = Image.new("RGBA", (8, 8), (10, 20, 30, 255))
            after_image.putpixel((6, 5), (110, 20, 30, 255))
            before_image.save(before)
            after_image.save(after)

            delta = compare.rgb_delta_map(before, after)

            self.assertEqual(delta["dimensions"], [8, 8])
            self.assertEqual(delta["peakPixel"], [6, 5])
            self.assertGreater(delta["peakMagnitude"], 0.22)
            self.assertEqual(delta["sampleMagnitude"]([6, 5]), delta["peakMagnitude"])
            self.assertEqual(delta["sampleMagnitude"]([0, 0]), 0.0)

    def test_find_nearest_delta_pixel_reports_distance_and_peak(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            before = root / "before.png"
            after = root / "after.png"
            before_image = Image.new("RGBA", (16, 16), (0, 0, 0, 255))
            after_image = Image.new("RGBA", (16, 16), (0, 0, 0, 255))
            after_image.putpixel((10, 8), (80, 0, 0, 255))
            after_image.putpixel((15, 15), (255, 0, 0, 255))
            before_image.save(before)
            after_image.save(after)
            delta = compare.rgb_delta_map(before, after)

            nearest = compare.find_nearest_delta_pixel(delta, [8, 8], min_magnitude=0.05)

            self.assertEqual(nearest["nearestPixel"], [10, 8])
            self.assertEqual(nearest["peakPixel"], [15, 15])
            self.assertEqual(nearest["nearestDistancePixels"], 2.0)
            self.assertGreater(nearest["peakMagnitude"], nearest["nearestMagnitude"])

    def test_find_nearest_delta_pixel_counts_exact_threshold_delta(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            before = root / "before.png"
            after = root / "after.png"
            Image.new("RGBA", (2, 2), (0, 0, 0, 255)).save(before)
            image = Image.new("RGBA", (2, 2), (0, 0, 0, 255))
            image.putpixel((1, 1), (1, 0, 0, 255))
            image.save(after)

            delta = compare.rgb_delta_map(before, after)
            nearest = compare.find_nearest_delta_pixel(delta, [0, 0])

            self.assertEqual(delta["nonzeroPixelCount"], 1)
            self.assertEqual(nearest["nearestPixel"], [1, 1])

    def test_find_nearest_delta_pixel_rejects_invalid_sample_pixels(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            before = root / "before.png"
            after = root / "after.png"
            Image.new("RGBA", (2, 2), (0, 0, 0, 255)).save(before)
            Image.new("RGBA", (2, 2), (1, 0, 0, 255)).save(after)
            delta = compare.rgb_delta_map(before, after)

            for sample_pixel in ([0], [0.5, 1], ["0", 1]):
                with self.subTest(sample_pixel=sample_pixel), self.assertRaisesRegex(
                    compare.FreshPublishError,
                    "delta sample pixel",
                ):
                    compare.find_nearest_delta_pixel(delta, sample_pixel)

    def test_default_delta_locator_classifies_misplaced_nearby_delta(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            before = root / "before.png"
            after = root / "after.png"
            before_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            after_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            after_image.putpixel((788, 512), (100, 0, 0, 255))
            before_image.save(before)
            after_image.save(after)
            sample = {
                "sampleName": "lowerRibbon",
                "beforeSample": {"samplePixel": [768, 512]},
                "coordinate": [1536, 1024],
                "coordinateDimensions": [2560, 1440],
            }

            locator = compare.build_default_delta_locator_sample(
                sample,
                before,
                after,
                near_radius=64.0,
            )

            self.assertEqual(locator["classification"], "delta-nearby")
            self.assertEqual(locator["nearestPixel"], [788, 512])
            self.assertEqual(locator["nearestDistancePixels"], 20.0)
            self.assertEqual(locator["sampleMagnitude"], 0.0)

    def test_default_delta_locator_classifies_missing_delta(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            before = root / "before.png"
            after = root / "after.png"
            Image.new("RGBA", (1280, 720), (0, 0, 0, 255)).save(before)
            Image.new("RGBA", (1280, 720), (0, 0, 0, 255)).save(after)
            sample = {
                "sampleName": "transparentEdge",
                "beforeSample": {"samplePixel": [640, 0]},
                "coordinate": [1280, 0],
                "coordinateDimensions": [2560, 1440],
            }

            locator = compare.build_default_delta_locator_sample(sample, before, after)

            self.assertEqual(locator["classification"], "missing-default-delta")
            self.assertIsNone(locator["nearestPixel"])
            self.assertEqual(locator["nonzeroPixelCount"], 0)

    def test_yakkai_default_delta_locator_reports_variant_summary(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            make_package(archive)
            report = compare.load_windows_package(archive)
            before = root / "before.png"
            after = root / "after.png"
            before_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            after_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            after_image.putpixel((788, 512), (100, 0, 0, 255))
            before_image.save(before)
            after_image.save(after)
            write_yakkai_summary(root, before, after)
            variants = compare.load_yakkai_variant_manifests(root)
            report["yakkaiDefaultDeltaOracle"] = compare.build_yakkai_default_delta_oracle(report, variants)

            locator = compare.build_yakkai_default_delta_locator(report, variants)

            self.assertEqual(locator["day"]["classification"], "delta-nearby")
            self.assertEqual(locator["day"]["samples"]["lowerRibbon"]["nearestPixel"], [788, 512])
            self.assertEqual(locator["day"]["samples"]["transparentEdge"]["classification"], "delta-elsewhere")
            self.assertEqual(
                locator["day"]["layerFinalPublishBoundary"]["fromStage"],
                "default-before-effect",
            )
            self.assertEqual(
                locator["day"]["layerFinalPublishBoundary"]["toStage"],
                "default-after-effect",
            )

    def test_yakkai_delta_locator_uses_isolated_final_display_boundary_when_present(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            make_package(archive)
            report = compare.load_windows_package(archive)
            default_before = root / "default_before.png"
            default_after = root / "default_after.png"
            final_before = root / "final_before.png"
            final_after = root / "final_after.png"
            for path in (default_before, default_after, final_before):
                Image.new("RGBA", (1280, 720), (0, 0, 0, 255)).save(path)
            final_after_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            final_after_image.putpixel((768, 512), (80, 0, 0, 255))
            final_after_image.save(final_after)
            write_yakkai_summary(root, default_before, default_after)
            for variant in ("day", "sunset", "night"):
                manifest_path = root / variant / "effect-captures" / "manifest.json"
                add_yakkai_capture(manifest_path, "final-display-before", final_before)
                add_yakkai_capture(manifest_path, "final-display-after", final_after)
            variants = compare.load_yakkai_variant_manifests(root)
            report["yakkaiDefaultDeltaOracle"] = compare.build_yakkai_default_delta_oracle(report, variants)

            locator = compare.build_yakkai_default_delta_locator(report, variants)

            self.assertEqual(locator["day"]["classification"], "missing-default-delta")
            layer_boundary = locator["day"]["layerFinalPublishBoundary"]
            self.assertEqual(layer_boundary["fromStage"], "final-display-before")
            self.assertEqual(layer_boundary["toStage"], "final-display-after")
            self.assertEqual(layer_boundary["classification"], "delta-at-windows-sample")
            self.assertGreater(layer_boundary["samples"]["lowerRibbon"]["sampleMagnitude"], 0.1)

    def test_yakkai_delta_locator_reports_missing_selected_final_display_capture_file(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            make_package(archive)
            report = compare.load_windows_package(archive)
            default_before = root / "default_before.png"
            default_after = root / "default_after.png"
            Image.new("RGBA", (1280, 720), (51, 76, 102, 255)).save(default_before)
            Image.new("RGBA", (1280, 720), (153, 178, 204, 255)).save(default_after)
            write_yakkai_summary(root, default_before, default_after)
            for variant in ("day", "sunset", "night"):
                manifest_path = root / variant / "effect-captures" / "manifest.json"
                add_yakkai_capture(manifest_path, "final-display-before", root / f"{variant}_missing_before.png")
                add_yakkai_capture(manifest_path, "final-display-after", root / f"{variant}_missing_after.png")
            variants = compare.load_yakkai_variant_manifests(root)
            report["yakkaiDefaultDeltaOracle"] = compare.build_yakkai_default_delta_oracle(report, variants)

            with self.assertRaisesRegex(
                compare.FreshPublishError,
                r"day missing final-display-before capture file: .*day_missing_before\.png",
            ):
                compare.build_yakkai_default_delta_locator(report, variants)

    def test_isolated_publish_parity_reports_variants_and_uses_final_display_boundary(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            make_package(archive)
            report = compare.load_windows_package(archive)
            default_before = root / "default_before.png"
            default_after = root / "default_after.png"
            final_before = root / "final_before.png"
            final_after = root / "final_after.png"
            for path in (default_before, final_before):
                Image.new("RGBA", (1280, 720), (51, 76, 102, 255)).save(path)
            default_after_image = Image.new("RGBA", (1280, 720), (51, 76, 102, 255))
            final_after_image = Image.new("RGBA", (1280, 720), (51, 76, 102, 255))
            for image in (default_after_image, final_after_image):
                image.putpixel((768, 512), (153, 178, 204, 255))
                image.putpixel((640, 0), (153, 178, 204, 255))
            default_after_image.save(default_after)
            final_after_image.save(final_after)
            write_yakkai_summary(root, default_before, default_after)
            for variant in ("day", "sunset", "night"):
                manifest_path = root / variant / "effect-captures" / "manifest.json"
                add_yakkai_capture(manifest_path, "final-display-before", final_before)
                add_yakkai_capture(manifest_path, "final-display-after", final_after)
            variants = compare.load_yakkai_variant_manifests(root)
            report["yakkaiDefaultDeltaOracle"] = compare.build_yakkai_default_delta_oracle(report, variants)

            parity = compare.build_yakkai_isolated_publish_parity(report, variants)

            self.assertEqual(parity["day"]["fromStage"], "final-display-before")
            self.assertEqual(parity["day"]["toStage"], "final-display-after")
            self.assertEqual(parity["day"]["classification"], "isolated-publish-close")
            self.assertIn("lowerRibbon", parity["day"]["samples"])
            self.assertIn("transparentEdge", parity["day"]["samples"])

    def test_combine_isolated_publish_classifications_reports_edge_cases(self):
        cases = [
            (
                {
                    "a": {"classification": "isolated-publish-close"},
                    "b": {"classification": "isolated-publish-close"},
                },
                "isolated-publish-close",
            ),
            (
                {
                    "a": {"classification": "isolated-publish-close"},
                    "b": {"classification": "isolated-publish-directional-match"},
                },
                "isolated-publish-directional-match",
            ),
            (
                {
                    "a": {"classification": "isolated-publish-close"},
                    "b": {"classification": "isolated-publish-mismatch"},
                },
                "isolated-publish-mixed",
            ),
            (
                {
                    "a": {"classification": "isolated-publish-mismatch"},
                    "b": {"classification": "isolated-publish-mismatch"},
                },
                "isolated-publish-mismatch",
            ),
        ]
        for samples, expected in cases:
            with self.subTest(expected=expected):
                self.assertEqual(compare.combine_isolated_publish_classifications(samples), expected)

        with self.assertRaisesRegex(compare.FreshPublishError, "no isolated publish samples"):
            compare.combine_isolated_publish_classifications({})

    def test_isolated_publish_parity_reports_context_for_missing_variants(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            make_package(archive)
            report = compare.load_windows_package(archive)
            before = root / "before.png"
            after = root / "after.png"
            Image.new("RGBA", (1280, 720), (51, 76, 102, 255)).save(before)
            Image.new("RGBA", (1280, 720), (153, 178, 204, 255)).save(after)
            write_yakkai_summary(root, before, after)
            for variant in ("day", "sunset", "night"):
                manifest_path = root / variant / "effect-captures" / "manifest.json"
                add_yakkai_capture(manifest_path, "final-display-before", before)
                add_yakkai_capture(manifest_path, "final-display-after", after)
            variants = compare.load_yakkai_variant_manifests(root)
            report["yakkaiDefaultDeltaOracle"] = compare.build_yakkai_default_delta_oracle(report, variants)

            report_without_night = dict(report)
            report_without_night["variants"] = [
                variant for variant in report["variants"] if variant["variant"] != "night"
            ]
            with self.assertRaisesRegex(compare.FreshPublishError, "missing Windows variant in report: night"):
                compare.build_yakkai_isolated_publish_parity(report_without_night, variants)

            variants_without_night = dict(variants)
            del variants_without_night["night"]
            with self.assertRaisesRegex(compare.FreshPublishError, "missing Yakkai variant manifest: night"):
                compare.build_yakkai_isolated_publish_parity(report, variants_without_night)

    def test_isolated_publish_parity_reports_missing_final_display_capture_file(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            make_package(archive)
            report = compare.load_windows_package(archive)
            default_before = root / "default_before.png"
            default_after = root / "default_after.png"
            Image.new("RGBA", (1280, 720), (51, 76, 102, 255)).save(default_before)
            Image.new("RGBA", (1280, 720), (153, 178, 204, 255)).save(default_after)
            write_yakkai_summary(root, default_before, default_after)
            for variant in ("day", "sunset", "night"):
                manifest_path = root / variant / "effect-captures" / "manifest.json"
                add_yakkai_capture(manifest_path, "final-display-before", root / f"{variant}_missing_before.png")
                add_yakkai_capture(manifest_path, "final-display-after", root / f"{variant}_missing_after.png")
            variants = compare.load_yakkai_variant_manifests(root)
            report["yakkaiDefaultDeltaOracle"] = compare.build_yakkai_default_delta_oracle(report, variants)

            with self.assertRaisesRegex(
                compare.FreshPublishError,
                "day missing final-display-before capture file",
            ):
                compare.build_yakkai_isolated_publish_parity(report, variants)

    def test_content_stage_attribution_ranks_best_yakkai_stage_for_windows_anchors(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            windows_root = root / "windows"
            for variant in ("day", "sunset", "night"):
                variant_root = windows_root / variant
                variant_root.mkdir(parents=True)
                Image.new("RGBA", (8, 8), (10, 20, 30, 255)).save(
                    variant_root / "effect-input-before-visible-effects.png"
                )
                Image.new("RGBA", (8, 8), (20, 40, 60, 255)).save(
                    variant_root / "prefix-3-after-first-lut-pair.png"
                )
                Image.new("RGBA", (8, 8), (30, 60, 90, 255)).save(
                    variant_root / "prefix-7-after-visible-effect-7.png"
                )
                Image.new("RGBA", (8, 8), (90, 120, 150, 255)).save(
                    variant_root / "final-publish-input.png"
                )
            default_before = root / "default_before.png"
            default_after = root / "default_after.png"
            Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(default_before)
            Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(default_after)
            write_yakkai_summary(root, default_before, default_after)
            effect_input = root / "effect_input.png"
            material_1 = root / "material_1.png"
            material_2 = root / "material_2.png"
            Image.new("RGBA", (8, 8), (10, 20, 30, 255)).save(effect_input)
            Image.new("RGBA", (8, 8), (30, 60, 90, 255)).save(material_1)
            Image.new("RGBA", (8, 8), (90, 120, 150, 255)).save(material_2)
            for variant in ("day", "sunset", "night"):
                manifest_path = root / variant / "effect-captures" / "manifest.json"
                add_yakkai_capture(manifest_path, "effect-input", effect_input)
                add_yakkai_material_capture(manifest_path, "material-output-7-0", material_1, "effects/waterwaves", 7)
                add_yakkai_material_capture(manifest_path, "material-output-12-0", material_2, "effects/shake", 12)
            variants = compare.load_yakkai_variant_manifests(root)

            attribution = compare.build_yakkai_content_stage_attribution(variants, windows_root)

            day = attribution["day"]
            self.assertEqual(day["anchors"]["effect-input"]["bestMatch"]["stage"], "effect-input")
            self.assertEqual(day["anchors"]["prefix-7"]["bestMatch"]["stage"], "material-output-7-0")
            self.assertEqual(day["anchors"]["prefix-7"]["bestMatch"]["stageFamily"], "waterwaves")
            self.assertEqual(day["anchors"]["final-publish-input"]["bestMatch"]["stage"], "material-output-12-0")
            self.assertEqual(day["anchors"]["final-publish-input"]["bestMatch"]["stageFamily"], "shake")
            self.assertEqual(day["ranking"][0]["anchor"], "prefix-3")
            self.assertEqual(day["classification"], "content-stage-mismatch")

    def test_content_transition_attribution_ranks_best_yakkai_delta_transition(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            windows_root = root / "windows"
            for variant in ("day", "sunset", "night"):
                variant_root = windows_root / variant
                variant_root.mkdir(parents=True)
                Image.new("RGBA", (8, 8), (10, 10, 10, 255)).save(
                    variant_root / "effect-input-before-visible-effects.png"
                )
                Image.new("RGBA", (8, 8), (20, 20, 20, 255)).save(
                    variant_root / "prefix-3-after-first-lut-pair.png"
                )
                Image.new("RGBA", (8, 8), (50, 40, 30, 255)).save(
                    variant_root / "prefix-7-after-visible-effect-7.png"
                )
                Image.new("RGBA", (8, 8), (55, 45, 35, 255)).save(
                    variant_root / "final-publish-input.png"
                )
            default_before = root / "default_before.png"
            default_after = root / "default_after.png"
            Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(default_before)
            Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(default_after)
            write_yakkai_summary(root, default_before, default_after)
            effect_input = root / "effect_input.png"
            material_2 = root / "material_2.png"
            material_7 = root / "material_7.png"
            material_12 = root / "material_12.png"
            Image.new("RGBA", (8, 8), (10, 10, 10, 255)).save(effect_input)
            Image.new("RGBA", (8, 8), (20, 20, 20, 255)).save(material_2)
            Image.new("RGBA", (8, 8), (50, 40, 30, 255)).save(material_7)
            Image.new("RGBA", (8, 8), (55, 45, 35, 255)).save(material_12)
            for variant in ("day", "sunset", "night"):
                manifest_path = root / variant / "effect-captures" / "manifest.json"
                add_yakkai_capture(manifest_path, "effect-input", effect_input)
                add_yakkai_material_capture(manifest_path, "material-output-2-0", material_2, "effects/pulse", 2)
                add_yakkai_material_capture(manifest_path, "material-output-7-0", material_7, "effects/waterwaves", 7)
                add_yakkai_material_capture(manifest_path, "material-output-12-0", material_12, "effects/shake", 12)
            variants = compare.load_yakkai_variant_manifests(root)

            attribution = compare.build_yakkai_content_transition_attribution(variants, windows_root)

            day = attribution["day"]
            prefix_match = day["transitions"]["prefix-3-to-prefix-7"]["bestMatch"]
            self.assertEqual(prefix_match["fromStage"], "material-output-2-0")
            self.assertEqual(prefix_match["toStage"], "material-output-7-0")
            self.assertEqual(prefix_match["toStageFamily"], "waterwaves")
            self.assertEqual(prefix_match["deltaRmse"], 0.0)
            publish_match = day["transitions"]["prefix-7-to-final-publish-input"]["bestMatch"]
            self.assertEqual(publish_match["fromStage"], "material-output-7-0")
            self.assertEqual(publish_match["toStage"], "material-output-12-0")
            self.assertEqual(day["classification"], "content-transition-close")

    def test_content_range_attribution_finds_non_adjacent_cumulative_range(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            windows_root = root / "windows"
            for variant in ("day", "sunset", "night"):
                variant_root = windows_root / variant
                variant_root.mkdir(parents=True)
                Image.new("RGBA", (8, 8), (10, 10, 10, 255)).save(
                    variant_root / "effect-input-before-visible-effects.png"
                )
                Image.new("RGBA", (8, 8), (20, 20, 20, 255)).save(
                    variant_root / "prefix-3-after-first-lut-pair.png"
                )
                Image.new("RGBA", (8, 8), (70, 60, 50, 255)).save(
                    variant_root / "prefix-7-after-visible-effect-7.png"
                )
                Image.new("RGBA", (8, 8), (70, 60, 50, 255)).save(
                    variant_root / "final-publish-input.png"
                )
            default_before = root / "default_before.png"
            default_after = root / "default_after.png"
            Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(default_before)
            Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(default_after)
            write_yakkai_summary(root, default_before, default_after)
            effect_input = root / "effect_input.png"
            material_2 = root / "material_2.png"
            material_4 = root / "material_4.png"
            material_7 = root / "material_7.png"
            Image.new("RGBA", (8, 8), (10, 10, 10, 255)).save(effect_input)
            Image.new("RGBA", (8, 8), (20, 20, 20, 255)).save(material_2)
            Image.new("RGBA", (8, 8), (45, 40, 35, 255)).save(material_4)
            Image.new("RGBA", (8, 8), (70, 60, 50, 255)).save(material_7)
            for variant in ("day", "sunset", "night"):
                manifest_path = root / variant / "effect-captures" / "manifest.json"
                add_yakkai_capture(manifest_path, "effect-input", effect_input)
                add_yakkai_material_capture(manifest_path, "material-output-2-0", material_2, "effects/pulse", 2)
                add_yakkai_material_capture(manifest_path, "material-output-4-0", material_4, "effects/waterwaves", 4)
                add_yakkai_material_capture(manifest_path, "material-output-7-0", material_7, "effects/waterwaves", 7)
            variants = compare.load_yakkai_variant_manifests(root)

            attribution = compare.build_yakkai_content_range_attribution(variants, windows_root)

            day = attribution["day"]
            best = day["ranges"]["prefix-3-to-prefix-7"]["bestMatch"]
            self.assertEqual(best["fromStage"], "material-output-2-0")
            self.assertEqual(best["toStage"], "material-output-7-0")
            self.assertEqual(best["stageFamilies"], ["pulse", "waterwaves", "waterwaves"])
            self.assertEqual(best["rangeLength"], 2)
            self.assertEqual(best["deltaRmse"], 0.0)
            self.assertEqual(day["classification"], "content-range-close")

    def test_middle_block_microscope_reports_toward_and_away_steps(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            windows_root = root / "windows"
            for variant in ("day", "sunset", "night"):
                variant_root = windows_root / variant
                variant_root.mkdir(parents=True)
                Image.new("RGBA", (8, 8), (10, 10, 10, 255)).save(
                    variant_root / "effect-input-before-visible-effects.png"
                )
                Image.new("RGBA", (8, 8), (20, 20, 20, 255)).save(
                    variant_root / "prefix-3-after-first-lut-pair.png"
                )
                Image.new("RGBA", (8, 8), (70, 60, 50, 255)).save(
                    variant_root / "prefix-7-after-visible-effect-7.png"
                )
                Image.new("RGBA", (8, 8), (70, 60, 50, 255)).save(
                    variant_root / "final-publish-input.png"
                )
            default_before = root / "default_before.png"
            default_after = root / "default_after.png"
            Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(default_before)
            Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(default_after)
            write_yakkai_summary(root, default_before, default_after)
            effect_input = root / "effect_input.png"
            material_2 = root / "material_2.png"
            material_4 = root / "material_4.png"
            material_7 = root / "material_7.png"
            Image.new("RGBA", (8, 8), (20, 20, 20, 255)).save(effect_input)
            Image.new("RGBA", (8, 8), (20, 20, 20, 255)).save(material_2)
            Image.new("RGBA", (8, 8), (55, 45, 35, 255)).save(material_4)
            Image.new("RGBA", (8, 8), (30, 20, 20, 255)).save(material_7)
            for variant in ("day", "sunset", "night"):
                manifest_path = root / variant / "effect-captures" / "manifest.json"
                add_yakkai_capture(manifest_path, "effect-input", effect_input)
                add_yakkai_material_capture(manifest_path, "material-output-2-0", material_2, "effects/pulse", 2)
                add_yakkai_material_capture(manifest_path, "material-output-4-0", material_4, "effects/waterwaves", 4)
                add_yakkai_material_capture(manifest_path, "material-output-7-0", material_7, "effects/waterwaves", 7)
            variants = compare.load_yakkai_variant_manifests(root)

            microscope = compare.build_yakkai_middle_block_microscope(variants, windows_root)

            day = microscope["day"]
            self.assertEqual(day["classification"], "middle-block-regression-step")
            self.assertEqual(day["strongestTowardStep"]["fromStage"], "material-output-2-0")
            self.assertEqual(day["strongestTowardStep"]["toStage"], "material-output-4-0")
            self.assertEqual(day["strongestTowardStep"]["direction"], "toward-prefix-7")
            self.assertLess(day["strongestTowardStep"]["prefix7RmseChange"], 0)
            self.assertEqual(day["strongestAwayStep"]["fromStage"], "material-output-4-0")
            self.assertEqual(day["strongestAwayStep"]["toStage"], "material-output-7-0")
            self.assertEqual(day["selectedTarget"], day["strongestAwayStep"])
            self.assertGreater(day["strongestAwayStep"]["prefix7RmseChange"], 0)

    def test_selected_step_metadata_preserves_material_state_and_windows_request(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            default_before = root / "default_before.png"
            default_after = root / "default_after.png"
            Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(default_before)
            Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(default_after)
            write_yakkai_summary(root, default_before, default_after)

            for variant in ("day", "sunset", "night"):
                manifest_path = root / variant / "effect-captures" / "manifest.json"
                for stage, shader, effect_index, color in (
                    ("material-output-2-0", "effects/pulse", 2, (20, 20, 20, 255)),
                    ("material-output-3-0", "effects/waterwaves", 3, (30, 40, 50, 255)),
                    ("material-output-4-0", "workshop/3165346237/effects/lut_loader", 4, (45, 50, 55, 255)),
                    ("material-output-5-0", "effects/waterwaves", 5, (50, 60, 70, 255)),
                ):
                    path = root / f"{variant}_{stage}.png"
                    Image.new("RGBA", (8, 8), color).save(path)
                    add_yakkai_material_capture(
                        manifest_path,
                        stage,
                        path,
                        shader,
                        effect_index,
                        material_extra={
                            "authoredCombos": {"MASK": "1"},
                            "resolvedCombos": {"MASK": "1", "DUALWAVES": "0"},
                            "defines": ["g_Texture0", "g_Texture1"],
                            "authoredTextures": ["_rt_effect_pingpong_a", "masks/test_mask"],
                            "resolvedTextures": ["_rt_effect_pingpong_a", "masks/test_mask"],
                            "textureBindings": [
                                {"slot": 0, "authored": "_rt_effect_pingpong_a", "resolved": "_rt_effect_pingpong_a"},
                                {"slot": 1, "authored": "masks/test_mask", "resolved": "masks/test_mask"},
                            ],
                            "materialValues": {"speed": [2.5], "strength": [0.088]},
                            "resolvedConstValues": {"g_Speed": [2.5], "g_Strength": [0.088]},
                            "resolvedOutputRenderTarget": "_rt_effect_pingpong_b",
                        },
                    )

            variants = compare.load_yakkai_variant_manifests(root)
            microscope = {
                "day": {
                    "manifestPath": variants["day"]["manifestPath"],
                    "windowsPrefix3Path": str(root / "day_prefix3.png"),
                    "windowsPrefix7Path": str(root / "day_prefix7.png"),
                    "selectedTarget": {
                        "fromStage": "material-output-2-0",
                        "toStage": "material-output-3-0",
                        "fromPath": str(root / "day_material-output-2-0.png"),
                        "toPath": str(root / "day_material-output-3-0.png"),
                        "direction": "away-from-prefix-7",
                        "prefix7RmseChange": 0.01,
                        "windowsBlockDeltaRmse": 0.11,
                        "windowsBlockDeltaCosine": 0.08,
                    },
                    "strongestTowardStep": None,
                    "strongestAwayStep": None,
                },
                "sunset": {
                    "manifestPath": variants["sunset"]["manifestPath"],
                    "windowsPrefix3Path": str(root / "sunset_prefix3.png"),
                    "windowsPrefix7Path": str(root / "sunset_prefix7.png"),
                    "selectedTarget": {
                        "fromStage": "material-output-2-0",
                        "toStage": "material-output-3-0",
                        "fromPath": str(root / "sunset_material-output-2-0.png"),
                        "toPath": str(root / "sunset_material-output-3-0.png"),
                        "direction": "toward-prefix-7",
                        "prefix7RmseChange": -0.01,
                        "windowsBlockDeltaRmse": 0.13,
                        "windowsBlockDeltaCosine": 0.34,
                    },
                    "strongestTowardStep": None,
                    "strongestAwayStep": None,
                },
                "night": {
                    "manifestPath": variants["night"]["manifestPath"],
                    "windowsPrefix3Path": str(root / "night_prefix3.png"),
                    "windowsPrefix7Path": str(root / "night_prefix7.png"),
                    "selectedTarget": {
                        "fromStage": "material-output-4-0",
                        "toStage": "material-output-5-0",
                        "fromPath": str(root / "night_material-output-4-0.png"),
                        "toPath": str(root / "night_material-output-5-0.png"),
                        "direction": "away-from-prefix-7",
                        "prefix7RmseChange": 0.004,
                        "windowsBlockDeltaRmse": 0.21,
                        "windowsBlockDeltaCosine": 0.02,
                    },
                    "strongestTowardStep": {
                        "fromStage": "material-output-2-0",
                        "toStage": "material-output-3-0",
                        "fromPath": str(root / "night_material-output-2-0.png"),
                        "toPath": str(root / "night_material-output-3-0.png"),
                        "direction": "toward-prefix-7",
                        "prefix7RmseChange": -0.09,
                        "windowsBlockDeltaRmse": 0.08,
                        "windowsBlockDeltaCosine": 0.89,
                    },
                    "strongestAwayStep": None,
                },
            }

            metadata = compare.build_yakkai_selected_step_metadata(variants, microscope)
            request = compare.middle_block_windows_request_markdown(metadata, scene_id="3228578419", layer_id=405)

            day_step = metadata["day"]["steps"][0]
            self.assertEqual(day_step["fromMaterial"]["shader"], "effects/pulse")
            self.assertEqual(day_step["toMaterial"]["shader"], "effects/waterwaves")
            self.assertEqual(day_step["toMaterial"]["resolvedCombos"]["DUALWAVES"], "0")
            self.assertEqual(day_step["toMaterial"]["materialValues"]["speed"], [2.5])
            self.assertEqual(day_step["toMaterial"]["textureBindings"][1]["resolved"], "masks/test_mask")
            self.assertEqual(day_step["toImageStats"]["dimensions"], [8, 8])
            for actual, expected in zip(
                day_step["toImageStats"]["meanRgba"],
                [30 / 255.0, 40 / 255.0, 50 / 255.0, 1.0],
            ):
                self.assertAlmostEqual(actual, expected)
            self.assertEqual(
                [step["toStage"] for step in metadata["night"]["steps"]],
                ["material-output-5-0", "material-output-3-0"],
            )
            self.assertIn("fresh Layer 405 internal pass export request", request)
            self.assertIn("day: `material-output-2-0 -> material-output-3-0`", request)
            self.assertIn("night: `material-output-4-0 -> material-output-5-0`", request)
            self.assertIn("export every internal pass output between Windows `prefix-3` and `prefix-7`", request)

    def test_boundary_stage_check_detects_late_final_publish_delta(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            before = root / "before.png"
            after = root / "after.png"
            final_publish = root / "final_publish.png"
            Image.new("RGBA", (1280, 720), (0, 0, 0, 255)).save(before)
            Image.new("RGBA", (1280, 720), (0, 0, 0, 255)).save(after)
            final_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            final_image.putpixel((768, 512), (80, 0, 0, 255))
            final_image.save(final_publish)
            sample = {
                "sampleName": "lowerRibbon",
                "beforeSample": {"samplePixel": [768, 512]},
                "coordinate": [1536, 1024],
                "coordinateDimensions": [2560, 1440],
            }

            boundary = compare.build_boundary_stage_locator_sample(sample, after, final_publish)

            self.assertEqual(boundary["classification"], "delta-at-windows-sample")
            self.assertGreater(boundary["sampleMagnitude"], 0.1)

    def test_isolated_publish_sample_compares_windows_and_yakkai_deltas(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            before = root / "final_before.png"
            after = root / "final_after.png"
            before_image = Image.new("RGBA", (1280, 720), (10, 20, 30, 255))
            after_image = Image.new("RGBA", (1280, 720), (10, 20, 30, 255))
            after_image.putpixel((768, 512), (60, 20, 30, 255))
            before_image.save(before)
            after_image.save(after)
            windows_variant = {
                "variant": "day",
                "pixelHistory": {
                    "lowerRibbon": {
                        "preModRgba": [10, 20, 30, 255],
                        "postModRgba": [60, 20, 30, 255],
                    }
                },
            }
            oracle_sample = {
                "sampleName": "lowerRibbon",
                "coordinate": [1536, 1024],
                "coordinateDimensions": [2560, 1440],
                "beforeSample": {"samplePixel": [768, 512]},
            }

            sample = compare.build_yakkai_isolated_publish_sample(
                windows_variant,
                oracle_sample,
                before,
                after,
                from_stage="final-display-before",
                to_stage="final-display-after",
            )

            self.assertEqual(sample["classification"], "isolated-publish-close")
            self.assertEqual(sample["fromStage"], "final-display-before")
            self.assertEqual(sample["toStage"], "final-display-after")
            self.assertEqual(sample["beforeSample"]["samplePixel"], [768, 512])
            self.assertEqual(sample["afterSample"]["samplePixel"], [768, 512])
            self.assertEqual(sample["metrics"]["classification"], "default-delta-close")

    def test_yakkai_default_delta_oracle_reports_matching_scaled_sample(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            make_package(archive)
            report = compare.load_windows_package(archive)

            before = root / "before.png"
            after = root / "after.png"
            before_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            after_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            before_image.putpixel((768, 512), (51, 76, 102, 255))
            after_image.putpixel((768, 512), (153, 178, 204, 255))
            before_image.putpixel((640, 0), (51, 76, 102, 255))
            after_image.putpixel((640, 0), (153, 178, 204, 255))
            before_image.save(before)
            after_image.save(after)
            write_yakkai_summary(root, before, after)

            oracle = compare.build_yakkai_default_delta_oracle(
                report,
                compare.load_yakkai_variant_manifests(root),
            )

            self.assertEqual(oracle["day"]["classification"], "default-delta-close")
            self.assertEqual(
                oracle["day"]["samples"]["lowerRibbon"]["beforeSample"]["samplePixel"],
                [768, 512],
            )
            self.assertEqual(
                oracle["day"]["samples"]["lowerRibbon"]["afterSample"]["samplePixel"],
                [768, 512],
            )
            self.assertEqual(
                oracle["day"]["samples"]["lowerRibbon"]["metrics"]["classification"],
                "default-delta-close",
            )
            self.assertEqual(
                oracle["day"]["samples"]["transparentEdge"]["metrics"]["classification"],
                "default-delta-close",
            )

    def test_yakkai_default_delta_oracle_uses_matching_final_publish_state_row(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            make_package(archive)
            report = compare.load_windows_package(archive)
            bad_row = dict(report["variants"][0]["finalPublish"]["stateCsvRows"][0])
            bad_row.update(
                {
                    "replay_event": "999",
                    "draw_id": "999",
                    "lower_ribbon_x": "0",
                    "lower_ribbon_y": "0",
                    "transparent_edge_x": "0",
                    "transparent_edge_y": "0",
                }
            )
            report["variants"][0]["finalPublish"]["stateCsvRows"].insert(0, bad_row)

            before = root / "before.png"
            after = root / "after.png"
            before_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            after_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            before_image.putpixel((768, 512), (51, 76, 102, 255))
            after_image.putpixel((768, 512), (153, 178, 204, 255))
            before_image.putpixel((640, 0), (51, 76, 102, 255))
            after_image.putpixel((640, 0), (153, 178, 204, 255))
            before_image.save(before)
            after_image.save(after)
            write_yakkai_summary(root, before, after)

            oracle = compare.build_yakkai_default_delta_oracle(
                report,
                compare.load_yakkai_variant_manifests(root),
            )

            self.assertEqual(oracle["day"]["classification"], "default-delta-close")
            self.assertEqual(
                oracle["day"]["samples"]["lowerRibbon"]["beforeSample"]["samplePixel"],
                [768, 512],
            )

    def test_yakkai_default_delta_oracle_rejects_mismatched_before_after_sample_pixels(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            make_package(archive)
            report = compare.load_windows_package(archive)

            before = root / "before.png"
            after = root / "after.png"
            before_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            after_image = Image.new("RGBA", (640, 360), (0, 0, 0, 255))
            before_image.putpixel((768, 512), (51, 76, 102, 255))
            after_image.putpixel((384, 256), (153, 178, 204, 255))
            before_image.save(before)
            after_image.save(after)
            write_yakkai_summary(root, before, after)

            with self.assertRaisesRegex(compare.FreshPublishError, "sample pixel mismatch"):
                compare.build_yakkai_default_delta_oracle(
                    report,
                    compare.load_yakkai_variant_manifests(root),
                )

    def test_sample_definition_rejects_fractional_state_coordinates(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / "fresh.zip"
            make_package(archive)
            report = compare.load_windows_package(archive)
            state_rows = report["variants"][0]["finalPublish"]["stateCsvRows"]
            state_rows[0]["lower_ribbon_x"] = "1536.5"

            with self.assertRaisesRegex(compare.FreshPublishError, "non-integer"):
                compare.sample_definition_from_state_rows(
                    state_rows,
                    "day",
                    621,
                    1140,
                )

    def test_cli_rejects_both_yakkai_manifest_and_root(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            make_package(archive)
            manifest = root / "manifest.json"
            manifest.write_text("{}", encoding="utf-8")

            with self.assertRaises(SystemExit):
                compare.main(
                    [
                        "--windows",
                        str(archive),
                        "--output",
                        str(root / "out"),
                        "--yakkai-manifest",
                        str(manifest),
                        "--yakkai-root",
                        str(root),
                    ]
                )

    def test_extract_windows_package_rejects_entries_outside_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            with zipfile.ZipFile(archive, "w") as zip_file:
                zip_file.writestr("layer405_final_publish_composite_fresh/source_manifest.json", "{}")
                zip_file.writestr("../escape.txt", "outside")

            with self.assertRaisesRegex(compare.FreshPublishError, "refuses to extract outside output"):
                compare.extract_windows_package(archive, root / "out")

            self.assertFalse((root / "escape.txt").exists())

    def test_cli_writes_windows_registration_summary(self):
        small_sizes = {
            "effect-input-before-visible-effects.png": (64, 48),
            "prefix-3-after-first-lut-pair.png": (64, 48),
            "prefix-7-after-visible-effect-7.png": (64, 48),
            "final-publish-input.png": (64, 48),
            "default-before-final-publish.png": (32, 24),
            "default-after-final-publish.png": (32, 24),
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            output = root / "out"
            make_package(archive, image_sizes=small_sizes)

            compare.main(["--windows", str(archive), "--output", str(output)])

            summary = json.loads((output / "summary.json").read_text(encoding="utf-8"))
            self.assertIn("windowsRegistration", summary)
            self.assertIn("finalInputToDefaultAfter", summary["windowsRegistration"]["day"])
            self.assertIn("finalInputToDefaultBefore", summary["windowsRegistration"]["night"])
            markdown = (output / "summary.md").read_text(encoding="utf-8")
            self.assertIn("## Windows Registration", markdown)
            self.assertIn("| day | `registered-layer-local-to-default` |", markdown)

    def test_cli_writes_yakkai_default_delta_oracle_summary(self):
        small_sizes = {
            "effect-input-before-visible-effects.png": (64, 48),
            "prefix-3-after-first-lut-pair.png": (64, 48),
            "prefix-7-after-visible-effect-7.png": (64, 48),
            "final-publish-input.png": (64, 48),
            "default-before-final-publish.png": (32, 24),
            "default-after-final-publish.png": (32, 24),
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            output = root / "out"
            make_package(archive, image_sizes=small_sizes)
            before = root / "before.png"
            after = root / "after.png"
            before_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            after_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            before_image.putpixel((768, 512), (51, 76, 102, 255))
            after_image.putpixel((768, 512), (153, 178, 204, 255))
            before_image.putpixel((640, 0), (51, 76, 102, 255))
            after_image.putpixel((640, 0), (153, 178, 204, 255))
            before_image.save(before)
            after_image.save(after)
            write_yakkai_summary(root, before, after)
            for variant in ("day", "sunset", "night"):
                manifest_path = root / variant / "effect-captures" / "manifest.json"
                add_yakkai_capture(manifest_path, "final-display-before", before)
                add_yakkai_capture(manifest_path, "final-display-after", after)

            compare.main(["--windows", str(archive), "--output", str(output), "--yakkai-root", str(root)])

            summary = json.loads((output / "summary.json").read_text(encoding="utf-8"))
            self.assertEqual(
                summary["yakkaiDefaultDeltaOracle"]["day"]["classification"],
                "default-delta-close",
            )
            self.assertEqual(
                summary["yakkaiDefaultDeltaOracle"]["day"]["samples"]["lowerRibbon"]["metrics"]["classification"],
                "default-delta-close",
            )
            markdown = (output / "summary.md").read_text(encoding="utf-8")
            self.assertIn("## Yakkai Default-Delta Oracle", markdown)
            self.assertIn("| Variant | Sample | Classification | Delta RMSE | Delta Cosine | Yakkai Pixel |", markdown)
            self.assertIn("| day | lowerRibbon | `default-delta-close` | 0.000000 | 1.000000 |", markdown)
            self.assertIn("| day | transparentEdge | `default-delta-close` |", markdown)
            self.assertIn("`[768, 512]`", markdown)

    def test_cli_writes_yakkai_default_delta_locator_summary_and_crops(self):
        small_sizes = {
            "effect-input-before-visible-effects.png": (64, 48),
            "prefix-3-after-first-lut-pair.png": (64, 48),
            "prefix-7-after-visible-effect-7.png": (64, 48),
            "final-publish-input.png": (64, 48),
            "default-before-final-publish.png": (32, 24),
            "default-after-final-publish.png": (32, 24),
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fresh.zip"
            output = root / "out"
            make_package(archive, image_sizes=small_sizes)
            before = root / "before.png"
            after = root / "after.png"
            before_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            after_image = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            after_image.putpixel((788, 512), (100, 0, 0, 255))
            before_image.save(before)
            after_image.save(after)
            write_yakkai_summary(root, before, after)
            for variant in ("day", "sunset", "night"):
                manifest_path = root / variant / "effect-captures" / "manifest.json"
                add_yakkai_capture(manifest_path, "final-display-before", before)
                add_yakkai_capture(manifest_path, "final-display-after", after)
                add_yakkai_capture(manifest_path, "effect-input", root / f"{variant}_effect_input.png")
                add_yakkai_material_capture(
                    manifest_path,
                    "material-output-7-0",
                    root / f"{variant}_material_7.png",
                    "effects/waterwaves",
                    7,
                )
                Image.new("RGBA", (64, 48), (32, 64, 96, 255)).save(root / f"{variant}_effect_input.png")
                Image.new("RGBA", (64, 48), (32, 64, 96, 255)).save(root / f"{variant}_material_7.png")

            compare.main(["--windows", str(archive), "--output", str(output), "--yakkai-root", str(root)])

            summary = json.loads((output / "summary.json").read_text(encoding="utf-8"))
            self.assertEqual(summary["yakkaiDefaultDeltaLocator"]["day"]["classification"], "delta-nearby")
            self.assertIn("yakkaiIsolatedPublishParity", summary)
            self.assertIn("day", summary["yakkaiIsolatedPublishParity"])
            self.assertIn("yakkaiContentStageAttribution", summary)
            self.assertIn("final-publish-input", summary["yakkaiContentStageAttribution"]["day"]["anchors"])
            self.assertIn("yakkaiContentTransitionAttribution", summary)
            self.assertIn(
                "prefix-3-to-prefix-7",
                summary["yakkaiContentTransitionAttribution"]["day"]["transitions"],
            )
            self.assertIn("yakkaiContentRangeAttribution", summary)
            self.assertIn(
                "prefix-3-to-prefix-7",
                summary["yakkaiContentRangeAttribution"]["day"]["ranges"],
            )
            self.assertIn("yakkaiMiddleBlockMicroscope", summary)
            self.assertIn("steps", summary["yakkaiMiddleBlockMicroscope"]["day"])
            self.assertIn("yakkaiSelectedStepMetadata", summary)
            self.assertEqual(
                summary["yakkaiSelectedStepMetadata"]["day"]["steps"][0]["toMaterial"]["shader"],
                "effects/waterwaves",
            )
            markdown = (output / "summary.md").read_text(encoding="utf-8")
            self.assertIn("## Yakkai Default-Delta Locator", markdown)
            self.assertIn("| day | lowerRibbon | `delta-nearby` |", markdown)
            self.assertIn("## Yakkai Layer Final-Publish Boundary", markdown)
            self.assertIn("| day | `final-display-before -> final-display-after` | `delta-nearby` |", markdown)
            self.assertIn("## Yakkai Isolated Final-Publish Parity", markdown)
            self.assertIn("## Yakkai Content Stage Attribution", markdown)
            self.assertIn("## Yakkai Content Transition Attribution", markdown)
            self.assertIn("## Yakkai Content Range Attribution", markdown)
            self.assertIn("## Yakkai Middle-Block Microscope", markdown)
            self.assertIn("## Yakkai Selected Step Metadata", markdown)
            self.assertTrue((output / "locator-crops" / "day-lowerRibbon-sample-delta.png").exists())
            self.assertTrue((output / "locator-crops" / "day-lowerRibbon-nearest-delta.png").exists())
            self.assertTrue(
                (output / "isolated-publish-crops" / "day-lowerRibbon-final-display-delta.png").exists()
            )
            self.assertTrue(
                (output / "content-stage-crops" / "day-final-publish-input-lowerRibbon-material-output-7-0.png").exists()
            )
            self.assertTrue(
                (
                    output
                    / "content-transition-crops"
                    / "day-prefix-3-to-prefix-7-lowerRibbon-effect-input-to-material-output-7-0.png"
                ).exists()
            )
            self.assertTrue(
                (
                    output
                    / "content-range-crops"
                    / "day-prefix-3-to-prefix-7-lowerRibbon-effect-input-to-material-output-7-0.png"
                ).exists()
            )
            self.assertTrue(
                (
                    output
                    / "middle-block-crops"
                    / "day-selected-lowerRibbon-effect-input-to-material-output-7-0.png"
                ).exists()
            )
            self.assertTrue(
                (
                    output
                    / "selected-step-crops"
                    / "day-selectedTarget-lowerRibbon-effect-input-to-material-output-7-0.png"
                ).exists()
            )
            self.assertTrue((output / "middle-block-windows-request.md").exists())
            request = (output / "middle-block-windows-request.md").read_text(encoding="utf-8")
            self.assertIn("fresh Layer 405 internal pass export request", request)


if __name__ == "__main__":
    unittest.main()
