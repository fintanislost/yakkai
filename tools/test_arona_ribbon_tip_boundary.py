import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from arona_ribbon_tip_boundary import (
    classify_motion,
    load_frames,
    measure_composition_roi,
    measure_roi_motion,
    scale_roi,
)


def write_frame(path: Path, y: int, alpha: int = 255) -> None:
    image = np.zeros((48, 64, 4), dtype=np.uint8)
    image[y:y + 6, 25:42, :] = [255, 255, 255, alpha]
    Image.fromarray(image, "RGBA").save(path)


def write_edge_frame(path: Path, *, hard: bool) -> None:
    image = np.zeros((80, 120, 4), dtype=np.uint8)
    image[:, :, :] = [180, 235, 255, 255]
    if hard:
        image[35:42, 20:100, :] = [255, 255, 255, 255]
        image[42:48, 20:100, :] = [80, 90, 110, 255]
    else:
        for row in range(24):
            alpha = row / 23.0
            color = int(225 + 30 * alpha)
            image[28 + row, 20:100, :] = [color, color, 255, 255]
    Image.fromarray(image, "RGBA").save(path)


def write_cyan_strand_frame(path: Path, *, crisp: bool) -> None:
    image = np.zeros((80, 120, 4), dtype=np.uint8)
    image[:, :, :] = [210, 240, 250, 255]
    width = 3 if crisp else 11
    x0 = 55 - width // 2
    image[10:70, x0:x0 + width, :] = [25, 235, 240, 255]
    Image.fromarray(image, "RGBA").save(path)


class RibbonTipBoundaryTests(unittest.TestCase):
    def test_static_sequence_classifies_static(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for index in range(6):
                write_frame(root / f"frame-{index:04d}.png", 20)
            frames = load_frames(root)
            result = measure_roi_motion(frames, [20, 12, 28, 28])
            self.assertLess(result["verticalAmplitudePx"], 0.5)
            self.assertEqual(classify_motion(result, moving_threshold_px=2.0), "static")

    def test_moving_sequence_classifies_moving(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for index, y in enumerate([20, 18, 16, 18, 20, 22]):
                write_frame(root / f"frame-{index:04d}.png", y)
            frames = load_frames(root)
            result = measure_roi_motion(frames, [20, 10, 28, 32])
            self.assertGreater(result["verticalAmplitudePx"], 4.0)
            self.assertEqual(classify_motion(result, moving_threshold_px=2.0), "moving")

    def test_low_alpha_sequence_classifies_low_confidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for index in range(6):
                write_frame(root / f"frame-{index:04d}.png", 20, alpha=5)
            frames = load_frames(root)
            result = measure_roi_motion(frames, [20, 12, 28, 28])
            self.assertEqual(result["confidence"], "low")

    def test_scales_roi_between_reference_sizes(self):
        self.assertEqual(
            scale_roi([100, 50, 200, 100], from_size=(1600, 900), to_size=(1920, 1080)),
            [120, 60, 240, 120],
        )

    def test_hard_edge_scores_above_soft_edge(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            hard = root / "hard.png"
            soft = root / "soft.png"
            write_edge_frame(hard, hard=True)
            write_edge_frame(soft, hard=False)

            hard_result = measure_composition_roi([Image.open(hard).convert("RGBA")], [0, 0, 120, 80])
            soft_result = measure_composition_roi([Image.open(soft).convert("RGBA")], [0, 0, 120, 80])

            self.assertGreater(hard_result["edgeHardnessP95"], soft_result["edgeHardnessP95"])
            self.assertGreater(hard_result["edgeHardnessMean"], soft_result["edgeHardnessMean"])

    def test_glow_wash_scores_bright_low_edge_region(self):
        bright = np.full((60, 90, 4), [242, 252, 255, 255], dtype=np.uint8)
        hard = bright.copy()
        hard[30:34, :, :] = [80, 120, 160, 255]

        bright_result = measure_composition_roi([Image.fromarray(bright, "RGBA")], [0, 0, 90, 60])
        hard_result = measure_composition_roi([Image.fromarray(hard, "RGBA")], [0, 0, 90, 60])

        self.assertGreater(bright_result["glowWashScore"], hard_result["glowWashScore"])

    def test_cyan_crispness_scores_narrow_high_gradient_strand(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            crisp = root / "crisp.png"
            soft = root / "soft.png"
            write_cyan_strand_frame(crisp, crisp=True)
            write_cyan_strand_frame(soft, crisp=False)

            crisp_result = measure_composition_roi([Image.open(crisp).convert("RGBA")], [0, 0, 120, 80])
            soft_result = measure_composition_roi([Image.open(soft).convert("RGBA")], [0, 0, 120, 80])

            self.assertGreater(crisp_result["cyanCrispness"], soft_result["cyanCrispness"])


if __name__ == "__main__":
    unittest.main()
