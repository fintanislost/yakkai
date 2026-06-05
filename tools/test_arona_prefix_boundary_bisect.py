#!/usr/bin/env python3

import math
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from arona_prefix_boundary_bisect import (
    TIER_ORDER,
    _final_summary_markdown,
    classify_first_bad_boundary,
    compute_motion_peaks,
    compute_white_margin_score,
    frame_files,
    mutate_scene_for_tier,
    validate_roi_config,
)


class PrefixBoundaryBisectTests(unittest.TestCase):
    def test_frame_files_supports_underscore_and_dash_with_numeric_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ("frame-0002.png", "frame_0001.png", "frame-0010.png", "other.png"):
                (root / name).touch()

            result = [path.name for path in frame_files(root)]

        self.assertEqual(result, ["frame_0001.png", "frame-0002.png", "frame-0010.png"])

    def test_validate_roi_config_accepts_required_regions(self):
        config = {
            "baseSize": [1280, 720],
            "regions": {
                "whiteMarginCheck": [100, 30, 1110, 560],
                "lowerRibbonTip": [860, 250, 1030, 390],
                "wallGlowBehindRibbon": [980, 130, 1180, 360],
                "bowMotion": [835, 115, 1020, 285],
            },
        }

        result = validate_roi_config(config)

        self.assertEqual(result["baseSize"], [1280, 720])
        self.assertEqual(result["regions"]["whiteMarginCheck"], [100, 30, 1110, 560])

    def test_validate_roi_config_rejects_missing_region(self):
        config = {"baseSize": [1280, 720], "regions": {"whiteMarginCheck": [0, 0, 10, 10]}}

        with self.assertRaises(ValueError) as ctx:
            validate_roi_config(config)

        self.assertIn("lowerRibbonTip", str(ctx.exception))

    def test_white_margin_score_detects_yakkai_only_white_artifact(self):
        we = Image.new("RGBA", (64, 64), (120, 180, 230, 255))
        yakkai = Image.new("RGBA", (64, 64), (120, 180, 230, 255))
        for x in range(8, 56):
            for y in range(8, 16):
                yakkai.putpixel((x, y), (255, 255, 255, 255))

        score = compute_white_margin_score(
            np.asarray(we),
            np.asarray(yakkai),
            [0, 0, 64, 64],
        )

        self.assertGreater(score["artifactPixelRatio"], 0.05)
        self.assertGreater(score["positiveLumaDeltaMean"], 0.05)

    def test_white_margin_score_stays_low_for_matching_images(self):
        we = Image.new("RGBA", (64, 64), (220, 230, 240, 255))
        yakkai = Image.new("RGBA", (64, 64), (220, 230, 240, 255))

        score = compute_white_margin_score(
            np.asarray(we),
            np.asarray(yakkai),
            [0, 0, 64, 64],
        )

        self.assertLess(score["artifactPixelRatio"], 0.001)
        self.assertLess(score["positiveLumaDeltaMean"], 0.001)

    def test_motion_peaks_find_expected_frequency(self):
        timestamps_ms = np.arange(0, 12000, 80, dtype=np.float32)
        t = timestamps_ms / 1000.0
        signal = np.sin(math.tau * 0.318 * t)
        frames = []
        for value in signal:
            image = np.zeros((32, 32, 4), dtype=np.uint8)
            y = int(16 + value * 5)
            image[max(0, y - 1):min(32, y + 2), 8:24, :] = [255, 255, 255, 255]
            frames.append(image)

        peaks = compute_motion_peaks(frames, timestamps_ms, [0, 0, 32, 32])

        self.assertAlmostEqual(peaks["nearestExpected"]["0.318"]["frequencyHz"], 0.318, delta=0.03)
        self.assertGreater(peaks["nearestExpected"]["0.318"]["amplitude"], 0.1)
        self.assertIn("dominantHz", peaks)
        self.assertIn("dominantAmplitude", peaks)
        self.assertGreater(peaks["dominantHz"], 0.0)

    def test_mutate_scene_for_tier_disables_parallax_and_effects(self):
        scene = {
            "general": {
                "camera": {
                    "cameraparallax": True,
                    "cameraparallaxamount": 0.1,
                    "cameraparallaxmouseinfluence": 0.5,
                }
            },
            "objects": [
                {
                    "id": 405,
                    "name": "ARONA_CROP_SHEET",
                    "effects": [
                        {"name": "Halo Pulse", "visible": True},
                        {"name": "Triangle Pulse", "visible": True},
                        {"name": "Ribbon Wave Top", "visible": True},
                        {"name": "Arm Shake", "visible": True},
                    ],
                }
            ],
        }

        mutated = mutate_scene_for_tier(scene, "pulse")
        effects = {effect["name"]: effect["visible"] for effect in mutated["objects"][0]["effects"]}

        self.assertFalse(mutated["general"]["camera"]["cameraparallax"])
        self.assertEqual(mutated["general"]["camera"]["cameraparallaxamount"], 0)
        self.assertEqual(mutated["general"]["camera"]["cameraparallaxmouseinfluence"], 0)
        self.assertTrue(effects["Halo Pulse"])
        self.assertTrue(effects["Triangle Pulse"])
        self.assertFalse(effects["Ribbon Wave Top"])
        self.assertFalse(effects["Arm Shake"])

    def test_classify_first_bad_boundary_uses_first_score_jump(self):
        tier_scores = {
            "base": {"artifactPixelRatio": 0.001},
            "pulse": {"artifactPixelRatio": 0.002},
            "pulse_waterwaves": {"artifactPixelRatio": 0.090},
            "full_safe_motion": {"artifactPixelRatio": 0.095},
        }

        result = classify_first_bad_boundary(tier_scores, threshold=0.03)

        self.assertEqual(result["classification"], "waterwaves-stage")
        self.assertEqual(result["firstBadTier"], "pulse_waterwaves")

    def test_classify_first_bad_boundary_uses_max_for_intermittent_artifact(self):
        tier_scores = {
            "base": {
                "artifactPixelRatio": 0.001,
                "artifactPixelRatioP95": 0.002,
                "artifactPixelRatioMax": 0.010,
            },
            "pulse": {
                "artifactPixelRatio": 0.002,
                "artifactPixelRatioP95": 0.003,
                "artifactPixelRatioMax": 0.050,
            },
            "pulse_waterwaves": {
                "artifactPixelRatio": 0.004,
                "artifactPixelRatioP95": 0.006,
                "artifactPixelRatioMax": 0.070,
            },
        }

        result = classify_first_bad_boundary(tier_scores, threshold=0.03)

        self.assertEqual(result["classification"], "pulse-stage")
        self.assertEqual(result["firstBadTier"], "pulse")
        self.assertEqual(result["classificationMetric"], "artifactPixelRatioMax")
        self.assertEqual(result["classificationMetricValue"], 0.050)
        self.assertIn("artifactPixelRatioMax", result["reason"])

    def test_classify_first_bad_boundary_uses_motion_cliff_when_white_margin_is_clean(self):
        tier_scores = {
            tier: {
                "artifactPixelRatio": 0.001,
                "artifactPixelRatioP95": 0.002,
                "artifactPixelRatioMax": 0.003,
            }
            for tier in TIER_ORDER
        }
        quiet_motion = {
            "nearestExpected": {
                "0.159": {"amplitude": 0.02},
                "0.318": {"amplitude": 0.03},
                "0.398": {"amplitude": 0.04},
            }
        }
        cliff_we = {
            "nearestExpected": {
                "0.159": {"amplitude": 0.12},
                "0.318": {"amplitude": 0.05},
                "0.398": {"amplitude": 0.46},
            }
        }
        cliff_yakkai = {
            "nearestExpected": {
                "0.159": {"amplitude": 0.03},
                "0.318": {"amplitude": 0.05},
                "0.398": {"amplitude": 0.013},
            }
        }
        motion_by_tier = {
            tier: {
                "bowMotion": {"we": quiet_motion, "yakkai": quiet_motion},
                "lowerRibbonTip": {"we": quiet_motion, "yakkai": quiet_motion},
            }
            for tier in TIER_ORDER
        }
        motion_by_tier["pulse_waterwaves"] = {
            "bowMotion": {"we": cliff_we, "yakkai": cliff_yakkai},
            "lowerRibbonTip": {"we": quiet_motion, "yakkai": quiet_motion},
        }

        result = classify_first_bad_boundary(tier_scores, motion_by_tier=motion_by_tier)

        self.assertEqual(result["classification"], "waterwaves-stage")
        self.assertEqual(result["firstBadTier"], "pulse_waterwaves")
        self.assertEqual(result["classificationMetric"], "motionAmplitudeRatio")
        self.assertEqual(result["motionRegion"], "bowMotion")
        self.assertEqual(result["motionFrequency"], "0.398")
        self.assertAlmostEqual(result["weAmplitude"], 0.46)
        self.assertAlmostEqual(result["yakkaiAmplitude"], 0.013)
        self.assertLessEqual(result["amplitudeRatio"], 0.35)

    def test_final_summary_uses_planned_schema_and_motion_table(self):
        motion = {
            "frameCount": 32,
            "durationSeconds": 2.48,
            "signalP2P": 4.5,
            "dominantHz": 0.318,
            "dominantAmplitude": 2.2,
            "nearestExpected": {
                "0.159": {"frequencyHz": 0.159, "amplitude": 0.4},
                "0.318": {"frequencyHz": 0.318, "amplitude": 2.2},
                "0.398": {"frequencyHz": 0.398, "amplitude": 0.7},
            },
        }
        tiers = {}
        for tier in TIER_ORDER:
            tiers[tier] = {
                "tier": tier,
                "boundary": f"{tier}-boundary",
                "frameCount": 32,
                "whiteMargin": {
                    "artifactPixelRatio": 0.001,
                    "artifactPixelRatioP95": 0.002,
                    "artifactPixelRatioMax": 0.003,
                },
                "motion": {
                    "bowMotion": {"we": motion, "yakkai": motion},
                    "lowerRibbonTip": {"we": motion, "yakkai": motion},
                },
                "contactSheet": f"contact-{tier}.png",
            }
            self.assertIn("whiteMargin", tiers[tier])
            self.assertIn("motion", tiers[tier])
            self.assertIn("dominantHz", tiers[tier]["motion"]["bowMotion"]["we"])

        summary = _final_summary_markdown(
            {
                "classification": {
                    "classification": "no-regression-reproduced",
                    "firstBadTier": None,
                    "classificationMetric": None,
                    "classificationMetricValue": None,
                    "threshold": 0.03,
                    "reason": "no tier crossed threshold",
                },
                "tiers": tiers,
            }
        )

        self.assertIn("## Motion", summary)
        self.assertIn("| tier | region | source | dominant Hz | dominant amplitude | signal P2P | amp 0.159 | amp 0.318 | amp 0.398 |", summary)
        self.assertIn("| base | bowMotion | we | 0.318000 | 2.200000 | 4.500000 | 0.400000 | 2.200000 | 0.700000 |", summary)


if __name__ == "__main__":
    unittest.main()
