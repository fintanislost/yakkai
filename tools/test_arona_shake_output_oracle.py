#!/usr/bin/env python3
from __future__ import annotations

import unittest

import numpy as np

from arona_shake_output_oracle import (
    apply_shake_pass,
    capture_time_seconds,
    capture_path_for_stage,
    flow_mask_from_rg,
    offset_sweep,
    preserve_source_alpha,
    sample_bilinear_rgba,
    select_effect_material,
    slot_union_pixels,
    shake_offset_at_time,
    shake_texcoord_offset,
)


class AronaShakeOutputOracleTests(unittest.TestCase):
    def test_shake_flow_mask_centers_rg8_around_0498(self):
        flow = flow_mask_from_rg(np.array([[[0.498, 0.748]]], dtype=np.float32))

        np.testing.assert_allclose(flow, [[[-0.0, 0.5]]], atol=1e-6)

    def test_shake_offset_uses_amp_squared(self):
        offset = shake_texcoord_offset(
            offset=1.0,
            amp=0.1,
            flow=np.array([[[1.0, -0.5]]], dtype=np.float32),
        )

        np.testing.assert_allclose(offset, [[[0.01, -0.005]]], atol=1e-6)

    def test_patched_alpha_restores_source_alpha(self):
        source = np.array([[[0.2, 0.3, 0.4, 0.8]]], dtype=np.float32)
        displaced = np.array([[[0.9, 0.9, 0.9, 0.2]]], dtype=np.float32)

        patched = preserve_source_alpha(displaced, source)

        self.assertEqual(patched[0, 0, 3], 0.8)
        self.assertLess(patched[0, 0, 0], 0.9)

    def test_shake_offset_at_time_maps_peak_signed_offset(self):
        offset = shake_offset_at_time(
            time_seconds=0.5,
            speed=1.0,
            bounds=(0.0, 1.0),
            friction=(1.0, 1.0),
            time_offset=0.0,
        )

        self.assertAlmostEqual(offset, 0.4775087235147958, delta=1.0e-6)

    def test_shake_offset_uses_we_tau_constant_named_m_pi_2(self):
        offset = shake_offset_at_time(
            time_seconds=9.273,
            speed=1.0,
            bounds=(0.0, 1.0),
            friction=(1.0, 1.0),
            time_offset=0.0,
        )

        self.assertAlmostEqual(offset, 0.15059110797000663, delta=1.0e-6)

    def test_sample_bilinear_rgba_samples_center_between_four_pixels(self):
        image = np.array(
            [
                [[0.0, 0.0, 0.0, 1.0], [1.0, 0.0, 0.0, 1.0]],
                [[0.0, 1.0, 0.0, 1.0], [0.0, 0.0, 1.0, 1.0]],
            ],
            dtype=np.float32,
        )

        sampled = sample_bilinear_rgba(image, np.array([[[0.5, 0.5]]], dtype=np.float32))

        np.testing.assert_allclose(sampled, [[[0.25, 0.25, 0.25, 1.0]]], atol=1e-6)

    def test_capture_path_for_stage_finds_layer_stage(self):
        manifest = {
            "captures": [
                {"stage": "effect-input", "path": "/tmp/nope.tga", "layer": {"layerId": 404}},
                {"stage": "effect-output", "path": "/tmp/yes.tga", "layer": {"layerId": 405}},
            ]
        }

        self.assertEqual(capture_path_for_stage(manifest, 405, "effect-output"), "/tmp/yes.tga")

    def test_capture_time_prefers_actual_shader_time(self):
        self.assertEqual(capture_time_seconds({"shaderTimeSeconds": 9.5, "captureDelayMs": 9287}), 9.5)
        self.assertEqual(capture_time_seconds({"captureDelayMs": 9287}), 9.287)

    def test_select_effect_material_finds_requested_effect_index(self):
        manifest = {
            "captures": [{
                "stage": "effect-input",
                "layer": {
                    "layerId": 405,
                    "effectMaterials": [
                        {"effectIndex": 10, "shader": "effects/shake"},
                        {"effectIndex": 11, "shader": "effects/shake", "materialValues": {"strength": [0.068]}},
                    ],
                },
            }]
        }

        material = select_effect_material(manifest, 405, 11)

        self.assertEqual(material["materialValues"]["strength"], [0.068])

    def test_slot_union_pixels_maps_layer_local_bounds_with_padding(self):
        coverage = [
            {"slot": 3, "layerLocalBounds": [10.0, -10.0, 20.0, 0.0]},
            {"slot": 13, "layerLocalBounds": [15.0, -5.0, 30.0, 5.0]},
        ]

        self.assertEqual(slot_union_pixels(coverage, (100, 80), slots=(3, 13), padding=2), (58, 28, 82, 47))

    def test_apply_shake_pass_centered_flow_leaves_source_unchanged(self):
        source = np.array(
            [
                [[0.0, 0.0, 0.0, 1.0], [1.0, 0.0, 0.0, 1.0]],
                [[0.0, 1.0, 0.0, 1.0], [0.0, 0.0, 1.0, 1.0]],
            ],
            dtype=np.float32,
        )
        mask = np.full((1, 1, 4), [0.498, 0.498, 0.0, 1.0], dtype=np.float32)

        output = apply_shake_pass(source, mask, offset=1.0, amp=0.1)

        np.testing.assert_allclose(output, source, atol=1e-6)

    def test_offset_sweep_finds_best_matching_shake_offset(self):
        source = np.array(
            [
                [[0.0, 0.0, 0.0, 1.0], [0.5, 0.0, 0.0, 1.0], [1.0, 0.0, 0.0, 1.0]],
            ],
            dtype=np.float32,
        )
        mask = np.full((1, 1, 4), [1.0, 0.498, 0.0, 1.0], dtype=np.float32)
        gpu = apply_shake_pass(source, mask, offset=0.5, amp=0.5)

        sweep = offset_sweep(source, gpu, mask, crop=(0, 0, 3, 1), amp=0.5, offsets=(-0.5, 0.0, 0.5))

        self.assertEqual(sweep[0]["offset"], 0.5)


if __name__ == "__main__":
    unittest.main()
