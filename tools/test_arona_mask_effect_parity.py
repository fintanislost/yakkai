#!/usr/bin/env python3
from __future__ import annotations

import struct
import unittest

import numpy as np

from arona_mask_effect_parity import (
    build_report,
    classify_mismatch,
    decode_tex_image,
    effect_slot_displacement_estimate,
    layer_local_bounds_to_pixels,
    mask_region_stats,
    safe_texture_name,
    select_layer_materials,
    texture_package_path,
    texture_names_for_materials,
)


def version(name: str, number: int) -> bytes:
    return f"{name}{number:04d}".encode("ascii") + b"\0"


def make_tex(format_id: int, width: int, height: int, payload: bytes, flags: int = 2) -> bytes:
    tex = bytearray()
    tex += version("TEXV", 5)
    tex += version("TEXI", 1)
    tex += struct.pack("<iIiiiii", format_id, flags, width, height, width, height, 0)
    tex += version("TEXB", 3)
    tex += struct.pack("<i", 1)
    tex += struct.pack("<i", -1)
    tex += struct.pack("<i", 1)
    tex += struct.pack("<ii", width, height)
    tex += struct.pack("<i", 0)
    tex += struct.pack("<i", 0)
    tex += struct.pack("<i", len(payload))
    tex += payload
    return bytes(tex)


class AronaMaskEffectParityTests(unittest.TestCase):
    def test_layer_local_bounds_to_pixels_maps_centered_coordinates(self):
        bounds = [777.0, -1403.5, 1899.0, -266.5]

        pixels = layer_local_bounds_to_pixels(bounds, (4160, 2923))

        self.assertEqual(pixels, (2857, 58, 3979, 1195))

    def test_select_layer_materials_returns_requested_shaders_for_layer(self):
        manifest = {
            "captures": [{
                "stage": "effect-input",
                "layer": {
                    "layerId": 405,
                    "effectMaterials": [
                        {"effectIndex": 1, "shader": "effects/pulse", "textureBindings": []},
                        {
                            "effectIndex": 3,
                            "shader": "effects/waterwaves",
                            "textureBindings": [{"slot": 1, "resolved": "masks/waterwaves_mask_a"}],
                        },
                        {
                            "effectIndex": 8,
                            "shader": "effects/shake",
                            "textureBindings": [{"slot": 1, "resolved": "masks/shake_mask_b"}],
                        },
                    ],
                },
            }]
        }

        materials = select_layer_materials(
            manifest,
            layer_id=405,
            shaders={"effects/waterwaves", "effects/shake"},
        )

        self.assertEqual([material["shader"] for material in materials], ["effects/waterwaves", "effects/shake"])

    def test_texture_names_for_materials_uses_mask_slots_once(self):
        materials = [
            {
                "textureBindings": [
                    {"slot": 0, "resolved": "_rt_effect_pingpong_a"},
                    {"slot": 1, "resolved": "masks/waterwaves_mask_a"},
                    {"slot": 2, "resolved": "masks/pulse_mask_c"},
                ]
            },
            {
                "textureBindings": [
                    {"slot": 1, "resolved": "masks/waterwaves_mask_a"},
                    {"slot": 1, "resolved": "masks/shake_mask_b"},
                ]
            },
        ]

        self.assertEqual(
            texture_names_for_materials(materials),
            ["masks/waterwaves_mask_a", "masks/pulse_mask_c", "masks/shake_mask_b"],
        )

    def test_classify_mismatch_prefers_shader_math_when_masks_align_but_crop_differs(self):
        result = classify_mismatch(mask_iou=0.82, constant_delta=0.0, time_delta=0.0, crop_rmse=0.09)

        self.assertEqual(result, "shader-math")

    def test_classify_mismatch_flags_mask_coordinate_when_mask_iou_is_low(self):
        result = classify_mismatch(mask_iou=0.24, constant_delta=0.0, time_delta=0.0, crop_rmse=0.09)

        self.assertEqual(result, "mask-coordinate")

    def test_texture_package_path_resolves_material_texture(self):
        self.assertEqual(
            texture_package_path("masks/waterwaves_mask_a"),
            "materials/masks/waterwaves_mask_a.tex",
        )

    def test_safe_texture_name_is_filesystem_friendly(self):
        self.assertEqual(safe_texture_name("masks/waterwaves_mask_a"), "masks__waterwaves_mask_a")

    def test_decode_tex_image_supports_r8_masks_as_grayscale_rgba(self):
        tex = make_tex(9, 2, 1, bytes([0, 255]))

        image = decode_tex_image(tex)

        self.assertEqual(image.width, 2)
        self.assertEqual(image.height, 1)
        self.assertEqual(image.format_id, 9)
        np.testing.assert_allclose(
            image.pixels,
            np.array([[[0.0, 0.0, 0.0, 1.0], [1.0, 1.0, 1.0, 1.0]]], dtype=np.float32),
        )

    def test_mask_region_stats_maps_source_region_into_mask_space(self):
        mask = np.zeros((2, 4, 4), dtype=np.float32)
        mask[:, 2:, 0] = 1.0
        mask[:, 2:, 1] = 0.5
        mask[:, 2:, 3] = 1.0

        stats = mask_region_stats(mask, source_size=(8, 4), region_pixels=(4, 0, 8, 4))

        self.assertEqual(stats["maskPixels"], [2, 0, 4, 2])
        self.assertAlmostEqual(stats["nonzeroFraction"], 1.0)
        self.assertAlmostEqual(stats["channelMean"][0], 1.0)
        self.assertAlmostEqual(stats["channelMean"][1], 0.5)
        self.assertAlmostEqual(stats["channelMean"][2], 0.0)

    def test_effect_slot_displacement_estimate_handles_waterwaves_strength_squared(self):
        material = {
            "shader": "effects/waterwaves",
            "materialValues": {"strength": [0.1]},
        }
        slot_stats = {"channelMax": [0.5, 0.5, 0.5], "channelMean": [0.25, 0.25, 0.25]}

        estimate = effect_slot_displacement_estimate(material, slot_stats, (1000, 500))

        self.assertEqual(estimate["kind"], "waterwaves")
        self.assertAlmostEqual(estimate["maxUv"], 0.005)
        self.assertAlmostEqual(estimate["maxPixels"], 5.0)

    def test_effect_slot_displacement_estimate_handles_shake_flow_mask(self):
        material = {
            "shader": "effects/shake",
            "materialValues": {"strength": [0.1]},
        }
        slot_stats = {"channelMax": [1.0, 1.0, 0.0], "channelMean": [0.248, 0.748, 0.0]}

        estimate = effect_slot_displacement_estimate(material, slot_stats, (1000, 500))

        self.assertEqual(estimate["kind"], "shake")
        self.assertEqual(estimate["meanFlow"], [-0.5, 0.5])
        self.assertEqual(estimate["peakPixels"], [5.0, 2.5])

    def test_build_report_includes_slots_materials_and_masks(self):
        manifest = {
            "captures": [{
                "stage": "effect-input",
                "layer": {
                    "layerId": 405,
                    "effectMaterials": [
                        {
                            "effectIndex": 3,
                            "shader": "effects/waterwaves",
                            "materialValues": {"strength": [0.088]},
                            "textureBindings": [{"slot": 1, "resolved": "masks/waterwaves_mask_a"}],
                        },
                    ],
                },
            }]
        }
        slot_bounds = {
            "3": [777.0, -1403.5, 1899.0, -266.5],
            "13": [718.0, -794.5, 2018.0, -266.5],
        }

        report = build_report(manifest, layer_id=405, slot_bounds=slot_bounds, image_size=(4160, 2923))

        self.assertEqual(report["layerId"], 405)
        self.assertEqual(report["materials"][0]["shader"], "effects/waterwaves")
        self.assertEqual(report["maskTextures"], ["masks/waterwaves_mask_a"])
        self.assertEqual(report["slots"]["3"]["pixelBounds"], [2857, 58, 3979, 1195])


if __name__ == "__main__":
    unittest.main()
