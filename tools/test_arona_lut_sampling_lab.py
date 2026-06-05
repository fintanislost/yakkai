import struct
import tempfile
import unittest
import json
from pathlib import Path

import numpy as np
from PIL import Image

import tools.arona_lut_sampling_lab as lab


def sized_string(value: str) -> bytes:
    data = value.encode("utf-8")
    return struct.pack("<i", len(data)) + data


def make_pkg(entries: dict[str, bytes]) -> bytes:
    header = bytearray()
    payload = bytearray()
    header += sized_string("PKGV0022")
    header += struct.pack("<i", len(entries))
    for path, data in entries.items():
        header += sized_string(path)
        header += struct.pack("<ii", len(payload), len(data))
        payload += data
    return bytes(header + payload)


def version(name: str, number: int) -> bytes:
    return f"{name}{number:04d}".encode("ascii") + b"\0"


def make_tex_rgba8(width: int, height: int, pixels: bytes, flags: int = 2) -> bytes:
    tex = bytearray()
    tex += version("TEXV", 5)
    tex += version("TEXI", 1)
    tex += struct.pack("<iIiiiii", 0, flags, width, height, width, height, 0)
    tex += version("TEXB", 3)
    tex += struct.pack("<i", 1)
    tex += struct.pack("<i", -1)
    tex += struct.pack("<i", 1)
    tex += struct.pack("<ii", width, height)
    tex += struct.pack("<i", 0)
    tex += struct.pack("<i", 0)
    tex += struct.pack("<i", len(pixels))
    tex += pixels
    return bytes(tex)


def make_gradient_source(width: int, height: int) -> np.ndarray:
    x = np.linspace(0.0, 1.0, width, dtype=np.float32)
    y = np.linspace(0.0, 1.0, height, dtype=np.float32)
    xx, yy = np.meshgrid(x, y)
    blue = np.full_like(xx, 0.625)
    alpha = np.ones_like(xx)
    return np.stack([xx, yy, blue, alpha], axis=2)


def make_identity_lut(quad_size: int) -> np.ndarray:
    tiles_per_row = int(round(quad_size ** 0.5))
    size = quad_size * tiles_per_row
    lut = np.zeros((size, size, 4), dtype=np.float32)
    for slice_index in range(quad_size):
        tile_x = slice_index % tiles_per_row
        tile_y = slice_index // tiles_per_row
        for y in range(quad_size):
            for x in range(quad_size):
                px = tile_x * quad_size + x
                py = tile_y * quad_size + y
                lut[py, px] = [
                    x / max(quad_size - 1, 1),
                    y / max(quad_size - 1, 1),
                    slice_index / max(quad_size - 1, 1),
                    1.0,
                ]
    return lut


def make_tinted_lut(quad_size: int) -> np.ndarray:
    lut = make_identity_lut(quad_size)
    lut[:, :, 0] = np.clip(lut[:, :, 0] * 0.65 + 0.08, 0.0, 1.0)
    lut[:, :, 1] = np.clip(lut[:, :, 1] * 0.85 + 0.03, 0.0, 1.0)
    lut[:, :, 2] = np.clip(lut[:, :, 2] * 1.10, 0.0, 1.0)
    return lut


class PkgAndTexTests(unittest.TestCase):
    def test_pkg_index_reads_named_file(self):
        payload = b"hello"
        pkg = make_pkg({"materials/test.tex": payload})

        index = lab.PkgIndex.from_bytes(pkg)

        self.assertEqual(index.read("materials/test.tex"), payload)

    def test_decode_uncompressed_rgba8_tex(self):
        tex = make_tex_rgba8(2, 1, bytes([255, 0, 0, 255, 0, 255, 0, 255]))

        image = lab.decode_tex_rgba8(tex)

        self.assertEqual(image.width, 2)
        self.assertEqual(image.height, 1)
        np.testing.assert_allclose(
            image.pixels,
            np.array([[[1.0, 0.0, 0.0, 1.0], [0.0, 1.0, 0.0, 1.0]]], dtype=np.float32),
        )

    def test_decode_tex_reports_sampler_flags(self):
        linear = lab.decode_tex_rgba8(make_tex_rgba8(1, 1, bytes([255, 0, 0, 255]), flags=0))
        clamped_nearest = lab.decode_tex_rgba8(
            make_tex_rgba8(1, 1, bytes([255, 0, 0, 255]), flags=3)
        )

        self.assertEqual(linear.filter_mode, "bilinear")
        self.assertEqual(linear.wrap_mode, "repeat")
        self.assertFalse(linear.no_interpolation)
        self.assertFalse(linear.clamp_uvs)
        self.assertEqual(clamped_nearest.filter_mode, "nearest")
        self.assertEqual(clamped_nearest.wrap_mode, "clamp")
        self.assertTrue(clamped_nearest.no_interpolation)
        self.assertTrue(clamped_nearest.clamp_uvs)


class LutSamplingTests(unittest.TestCase):
    def test_identity_lut_preserves_rgb(self):
        lut = make_identity_lut(16)
        source = np.array([[[0.2, 0.4, 0.6, 1.0]]], dtype=np.float32)

        out = lab.apply_lut(
            source,
            lut,
            lab.LutCandidate(quad_size=16, flip_y=False, filter_mode="bilinear", color_mode="shader"),
        )

        self.assertLess(lab.rmse(out[:, :, :3], source[:, :, :3]), 0.02)

    def test_candidate_ranking_prefers_matching_quad_size(self):
        source = make_gradient_source(32, 16)
        lut = make_tinted_lut(64)
        expected = lab.apply_lut(
            source,
            lut,
            lab.LutCandidate(quad_size=64, flip_y=False, filter_mode="bilinear", color_mode="shader"),
        )

        candidates = lab.rank_lut_candidates(source, expected, lut)

        self.assertEqual(candidates[0].candidate.quad_size, 64)

    def test_apply_lut_respects_normal_blend_opacity(self):
        lut = make_tinted_lut(16)
        source = np.array([[[0.2, 0.4, 0.6, 1.0]]], dtype=np.float32)
        sampled = lab.apply_lut(
            source,
            lut,
            lab.LutCandidate(quad_size=16, flip_y=False, filter_mode="bilinear", color_mode="shader"),
        )

        blended = lab.apply_lut(
            source,
            lut,
            lab.LutCandidate(quad_size=16, flip_y=False, filter_mode="bilinear", color_mode="shader"),
            lab.MaterialContext(blend_mode=0, multiply=0.5, translucent_compensation=0.0, clamp=True),
        )

        np.testing.assert_allclose(
            blended[:, :, :3],
            source[:, :, :3] * 0.5 + sampled[:, :, :3] * 0.5,
            atol=0.01,
        )

    def test_score_prediction_ignores_transparent_pixel_drift_for_visible_metrics(self):
        expected = np.array(
            [
                [[0.1, 0.2, 0.3, 1.0], [0.2, 0.3, 0.4, 1.0]],
                [[0.3, 0.4, 0.5, 0.0], [0.4, 0.5, 0.6, 0.0]],
            ],
            dtype=np.float32,
        )
        predicted = expected.copy()
        predicted[1, :, :3] = 1.0 - predicted[1, :, :3]

        metrics = lab.score_prediction(predicted, expected)

        self.assertGreater(metrics["rmse"], 0.0)
        self.assertEqual(metrics["visibleFraction"], 0.5)
        self.assertEqual(metrics["opaqueFraction"], 0.5)
        self.assertLess(metrics["alphaWeightedRmse"], 0.001)
        self.assertLess(metrics["opaqueRmse"], 0.001)

    def test_alpha_weighted_ranking_prefers_visible_pixels(self):
        source = np.array(
            [
                [[0.2, 0.4, 0.6, 1.0], [0.3, 0.5, 0.7, 1.0]],
                [[0.8, 0.2, 0.4, 0.0], [0.7, 0.3, 0.5, 0.0]],
            ],
            dtype=np.float32,
        )
        source[..., 3] = np.array([[1.0, 1.0], [0.0, 0.0]], dtype=np.float32)
        lut = make_identity_lut(16)
        visible_match = lab.LutCandidate(
            quad_size=16,
            flip_y=False,
            filter_mode="nearest",
            color_mode="shader",
        )
        transparent_match = lab.LutCandidate(
            quad_size=16,
            flip_y=True,
            filter_mode="nearest",
            color_mode="shader",
        )
        expected = lab.apply_lut(source, lut, visible_match)
        transparent_prediction = lab.apply_lut(source, lut, transparent_match)
        expected[1, :, :3] = transparent_prediction[1, :, :3]

        candidates = lab.rank_lut_candidates(
            source,
            expected,
            lut,
            candidates=[transparent_match, visible_match],
            score_mode="alpha-weighted",
        )

        self.assertEqual(candidates[0].candidate, visible_match)
        self.assertEqual(candidates[0].score_mode, "alpha-weighted")
        self.assertIn("alphaWeightedRmse", candidates[0].metrics)
        self.assertIn("opaqueRmse", candidates[0].metrics)
        self.assertEqual(candidates[0].metrics["visibleFraction"], 0.5)
        self.assertEqual(candidates[0].metrics["opaqueFraction"], 0.5)


class AronaArtifactWiringTests(unittest.TestCase):
    def test_compare_layer_reports_manifest_quad_size_mismatch(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_dir = root / "captures" / "3228578419" / "82_WALL"
            capture_dir.mkdir(parents=True)
            source = make_gradient_source(8, 8)
            lut = make_tinted_lut(64)
            expected = lab.apply_lut(
                source,
                lut,
                lab.LutCandidate(quad_size=64, flip_y=False, filter_mode="bilinear", color_mode="shader"),
            )
            input_path = capture_dir / "effect_input.tga"
            output_path = capture_dir / "effect_output.tga"
            Image.fromarray((source * 255.0).astype(np.uint8), "RGBA").save(input_path)
            Image.fromarray((expected * 255.0).astype(np.uint8), "RGBA").save(output_path)

            source_pkg = root / "scene.pkg"
            source_pkg.write_bytes(make_pkg({"materials/night.tex": make_tex_rgba8(512, 512, (lut * 255.0).astype(np.uint8).tobytes())}))
            manifest_path = root / "manifest.json"
            manifest_path.write_text(
                """{
  "captures": [
    {
      "stage": "effect-input",
      "path": "%s",
      "renderTargetInfo": {"width": 8, "height": 8},
      "layer": {
        "layerId": 82,
        "layerName": "WALL",
        "effectMaterials": [
          {
            "shader": "workshop/3165346237/effects/lut_loader",
            "resolvedCombos": {"QUAD_SIZE": "16"},
            "materialValues": {"multiply1": [1.0]},
            "textureBindings": [
              {"slot": 0, "resolved": "_rt_effect_pingpong_a"},
              {"slot": 1, "resolved": "night"}
            ]
          }
        ]
      }
    },
    {
      "stage": "effect-output",
      "path": "%s",
      "renderTargetInfo": {"width": 8, "height": 8},
      "layer": {"layerId": 82, "layerName": "WALL"}
    }
  ]
}
"""
                % (input_path, output_path),
                encoding="utf-8",
            )
            summary_path = root / "summary.json"
            summary_path.write_text(
                """{"variants": [{"name": "night", "effectManifest": "%s"}]}""" % manifest_path,
                encoding="utf-8",
            )

            result = lab.compare_layer(summary_path, "night", 82, source_pkg, root / "out", 8)

            self.assertEqual(result["texture"], "night")
            self.assertEqual(result["candidates"][0]["quad_size"], 64)
            self.assertEqual(result["manifestCandidate"]["quad_size"], 16)
            self.assertGreater(result["manifestCandidate"]["rank"], 1)
            self.assertIn("texture dimensions suggest QUAD_SIZE=64", "\n".join(result["warnings"]))
            self.assertTrue((root / "out" / "lut-sampling-report.md").is_file())

    def test_compare_layer_prefers_material_output_stage(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_dir = root / "captures" / "3228578419" / "82_WALL"
            capture_dir.mkdir(parents=True)
            source = make_gradient_source(8, 8)
            source = np.round(source * 255.0) / 255.0
            lut = make_tinted_lut(16)
            material_output = lab.apply_lut(
                source,
                lut,
                lab.LutCandidate(quad_size=16, flip_y=False, filter_mode="bilinear", color_mode="shader"),
            )
            screen_output = np.zeros((4, 4, 4), dtype=np.float32)
            screen_output[:, :, 3] = 1.0
            input_path = capture_dir / "effect_input.tga"
            material_path = capture_dir / "material_output_1_0.tga"
            output_path = capture_dir / "effect_output.tga"
            Image.fromarray((source * 255.0).astype(np.uint8), "RGBA").save(input_path)
            Image.fromarray((material_output * 255.0).astype(np.uint8), "RGBA").save(material_path)
            Image.fromarray((screen_output * 255.0).astype(np.uint8), "RGBA").save(output_path)

            source_pkg = root / "scene.pkg"
            source_pkg.write_bytes(make_pkg({"materials/sunset.tex": make_tex_rgba8(64, 64, (lut * 255.0).astype(np.uint8).tobytes())}))
            manifest_path = root / "manifest.json"
            common_layer = """{
        "layerId": 82,
        "layerName": "WALL",
        "publish": {
          "enabled": true,
          "finalBlendMode": 0,
          "fullscreen": false,
          "composelayer": false,
          "standalonePuppetFinalDisplay": false,
          "publishFinalOutput": true,
          "finalNodeUsesOriginalParent": true,
          "effectInputNodeReset": true,
          "parentId": 44,
          "hasParsedParentNode": true,
          "effectInputRenderTarget": "_rt_effect_ppong_a",
          "effectPingPongA": "_rt_effect_ppong_a",
          "effectPingPongB": "_rt_effect_ppong_b",
          "effectOutputSourceTarget": "_rt_default",
          "finalPublishRenderTarget": "_rt_default",
          "materialOutputCaptureTiming": "effect-command-copy-after-layer-node",
          "finalPublishCaptureTiming": "post-frame-render-target-dump"
        },
        "effectMaterials": [
          {
            "shader": "workshop/3165346237/effects/lut_loader",
            "resolvedCombos": {"QUAD_SIZE": "16", "LUT_FLIP_Y": "0"},
            "finalPublishedMaterial": true,
            "debugSourceFinalEffectOutput": true,
            "debugMaterialOutputSourceRenderTarget": "_rt_default",
            "materialValues": {"multiply1": [1.0]},
            "textureBindings": [
              {"slot": 0, "resolved": "_rt_effect_pingpong_a"},
              {"slot": 1, "resolved": "sunset"}
            ]
          }
        ]
      }"""
            manifest_path.write_text(
                """{
  "captures": [
    {
      "stage": "effect-input",
      "path": "%s",
      "renderTargetInfo": {"width": 8, "height": 8},
      "layer": %s
    },
    {
      "stage": "material-output-1-0",
      "path": "%s",
      "renderTargetInfo": {"width": 8, "height": 8},
      "layer": %s
    },
    {
      "stage": "effect-output",
      "path": "%s",
      "renderTargetInfo": {"width": 4, "height": 4},
      "layer": %s
    }
  ]
}
"""
                % (input_path, common_layer, material_path, common_layer, output_path, common_layer),
                encoding="utf-8",
            )
            summary_path = root / "summary.json"
            summary_path.write_text(
                """{"variants": [{"name": "sunset", "effectManifest": "%s"}]}""" % manifest_path,
                encoding="utf-8",
            )

            result = lab.compare_layer(summary_path, "sunset", 82, source_pkg, root / "out", 8)

            self.assertEqual(result["outputStage"], "material-output-1-0")
            self.assertEqual(result["manifestCandidate"]["rank"], 1)
            self.assertEqual(result["warnings"], [])

    def test_compare_layer_prefers_local_material_output_stage(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_dir = root / "captures" / "3228578419" / "82_WALL"
            capture_dir.mkdir(parents=True)
            source = make_gradient_source(8, 8)
            source = np.round(source * 255.0) / 255.0
            lut = make_tinted_lut(16)
            material_output = lab.apply_lut(
                source,
                lut,
                lab.LutCandidate(quad_size=16, flip_y=False, filter_mode="bilinear", color_mode="shader"),
            )
            input_path = capture_dir / "effect_input.tga"
            local_path = capture_dir / "material_output_local_1_0.tga"
            final_path = capture_dir / "material_output_1_0.tga"
            Image.fromarray((source * 255.0).astype(np.uint8), "RGBA").save(input_path)
            Image.fromarray((material_output * 255.0).astype(np.uint8), "RGBA").save(local_path)
            Image.fromarray((material_output * 255.0).astype(np.uint8), "RGBA").resize((16, 8)).save(final_path)

            source_pkg = root / "scene.pkg"
            source_pkg.write_bytes(make_pkg({"materials/sunset.tex": make_tex_rgba8(64, 64, (lut * 255.0).astype(np.uint8).tobytes())}))
            manifest_path = root / "manifest.json"
            common_layer = """{
        "layerId": 82,
        "layerName": "WALL",
        "publish": {
          "enabled": true,
          "finalBlendMode": 0,
          "fullscreen": false,
          "composelayer": false,
          "standalonePuppetFinalDisplay": false,
          "publishFinalOutput": true,
          "finalNodeUsesOriginalParent": true,
          "effectInputNodeReset": true,
          "parentId": 44,
          "hasParsedParentNode": true,
          "effectInputRenderTarget": "_rt_effect_ppong_a",
          "effectPingPongA": "_rt_effect_ppong_a",
          "effectPingPongB": "_rt_effect_ppong_b",
          "effectOutputSourceTarget": "_rt_default",
          "finalPublishRenderTarget": "_rt_default",
          "materialOutputCaptureTiming": "effect-command-copy-after-layer-node",
          "finalPublishCaptureTiming": "post-frame-render-target-dump"
        },
        "effectMaterials": [
          {
            "shader": "workshop/3165346237/effects/lut_loader",
            "resolvedCombos": {"QUAD_SIZE": "16", "LUT_FLIP_Y": "0"},
            "finalPublishedMaterial": true,
            "debugSourceFinalEffectOutput": true,
            "debugMaterialOutputSourceRenderTarget": "_rt_default",
            "materialValues": {"multiply1": [1.0]},
            "textureBindings": [
              {"slot": 0, "resolved": "_rt_effect_pingpong_a"},
              {"slot": 1, "resolved": "sunset"}
            ]
          }
        ]
      }"""
            manifest_path.write_text(
                """{
  "captures": [
    {
      "stage": "effect-input",
      "path": "%s",
      "renderTargetInfo": {"width": 8, "height": 8},
      "layer": %s
    },
    {
      "stage": "material-output-1-0",
      "path": "%s",
      "renderTargetInfo": {"width": 16, "height": 8},
      "layer": %s
    },
    {
      "stage": "material-output-local-1-0",
      "path": "%s",
      "renderTargetInfo": {"width": 8, "height": 8},
      "layer": %s
    }
  ]
}
"""
                % (input_path, common_layer, final_path, common_layer, local_path, common_layer),
                encoding="utf-8",
            )
            summary_path = root / "summary.json"
            summary_path.write_text(
                """{"variants": [{"name": "sunset", "effectManifest": "%s"}]}""" % manifest_path,
                encoding="utf-8",
            )

            result = lab.compare_layer(summary_path, "sunset", 82, source_pkg, root / "out", 8)

            self.assertEqual(result["outputStage"], "material-output-local-1-0")
            self.assertTrue(result["visibility"]["dimensionCompatible"])
            self.assertTrue(result["visibility"]["trustedComparison"])
            self.assertEqual(result["warnings"], [])

    def test_compare_layer_reports_local_to_final_publish_drift(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_dir = root / "captures" / "3228578419" / "82_WALL"
            capture_dir.mkdir(parents=True)
            source = make_gradient_source(8, 8)
            source = np.round(source * 255.0) / 255.0
            lut = make_tinted_lut(16)
            local_output = lab.apply_lut(
                source,
                lut,
                lab.LutCandidate(quad_size=16, flip_y=False, filter_mode="bilinear", color_mode="shader"),
            )
            final_material = np.asarray(
                lab.array_to_image(local_output).resize((4, 4), Image.Resampling.BILINEAR),
                dtype=np.float32,
            ) / 255.0
            final_publish = final_material.copy()
            final_publish[:, :, 0] = np.clip(final_publish[:, :, 0] + 0.25, 0.0, 1.0)

            input_path = capture_dir / "effect_input.tga"
            local_path = capture_dir / "material_output_local_1_0.tga"
            final_path = capture_dir / "material_output_1_0.tga"
            publish_path = capture_dir / "final_publish.tga"
            Image.fromarray((source * 255.0).astype(np.uint8), "RGBA").save(input_path)
            Image.fromarray((local_output * 255.0).astype(np.uint8), "RGBA").save(local_path)
            Image.fromarray((final_material * 255.0).astype(np.uint8), "RGBA").save(final_path)
            Image.fromarray((final_publish * 255.0).astype(np.uint8), "RGBA").save(publish_path)

            source_pkg = root / "scene.pkg"
            source_pkg.write_bytes(make_pkg({"materials/sunset.tex": make_tex_rgba8(64, 64, (lut * 255.0).astype(np.uint8).tobytes())}))
            manifest_path = root / "manifest.json"
            common_layer = """{
        "layerId": 82,
        "layerName": "WALL",
        "publish": {
          "enabled": true,
          "finalBlendMode": 0,
          "fullscreen": false,
          "composelayer": false,
          "standalonePuppetFinalDisplay": false,
          "publishFinalOutput": true,
          "finalNodeUsesOriginalParent": true,
          "effectInputNodeReset": true,
          "parentId": 44,
          "hasParsedParentNode": true,
          "effectInputRenderTarget": "_rt_effect_ppong_a",
          "effectPingPongA": "_rt_effect_ppong_a",
          "effectPingPongB": "_rt_effect_ppong_b",
          "effectOutputSourceTarget": "_rt_default",
          "finalPublishRenderTarget": "_rt_default",
          "materialOutputCaptureTiming": "effect-command-copy-after-layer-node",
          "finalPublishCaptureTiming": "post-frame-render-target-dump"
        },
        "effectMaterials": [
          {
            "shader": "workshop/3165346237/effects/lut_loader",
            "resolvedCombos": {"QUAD_SIZE": "16", "LUT_FLIP_Y": "0"},
            "finalPublishedMaterial": true,
            "debugSourceFinalEffectOutput": true,
            "debugMaterialOutputSourceRenderTarget": "_rt_default",
            "materialValues": {"multiply1": [1.0]},
            "textureBindings": [
              {"slot": 0, "resolved": "_rt_effect_pingpong_a"},
              {"slot": 1, "resolved": "sunset"}
            ]
          }
        ]
      }"""
            manifest_path.write_text(
                """{
  "captures": [
    {
      "stage": "effect-input",
      "path": "%s",
      "renderTargetInfo": {"width": 8, "height": 8},
      "layer": %s
    },
    {
      "stage": "material-output-local-1-0",
      "path": "%s",
      "renderTargetInfo": {"width": 8, "height": 8},
      "layer": %s
    },
    {
      "stage": "material-output-1-0",
      "path": "%s",
      "renderTargetInfo": {"width": 4, "height": 4},
      "layer": %s
    },
    {
      "stage": "final-publish",
      "path": "%s",
      "renderTargetInfo": {"width": 4, "height": 4},
      "layer": %s
    }
  ]
}
"""
                % (input_path, common_layer, local_path, common_layer, final_path, common_layer, publish_path, common_layer),
                encoding="utf-8",
            )
            summary_path = root / "summary.json"
            summary_path.write_text(
                """{"variants": [{"name": "sunset", "effectManifest": "%s", "metrics": {"rmse": 0.12, "referenceMeanRgb": [0.8, 0.6, 0.6], "yakkaiMeanRgb": [0.7, 0.6, 0.6]}}]}""" % manifest_path,
                encoding="utf-8",
            )

            result = lab.compare_layer(summary_path, "sunset", 82, source_pkg, root / "out", 8)

            drift = result["publishDrift"]
            self.assertEqual(drift["classification"], "post-frame-composite-delta")
            self.assertEqual(drift["localStage"], "material-output-local-1-0")
            self.assertEqual(drift["finalStage"], "material-output-1-0")
            self.assertEqual(drift["publishedStage"], "final-publish")
            self.assertEqual(drift["localDimensions"], [8, 8])
            self.assertEqual(drift["finalDimensions"], [4, 4])
            self.assertEqual(drift["publishedDimensions"], [4, 4])
            self.assertLess(drift["localToFinal"]["rmse"], 0.02)
            self.assertGreater(drift["finalToPublished"]["rmse"], 0.05)
            self.assertEqual(drift["variantScreenDrift"]["rmse"], 0.12)
            self.assertEqual(drift["variantScreenDrift"]["meanRgbDelta"], [-0.1, 0.0, 0.0])
            publish = drift["publishDiagnostics"]
            self.assertEqual(publish["finalPublishCaptureTiming"], "post-frame-render-target-dump")
            self.assertEqual(publish["materialOutputCaptureTiming"], "effect-command-copy-after-layer-node")
            self.assertEqual(publish["effectOutputSourceTarget"], "_rt_default")
            self.assertEqual(publish["parentId"], 44)
            self.assertTrue(publish["finalPublishedMaterial"])
            self.assertTrue(publish["debugSourceFinalEffectOutput"])

    def test_compare_layer_registers_local_material_crop_to_final_publish(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_dir = root / "captures" / "3228578419" / "82_WALL"
            capture_dir.mkdir(parents=True)
            source = make_gradient_source(16, 12)
            source = np.round(source * 255.0) / 255.0
            lut = make_identity_lut(16)
            local_output = lab.apply_lut(
                source,
                lut,
                lab.LutCandidate(quad_size=16, flip_y=False, filter_mode="bilinear", color_mode="shader"),
            )
            final_material = local_output[2:10, 4:12, :]

            input_path = capture_dir / "effect_input.tga"
            local_path = capture_dir / "material_output_local_1_0.tga"
            final_path = capture_dir / "material_output_1_0.tga"
            Image.fromarray((source * 255.0).astype(np.uint8), "RGBA").save(input_path)
            Image.fromarray((local_output * 255.0).astype(np.uint8), "RGBA").save(local_path)
            Image.fromarray((final_material * 255.0).astype(np.uint8), "RGBA").save(final_path)

            source_pkg = root / "scene.pkg"
            source_pkg.write_bytes(make_pkg({"materials/identity.tex": make_tex_rgba8(64, 64, (lut * 255.0).astype(np.uint8).tobytes())}))
            manifest_path = root / "manifest.json"
            common_layer = """{
        "layerId": 82,
        "layerName": "WALL",
        "effectMaterials": [
          {
            "shader": "workshop/3165346237/effects/lut_loader",
            "resolvedCombos": {"QUAD_SIZE": "16", "LUT_FLIP_Y": "0"},
            "materialValues": {"multiply1": [1.0]},
            "textureBindings": [
              {"slot": 0, "resolved": "_rt_effect_pingpong_a"},
              {"slot": 1, "resolved": "identity"}
            ]
          }
        ]
      }"""
            manifest_path.write_text(
                """{
  "captures": [
    {
      "stage": "effect-input",
      "path": "%s",
      "renderTargetInfo": {"width": 16, "height": 12},
      "layer": %s
    },
    {
      "stage": "material-output-local-1-0",
      "path": "%s",
      "renderTargetInfo": {"width": 16, "height": 12},
      "layer": %s
    },
    {
      "stage": "material-output-1-0",
      "path": "%s",
      "renderTargetInfo": {"width": 8, "height": 8},
      "layer": %s
    }
  ]
}
"""
                % (input_path, common_layer, local_path, common_layer, final_path, common_layer),
                encoding="utf-8",
            )
            summary_path = root / "summary.json"
            summary_path.write_text(
                """{"variants": [{"name": "sunset", "effectManifest": "%s"}]}""" % manifest_path,
                encoding="utf-8",
            )

            result = lab.compare_layer(summary_path, "sunset", 82, source_pkg, root / "out", 16)

            drift = result["publishDrift"]
            registered = drift["registeredLocalToFinal"]
            self.assertEqual(registered["cropPixels"], [4, 2, 8, 8])
            self.assertLess(registered["rmse"], 0.02)
            self.assertLess(registered["rmse"], drift["localToFinal"]["rmse"] * 0.25)
            self.assertGreater(registered["improvement"], 0.05)

    def test_compare_variant_lut_layers_reports_each_material_output_layer(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source_pkg = root / "scene.pkg"
            lut = make_tinted_lut(16)
            source_pkg.write_bytes(make_pkg({"materials/sunset.tex": make_tex_rgba8(64, 64, (lut * 255.0).astype(np.uint8).tobytes())}))

            manifest_captures = []
            for layer_id, layer_name, alpha in ((82, "WALL", 1.0), (174, "CHAIR", 0.05)):
                capture_dir = root / "captures" / "3228578419" / f"{layer_id}_{layer_name}"
                capture_dir.mkdir(parents=True)
                source = make_gradient_source(8, 8)
                source = np.round(source * 255.0) / 255.0
                source[..., 3] = alpha
                material_output = lab.apply_lut(
                    source,
                    lut,
                    lab.LutCandidate(quad_size=16, flip_y=False, filter_mode="bilinear", color_mode="shader"),
                )
                input_path = capture_dir / "effect_input.tga"
                material_path = capture_dir / "material_output_1_0.tga"
                Image.fromarray((source * 255.0).astype(np.uint8), "RGBA").save(input_path)
                Image.fromarray((material_output * 255.0).astype(np.uint8), "RGBA").save(material_path)
                layer_json = """{
        "layerId": %d,
        "layerName": "%s",
        "policy": {"strippedEffects": false},
        "effectMaterials": [
          {
            "shader": "workshop/3165346237/effects/lut_loader",
            "resolvedCombos": {"QUAD_SIZE": "16", "LUT_FLIP_Y": "0", "BLENDMODE": "0", "CLAMP": "1"},
            "materialValues": {"multiply1": [1.0], "tc": [0.0]},
            "textureBindings": [
              {"slot": 0, "resolved": "_rt_effect_pingpong_a"},
              {"slot": 1, "resolved": "sunset"}
            ]
          }
        ]
      }""" % (layer_id, layer_name)
                manifest_captures.append(
                    """{
      "stage": "effect-input",
      "path": "%s",
      "renderTargetInfo": {"width": 8, "height": 8},
      "layer": %s
    },
    {
      "stage": "material-output-1-0",
      "path": "%s",
      "renderTargetInfo": {"width": 8, "height": 8},
      "layer": %s
    }""" % (input_path, layer_json, material_path, layer_json)
                )

            manifest_path = root / "manifest.json"
            manifest_path.write_text(
                """{"captures": [%s]}""" % ",".join(manifest_captures),
                encoding="utf-8",
            )
            summary_path = root / "summary.json"
            summary_path.write_text(
                """{"variants": [{"name": "sunset", "effectManifest": "%s"}]}""" % manifest_path,
                encoding="utf-8",
            )

            result = lab.compare_variant_lut_layers(summary_path, "sunset", source_pkg, root / "out", 8)

            self.assertEqual(result["variant"], "sunset")
            self.assertEqual(result["layerCount"], 2)
            self.assertEqual(result["trustedLayerCount"], 1)
            self.assertEqual(result["lowVisibilityLayerCount"], 1)
            self.assertEqual([layer["layerId"] for layer in result["layers"]], [82, 174])
            self.assertEqual(result["layers"][0]["outputStage"], "material-output-1-0")
            self.assertTrue(result["layers"][0]["trustedVisible"])
            self.assertFalse(result["layers"][1]["trustedVisible"])
            self.assertEqual(result["layers"][0]["manifestCandidate"]["rank"], 1)
            self.assertIn("trustedBestCandidateCounts", result)
            self.assertIn("quad=16 flipY=False filter=bilinear color=shader", result["bestCandidateCounts"])
            self.assertTrue((root / "out" / "lut-sampling-summary.md").is_file())

    def test_compare_variant_lut_layers_demotes_dimension_mismatched_outputs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source_pkg = root / "scene.pkg"
            lut = make_tinted_lut(16)
            source_pkg.write_bytes(make_pkg({"materials/sunset.tex": make_tex_rgba8(64, 64, (lut * 255.0).astype(np.uint8).tobytes())}))

            capture_dir = root / "captures" / "3228578419" / "82_WALL"
            capture_dir.mkdir(parents=True)
            source = make_gradient_source(8, 8)
            source = np.round(source * 255.0) / 255.0
            material_output = lab.apply_lut(
                source,
                lut,
                lab.LutCandidate(quad_size=16, flip_y=False, filter_mode="bilinear", color_mode="shader"),
            )
            input_path = capture_dir / "effect_input.tga"
            material_path = capture_dir / "material_output_1_0.tga"
            Image.fromarray((source * 255.0).astype(np.uint8), "RGBA").save(input_path)
            Image.fromarray((material_output * 255.0).astype(np.uint8), "RGBA").resize((16, 8)).save(material_path)

            layer_json = """{
        "layerId": 82,
        "layerName": "WALL",
        "policy": {"strippedEffects": false},
        "effectMaterials": [
          {
            "shader": "workshop/3165346237/effects/lut_loader",
            "resolvedCombos": {"QUAD_SIZE": "16", "LUT_FLIP_Y": "0", "BLENDMODE": "0", "CLAMP": "1"},
            "materialValues": {"multiply1": [1.0], "tc": [0.0]},
            "textureBindings": [
              {"slot": 0, "resolved": "_rt_effect_pingpong_a"},
              {"slot": 1, "resolved": "sunset"}
            ]
          }
        ]
      }"""
            manifest_path = root / "manifest.json"
            manifest_path.write_text(
                """{
  "captures": [
    {
      "stage": "effect-input",
      "path": "%s",
      "renderTargetInfo": {"width": 8, "height": 8},
      "layer": %s
    },
    {
      "stage": "material-output-1-0",
      "path": "%s",
      "renderTargetInfo": {"width": 16, "height": 8},
      "layer": %s
    }
  ]
}
"""
                % (input_path, layer_json, material_path, layer_json),
                encoding="utf-8",
            )
            summary_path = root / "summary.json"
            summary_path.write_text(
                """{"variants": [{"name": "sunset", "effectManifest": "%s"}]}""" % manifest_path,
                encoding="utf-8",
            )

            result = lab.compare_variant_lut_layers(summary_path, "sunset", source_pkg, root / "out", 16)

            self.assertEqual(result["trustedLayerCount"], 0)
            self.assertEqual(result["dimensionMismatchLayerCount"], 1)
            self.assertEqual(result["lowVisibilityLayerCount"], 0)
            self.assertTrue(result["layers"][0]["trustedVisible"])
            self.assertFalse(result["layers"][0]["trustedComparison"])
            self.assertFalse(result["layers"][0]["visibility"]["dimensionCompatible"])
            self.assertEqual(result["trustedBestCandidateCounts"], {})


class DefaultFrameProgressionTests(unittest.TestCase):
    def test_progression_orders_screen_sized_material_outputs_by_capture_index(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_dir = root / "captures"
            capture_dir.mkdir(parents=True)

            def save_frame(name: str, rgb_value: float) -> Path:
                pixels = np.full((4, 4, 4), rgb_value, dtype=np.float32)
                pixels[..., 3] = 1.0
                path = capture_dir / name
                Image.fromarray((pixels * 255.0).astype(np.uint8), "RGBA").save(path)
                return path

            reference_path = save_frame("reference.png", 0.0)
            final_path = save_frame("yakkai.png", 0.0)
            early_path = save_frame("material_output_early.tga", 0.75)
            late_path = save_frame("material_output_late.tga", 0.10)

            def capture(layer_id: int, layer_name: str, capture_index: int, path: Path) -> dict[str, object]:
                return {
                    "captureIndex": capture_index,
                    "stage": "material-output-1-0",
                    "path": str(path),
                    "renderTarget": "_rt_debug_material_output",
                    "renderTargetInfo": {"width": 4, "height": 4},
                    "layer": {
                        "layerId": layer_id,
                        "layerName": layer_name,
                        "publish": {
                            "enabled": True,
                            "effectOutputSourceTarget": "_rt_default",
                            "finalPublishRenderTarget": "_rt_default",
                            "materialOutputCaptureTiming": "effect-command-copy-after-layer-node",
                            "finalPublishCaptureTiming": "post-frame-render-target-dump",
                        },
                        "effectMaterials": [
                            {
                                "shader": "workshop/3165346237/effects/lut_loader",
                                "debugMaterialOutputSourceRenderTarget": "_rt_default",
                                "finalPublishedMaterial": True,
                                "resolvedCombos": {"QUAD_SIZE": "16"},
                                "textureBindings": [{"slot": 1, "resolved": "sunset"}],
                            }
                        ],
                    },
                }

            manifest_path = root / "manifest.json"
            manifest_path.write_text(
                json.dumps(
                    {
                        "captures": [
                            capture(200, "Late LUT", 7, late_path),
                            capture(100, "Early LUT", 3, early_path),
                        ]
                    }
                ),
                encoding="utf-8",
            )
            summary_path = root / "summary.json"
            summary_path.write_text(
                json.dumps(
                    {
                        "variants": [
                            {
                                "name": "sunset",
                                "effectManifest": str(manifest_path),
                                "normalizedReference": str(reference_path),
                                "yakkai": str(final_path),
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )

            result = lab.compare_default_frame_progression(summary_path, "sunset", root / "out", 8)

            self.assertEqual(result["snapshotCount"], 2)
            self.assertEqual([snapshot["layerId"] for snapshot in result["snapshots"]], [100, 200])
            self.assertGreater(result["snapshots"][0]["referenceRmse"], result["snapshots"][1]["referenceRmse"])
            self.assertLess(result["snapshots"][1]["referenceRmseDeltaFromPrevious"], 0.0)
            self.assertGreater(result["snapshots"][1]["deltaFromPreviousRmse"], 0.2)
            self.assertEqual(result["largestReferenceImprovements"][0]["toLayerId"], 200)
            self.assertTrue((root / "out" / "default-frame-progression.json").is_file())
            self.assertTrue((root / "out" / "default-frame-progression.md").is_file())


class PostLutDriftAttributionTests(unittest.TestCase):
    def test_report_attributes_final_frame_drift_after_last_lut_snapshot(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_dir = root / "captures"
            capture_dir.mkdir(parents=True)

            def save_frame(name: str, rgb_value: float, bottom_rgb: float | None = None) -> Path:
                pixels = np.full((6, 6, 4), rgb_value, dtype=np.float32)
                if bottom_rgb is not None:
                    pixels[4:, :, :3] = bottom_rgb
                pixels[..., 3] = 1.0
                path = capture_dir / name
                Image.fromarray((pixels * 255.0).astype(np.uint8), "RGBA").save(path)
                return path

            reference_path = save_frame("reference.png", 0.0)
            lut_path = save_frame("material_output_lut.tga", 0.10)
            final_path = save_frame("yakkai.png", 0.35, bottom_rgb=0.80)
            later_capture_path = save_frame("final_publish.tga", 0.50)

            def layer(stage: str, layer_id: int, layer_name: str, capture_index: int, path: Path) -> dict[str, object]:
                return {
                    "captureIndex": capture_index,
                    "stage": stage,
                    "path": str(path),
                    "renderTarget": "_rt_default",
                    "renderTargetInfo": {"width": 6, "height": 6},
                    "layer": {
                        "layerId": layer_id,
                        "layerName": layer_name,
                        "publish": {
                            "enabled": True,
                            "effectOutputSourceTarget": "_rt_default",
                            "finalPublishRenderTarget": "_rt_default",
                            "materialOutputCaptureTiming": "effect-command-copy-after-layer-node",
                            "finalPublishCaptureTiming": "post-frame-render-target-dump",
                        },
                        "effectMaterials": [
                            {
                                "shader": "workshop/3165346237/effects/lut_loader",
                                "debugMaterialOutputSourceRenderTarget": "_rt_default",
                                "finalPublishedMaterial": True,
                                "textureBindings": [{"slot": 1, "resolved": "night"}],
                            }
                        ],
                    },
                }

            manifest_path = root / "manifest.json"
            manifest_path.write_text(
                json.dumps(
                    {
                        "protectedPuppetDiagnostics": [
                            {
                                "diagnosticKind": "protected-puppet-chain",
                                "captureMode": "metadata-only",
                                "layerId": 405,
                                "layerName": "ARONA_CROP_SHEET",
                                "alpha": 0.84,
                                "effectOrder": ["lut", "pulse", "shake"],
                                "materialShaders": [
                                    "effects/lut_loader",
                                    "effects/pulse",
                                    "effects/shake",
                                ],
                                "candidateChainShape": "protected-puppet-mixed",
                                "candidateEffectClass": "protected-puppet-lut",
                                "candidateRisk": "protected-puppet-path",
                                "finalPublishRenderTarget": "_rt_default",
                                "alphaEvidence": {"layerAlpha": 0.84},
                            }
                        ],
                        "captures": [
                            layer("material-output-1-0", 82, "WALL", 4, lut_path),
                            layer("final-publish", 82, "WALL", 5, later_capture_path),
                            {
                                "captureIndex": 6,
                                "stage": "effect-input",
                                "path": str(later_capture_path),
                                "renderTarget": "_rt_effect_input",
                                "renderTargetInfo": {"width": 6, "height": 6},
                                "layer": {"layerId": 405, "layerName": "ARONA_CROP_SHEET"},
                            },
                        ]
                    }
                ),
                encoding="utf-8",
            )
            summary_path = root / "summary.json"
            summary_path.write_text(
                json.dumps(
                    {
                        "variants": [
                            {
                                "name": "night",
                                "effectManifest": str(manifest_path),
                                "normalizedReference": str(reference_path),
                                "yakkai": str(final_path),
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )

            result = lab.compare_post_lut_drift(summary_path, "night", root / "out", 6)

            self.assertEqual(result["classification"], "final-frame-regressed-after-lut")
            self.assertEqual(result["lastLutSnapshot"]["layerId"], 82)
            self.assertGreater(result["downstreamReferenceRmseDelta"], 0.2)
            self.assertGreater(result["lastLutToFinal"]["rmse"], 0.2)
            self.assertEqual([capture["captureIndex"] for capture in result["postLutCaptures"]], [5, 6])
            self.assertEqual(result["fullFrameAttribution"]["classification"], "active-post-lut-full-frame-transition")
            self.assertEqual(result["fullFrameAttribution"]["activeCaptureCount"], 1)
            self.assertEqual(result["fullFrameAttribution"]["disabledCaptureCount"], 0)
            self.assertEqual(result["fullFrameAttribution"]["firstActiveStep"]["layerId"], 82)
            self.assertEqual(result["fullFrameAttribution"]["firstActiveStep"]["stage"], "final-publish")
            self.assertGreater(result["fullFrameAttribution"]["firstActiveStep"]["previousRmse"], 0.2)
            protected = result["protectedPuppetDiagnostics"]
            self.assertEqual(protected["classification"], "protected-puppet-diagnostics-present")
            self.assertEqual(protected["count"], 1)
            self.assertEqual(protected["chainShapeCounts"], {"protected-puppet-mixed": 1})
            self.assertEqual(protected["layers"][0]["layerName"], "ARONA_CROP_SHEET")
            self.assertEqual(protected["layers"][0]["class"], "protected-puppet-lut")
            self.assertEqual(protected["layers"][0]["alpha"], 0.84)
            self.assertEqual(protected["layers"][0]["effectOrder"], ["lut", "pulse", "shake"])
            self.assertEqual(protected["layers"][0]["finalPublishRenderTarget"], "_rt_default")
            markdown = (root / "out" / "post-lut-drift.md").read_text(encoding="utf-8")
            self.assertIn("## Protected Puppet Diagnostics", markdown)
            self.assertIn("ARONA_CROP_SHEET", markdown)
            bottom = next(region for region in result["regionDrift"] if region["region"] == "bottom")
            center = next(region for region in result["regionDrift"] if region["region"] == "center")
            self.assertGreater(bottom["downstreamReferenceRmseDelta"], center["downstreamReferenceRmseDelta"])
            self.assertTrue((root / "out" / "post-lut-drift.json").is_file())
            self.assertTrue((root / "out" / "post-lut-drift.md").is_file())

    def test_report_ranks_default_rt_boundary_transition(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_dir = root / "captures"
            capture_dir.mkdir(parents=True)

            def save_frame(name: str, rgb_value: float, top_rgb: float | None = None) -> Path:
                pixels = np.full((6, 6, 4), rgb_value, dtype=np.float32)
                if top_rgb is not None:
                    pixels[:2, :, :3] = top_rgb
                pixels[..., 3] = 1.0
                path = capture_dir / name
                Image.fromarray((pixels * 255.0).astype(np.uint8), "RGBA").save(path)
                return path

            reference_path = save_frame("reference.png", 0.0)
            lut_path = save_frame("material_output_lut.tga", 0.05)
            boundary_before_path = save_frame("default_before_effect.tga", 0.35, top_rgb=0.80)
            boundary_after_path = save_frame("default_after_effect.tga", 0.36, top_rgb=0.80)
            final_path = boundary_after_path

            def capture(stage: str, layer_id: int, layer_name: str, capture_index: int, path: Path) -> dict[str, object]:
                return {
                    "captureIndex": capture_index,
                    "stage": stage,
                    "path": str(path),
                    "renderTarget": "_rt_debug_default_boundary",
                    "renderTargetInfo": {"width": 6, "height": 6},
                    "layer": {
                        "layerId": layer_id,
                        "layerName": layer_name,
                        "alpha": 0.0 if layer_id == 469 else 1.0,
                        "publish": {
                            "enabled": True,
                            "effectOutputSourceTarget": "_rt_default",
                            "finalPublishRenderTarget": "_rt_default",
                            "defaultRtBoundaryCaptureTiming": "effect-command-copy-around-effect-layer",
                        },
                        "effectMaterials": [
                            {
                                "shader": "workshop/3165346237/effects/lut_loader",
                                "debugMaterialOutputSourceRenderTarget": "_rt_default",
                                "finalPublishedMaterial": True,
                                "resolvedConstValues": {"g_UserAlpha": [0.0 if layer_id == 469 else 1.0]},
                            }
                        ],
                    },
                }

            manifest_path = root / "manifest.json"
            manifest_path.write_text(
                json.dumps(
                    {
                        "captures": [
                            capture("material-output-1-0", 2143, "Arm_fix", 4, lut_path),
                            capture("default-before-effect", 469, "1st flare", 5, boundary_before_path),
                            capture("default-after-effect", 469, "1st flare", 6, boundary_after_path),
                        ]
                    }
                ),
                encoding="utf-8",
            )
            summary_path = root / "summary.json"
            summary_path.write_text(
                json.dumps(
                    {
                        "variants": [
                            {
                                "name": "night",
                                "effectManifest": str(manifest_path),
                                "normalizedReference": str(reference_path),
                                "yakkai": str(final_path),
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )

            result = lab.compare_post_lut_drift(summary_path, "night", root / "out", 6)

            boundary = result["defaultRtBoundaryAttribution"]
            self.assertEqual(boundary["classification"], "default-rt-boundary-transition-detected")
            self.assertEqual(boundary["boundaryCaptureCount"], 2)
            self.assertEqual(boundary["firstTransition"]["stage"], "default-before-effect")
            self.assertEqual(boundary["firstTransition"]["layerId"], 469)
            self.assertGreater(boundary["firstTransition"]["previousRmse"], 0.2)
            self.assertGreater(boundary["largestTransitions"][0]["referenceRmseDeltaFromPrevious"], 0.2)


class PostLutFlareDriftTests(unittest.TestCase):
    def test_report_ranks_first_flare_step_and_regions(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_dir = root / "captures"
            capture_dir.mkdir(parents=True)

            def save_frame(name: str, base_rgb: float, top_rgb: float | None = None, right_rgb: float | None = None) -> Path:
                pixels = np.full((6, 6, 4), base_rgb, dtype=np.float32)
                if top_rgb is not None:
                    pixels[:2, :, :3] = top_rgb
                if right_rgb is not None:
                    pixels[:, 4:, :3] = right_rgb
                pixels[..., 3] = 1.0
                path = capture_dir / name
                Image.fromarray((pixels * 255.0).astype(np.uint8), "RGBA").save(path)
                return path

            reference_path = save_frame("reference.png", 0.0)
            lut_path = save_frame("material_output_lut.tga", 0.05)
            first_flare_path = save_frame("first_flare.tga", 0.05, top_rgb=0.80, right_rgb=0.75)
            second_flare_path = save_frame("second_flare.tga", 0.05, top_rgb=0.82, right_rgb=0.76)
            pulse_path = save_frame("pulse_hash.tga", 0.05, top_rgb=0.82, right_rgb=0.78)
            final_path = pulse_path

            def capture(stage: str, layer_id: int, layer_name: str, capture_index: int, path: Path) -> dict[str, object]:
                return {
                    "captureIndex": capture_index,
                    "stage": stage,
                    "path": str(path),
                    "renderTarget": "_rt_default",
                    "renderTargetInfo": {"width": 6, "height": 6},
                    "layer": {
                        "layerId": layer_id,
                        "layerName": layer_name,
                        "publish": {
                            "enabled": True,
                            "effectOutputSourceTarget": "_rt_default",
                            "finalPublishRenderTarget": "_rt_default",
                            "finalPublishCaptureTiming": "post-frame-render-target-dump",
                        },
                        "effectMaterials": [
                            {
                                "shader": "workshop/3165346237/effects/lut_loader",
                                "debugMaterialOutputSourceRenderTarget": "_rt_default",
                                "finalPublishedMaterial": True,
                                "textureBindings": [{"slot": 1, "resolved": "night"}],
                            }
                        ],
                    },
                }

            manifest_path = root / "manifest.json"
            manifest_path.write_text(
                json.dumps(
                    {
                        "captures": [
                            capture("material-output-1-0", 2143, "Arm_fix", 4, lut_path),
                            capture("effect-output", 469, "1st flare", 5, first_flare_path),
                            capture("final-publish", 469, "1st flare", 6, first_flare_path),
                            capture("effect-output", 472, "2nd lens", 7, second_flare_path),
                            capture("final-publish", 472, "2nd lens", 8, second_flare_path),
                            capture("effect-output", 482, "c7884e6807cf62bb85f8d8b67942cec4", 9, pulse_path),
                            capture("final-publish", 482, "c7884e6807cf62bb85f8d8b67942cec4", 10, pulse_path),
                        ]
                    }
                ),
                encoding="utf-8",
            )
            summary_path = root / "summary.json"
            summary_path.write_text(
                json.dumps(
                    {
                        "variants": [
                            {
                                "name": "night",
                                "effectManifest": str(manifest_path),
                                "normalizedReference": str(reference_path),
                                "yakkai": str(final_path),
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )

            result = lab.compare_post_lut_flare_drift(summary_path, "night", root / "out", 6)

            self.assertEqual(result["classification"], "shared-flare-default-frame-drift")
            self.assertEqual(result["flareLayerCount"], 3)
            self.assertEqual(result["largestFlareSteps"][0]["layerId"], 469)
            self.assertGreater(result["largestFlareSteps"][0]["referenceRmseDeltaFromPrevious"], 0.2)
            self.assertLess(result["largestFlareSteps"][1]["previousRmse"], 0.05)
            self.assertEqual([layer["layerId"] for layer in result["flareLayers"]], [469, 472, 482])
            self.assertEqual(result["flareLayers"][2]["layerKind"], "post-lut-effect")
            first_regions = result["largestFlareSteps"][0]["regionDeltas"]
            self.assertIn(first_regions[0]["region"], {"top", "right"})
            self.assertGreater(first_regions[0]["referenceRmseDeltaFromPrevious"], 0.2)
            self.assertTrue((root / "out" / "post-lut-flare-drift.json").is_file())
            self.assertTrue((root / "out" / "post-lut-flare-drift.md").is_file())

    def test_report_excludes_zero_alpha_disabled_flare_steps(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_dir = root / "captures"
            capture_dir.mkdir(parents=True)

            def save_frame(name: str, base_rgb: float, top_rgb: float | None = None) -> Path:
                pixels = np.full((6, 6, 4), base_rgb, dtype=np.float32)
                if top_rgb is not None:
                    pixels[:2, :, :3] = top_rgb
                pixels[..., 3] = 1.0
                path = capture_dir / name
                Image.fromarray((pixels * 255.0).astype(np.uint8), "RGBA").save(path)
                return path

            reference_path = save_frame("reference.png", 0.0)
            lut_path = save_frame("material_output_lut.tga", 0.05)
            disabled_flare_path = save_frame("disabled_flare.tga", 0.05, top_rgb=0.80)
            final_path = disabled_flare_path

            def capture(stage: str, capture_index: int, path: Path) -> dict[str, object]:
                return {
                    "captureIndex": capture_index,
                    "stage": stage,
                    "path": str(path),
                    "renderTarget": "_rt_default",
                    "renderTargetInfo": {"width": 6, "height": 6},
                    "layer": {
                        "alpha": 0.0,
                        "layerId": 469,
                        "layerName": "1st flare",
                        "publish": {
                            "enabled": True,
                            "effectOutputSourceTarget": "_rt_default",
                            "finalPublishRenderTarget": "_rt_default",
                            "finalPublishCaptureTiming": "post-frame-render-target-dump",
                        },
                        "effectMaterials": [
                            {
                                "shader": "effects/pulse",
                                "resolvedConstValues": {"g_UserAlpha": [0.0]},
                            }
                        ],
                    },
                }

            manifest_path = root / "manifest.json"
            manifest_path.write_text(
                json.dumps(
                    {
                        "captures": [
                            {
                                **capture("material-output-1-0", 4, lut_path),
                                "layer": {
                                    **capture("material-output-1-0", 4, lut_path)["layer"],
                                    "alpha": 1.0,
                                    "layerId": 2143,
                                    "layerName": "Arm_fix",
                                    "effectMaterials": [
                                        {
                                            "shader": "workshop/3165346237/effects/lut_loader",
                                            "debugMaterialOutputSourceRenderTarget": "_rt_default",
                                            "finalPublishedMaterial": True,
                                        }
                                    ],
                                },
                            },
                            capture("effect-output", 5, disabled_flare_path),
                            capture("final-publish", 6, disabled_flare_path),
                        ]
                    }
                ),
                encoding="utf-8",
            )
            summary_path = root / "summary.json"
            summary_path.write_text(
                json.dumps(
                    {
                        "variants": [
                            {
                                "name": "night",
                                "effectManifest": str(manifest_path),
                                "normalizedReference": str(reference_path),
                                "yakkai": str(final_path),
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )

            result = lab.compare_post_lut_flare_drift(summary_path, "night", root / "out", 6)

            self.assertEqual(result["classification"], "no-active-post-lut-flare-captures")
            self.assertEqual(result["activeFlareCaptureCount"], 0)
            self.assertEqual(result["disabledFlareCaptureCount"], 2)
            self.assertEqual(result["largestFlareSteps"], [])
            self.assertEqual(result["disabledFlareCaptures"][0]["disabledReason"], "zero-alpha-layer-and-material")


if __name__ == "__main__":
    unittest.main()
