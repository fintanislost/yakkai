#!/usr/bin/env python3

import tempfile
import unittest
from pathlib import Path

import numpy as np

from arona_waterwaves_output_oracle import (
    classify_waterwaves_mismatch,
    preserve_source_alpha,
    sample_bilinear_rgba,
    waterwaves_texcoord_offset,
    write_summary_markdown,
)


class WaterwavesOutputOracleTests(unittest.TestCase):
    def test_waterwaves_offset_uses_strength_mask_and_time(self):
        uv = np.array([[[0.25, 0.5]]], dtype=np.float32)
        mask = np.array([[[1.0, 0.5, 0.0, 1.0]]], dtype=np.float32)

        offset = waterwaves_texcoord_offset(
            uv,
            mask,
            time_seconds=0.25,
            strength=0.02,
            speed=1.0,
            frequency=1.0,
            direction=(1.0, 0.0),
        )

        self.assertEqual(offset.shape, uv.shape)
        self.assertGreater(abs(float(offset[0, 0, 0])), 0.0)
        self.assertAlmostEqual(float(offset[0, 0, 1]), 0.0, places=6)

    def test_waterwaves_offset_can_model_secondary_default_wave(self):
        uv = np.array([[[0.25, 0.5]]], dtype=np.float32)
        mask = np.array([[[1.0, 0.5, 0.0, 1.0]]], dtype=np.float32)

        single = waterwaves_texcoord_offset(
            uv,
            mask,
            time_seconds=0.25,
            strength=0.02,
            speed=1.0,
            frequency=1.0,
            direction=(1.0, 0.0),
        )
        dual = waterwaves_texcoord_offset(
            uv,
            mask,
            time_seconds=0.25,
            strength=0.02,
            speed=1.0,
            frequency=1.0,
            direction=(1.0, 0.0),
            secondary_speed=3.0,
            secondary_frequency=66.0,
            secondary_offset=0.0,
            secondary_exponent=1.0,
            secondary_phase_direction=(0.0, 1.0),
        )

        self.assertNotAlmostEqual(float(single[0, 0, 0]), float(dual[0, 0, 0]), places=8)

    def test_bilinear_sampler_interpolates_rgba(self):
        image = np.array(
            [
                [[0.0, 0.0, 0.0, 1.0], [1.0, 0.0, 0.0, 1.0]],
                [[0.0, 1.0, 0.0, 1.0], [1.0, 1.0, 0.0, 1.0]],
            ],
            dtype=np.float32,
        )

        sample = sample_bilinear_rgba(image, np.array([[[0.5, 0.5]]], dtype=np.float32))

        np.testing.assert_allclose(sample[0, 0, :3], [0.5, 0.5, 0.0], atol=1e-6)

    def test_preserve_source_alpha_can_suppress_displaced_silhouette_motion(self):
        source = np.array([[[0.2, 0.2, 1.0, 0.8]]], dtype=np.float32)
        displaced = np.array([[[0.9, 0.9, 1.0, 0.2]]], dtype=np.float32)

        patched = preserve_source_alpha(displaced, source, blend_edge_rgb=True)

        self.assertAlmostEqual(float(patched[0, 0, 3]), 0.8, places=6)
        self.assertLess(float(patched[0, 0, 0]), 0.9)

    def test_classifier_identifies_source_alpha_suppression(self):
        result = classify_waterwaves_mismatch(
            current_gpu_rmse=0.01,
            source_alpha_rmse=0.012,
            displaced_alpha_rmse=0.003,
            uv_variant_rmse=0.02,
            time_variant_rmse=0.02,
            final_display_lost_motion=False,
            windows_motion_ratio_current=0.02,
            windows_motion_ratio_displaced_alpha=0.72,
        )

        self.assertEqual(result["classification"], "source-alpha-suppresses-motion")

    def test_write_summary_markdown_records_classification(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "summary.md"
            write_summary_markdown(
                path,
                {
                    "classification": "mask-coordinate-mismatch",
                    "reason": "mask projection differs",
                    "metrics": {"maskIou": 0.22},
                },
            )
            text = path.read_text()

        self.assertIn("Classification: mask-coordinate-mismatch", text)
        self.assertIn("mask projection differs", text)


if __name__ == "__main__":
    unittest.main()
