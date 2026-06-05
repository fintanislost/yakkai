import math
import sys
import tempfile
import unittest
import warnings
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from arona_ribbon_flutter_parity import classify_flutter_report, extract_edge_trace, measure_flutter_sequence


def write_edge_sequence(
    directory: Path,
    *,
    wave_amp: float,
    rigid_amp: float,
    phase_lag: float = 0.0,
    frames: int = 24,
) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    width = 96
    height = 64
    xs = np.arange(width, dtype=np.float32)
    for frame_index in range(frames):
        image = np.zeros((height, width, 4), dtype=np.uint8)
        image[:, :, :] = [210, 240, 250, 255]
        phase = (frame_index / frames) * math.tau
        rigid = rigid_amp * math.sin(phase)
        wave = wave_amp * np.sin((xs / width) * math.tau * 2.0 + phase + phase_lag)
        ys = np.round(32.0 + rigid + wave).astype(np.int32)
        for x, y in enumerate(ys):
            y0 = max(2, min(height - 4, int(y)))
            image[:y0, x, :] = [248, 248, 255, 255]
            image[y0:y0 + 2, x, :] = [70, 120, 180, 255]
            image[y0 + 2:, x, :] = [190, 235, 255, 255]
        Image.fromarray(image, "RGBA").save(directory / f"frame-{frame_index:04d}.png")


class RibbonFlutterParityTests(unittest.TestCase):
    def test_rigid_motion_has_low_flutter_residual(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_edge_sequence(root, wave_amp=0.0, rigid_amp=8.0)

            result = measure_flutter_sequence(root, [0, 0, 96, 64], sample_columns=48)

            self.assertGreater(result["rigidMotionAmplitudePx"], 10.0)
            self.assertLess(result["flutterResidualRmsPx"], 0.8)
            self.assertEqual(result["motionClass"], "rigid-motion")

    def test_traveling_wave_has_high_flutter_residual(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_edge_sequence(root, wave_amp=6.0, rigid_amp=1.0)

            result = measure_flutter_sequence(root, [0, 0, 96, 64], sample_columns=48)

            self.assertGreater(result["flutterResidualRmsPx"], 2.0)
            self.assertEqual(result["motionClass"], "flutter-wave")

    def test_phase_shift_is_measured_between_sequences(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            left = root / "left"
            right = root / "right"
            write_edge_sequence(left, wave_amp=6.0, rigid_amp=1.0, phase_lag=0.0)
            write_edge_sequence(right, wave_amp=6.0, rigid_amp=1.0, phase_lag=math.pi / 2.0)

            left_result = measure_flutter_sequence(left, [0, 0, 96, 64], sample_columns=48)
            right_result = measure_flutter_sequence(right, [0, 0, 96, 64], sample_columns=48)
            report = {
                "sequences": {
                    "windows": {"flutter": left_result},
                    "normal-yakkai": {"flutter": right_result},
                }
            }

            self.assertEqual(classify_flutter_report(report), "windows-and-yakkai-flutter-different-phase")

    def test_low_contrast_edge_is_low_confidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            root.mkdir(parents=True, exist_ok=True)
            for index in range(4):
                image = np.full((64, 96, 4), [220, 220, 220, 255], dtype=np.uint8)
                Image.fromarray(image, "RGBA").save(root / f"frame-{index:04d}.png")

            trace = extract_edge_trace(root, [0, 0, 96, 64], sample_columns=48)

            self.assertEqual(trace["confidence"], "low")

    def test_sparse_invalid_columns_do_not_warn_or_poison_metrics(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            root.mkdir(parents=True, exist_ok=True)
            width = 96
            height = 64
            xs = np.arange(width, dtype=np.float32)
            for frame_index in range(12):
                image = np.full((height, width, 4), [220, 220, 220, 255], dtype=np.uint8)
                phase = (frame_index / 12) * math.tau
                ys = np.round(32.0 + 3.0 * np.sin((xs / width) * math.tau + phase)).astype(np.int32)
                for x, y in enumerate(ys[:82]):
                    y0 = max(2, min(height - 4, int(y)))
                    image[:y0, x, :] = [248, 248, 255, 255]
                    image[y0:y0 + 2, x, :] = [70, 120, 180, 255]
                    image[y0 + 2:, x, :] = [190, 235, 255, 255]
                Image.fromarray(image, "RGBA").save(root / f"frame-{frame_index:04d}.png")

            with warnings.catch_warnings():
                warnings.simplefilter("error", RuntimeWarning)
                result = measure_flutter_sequence(root, [0, 0, 96, 64], sample_columns=48)

            self.assertEqual(result["confidence"], "high")
            self.assertTrue(math.isfinite(result["flutterResidualRmsPx"]))


if __name__ == "__main__":
    unittest.main()
