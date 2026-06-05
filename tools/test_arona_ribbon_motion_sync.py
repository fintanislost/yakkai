import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

import arona_ribbon_motion_sync as motion


TIP_ROI = (0, 0, 50, 40)
MAIN_ROI = (50, 0, 50, 40)


class AronaRibbonMotionSyncTests(unittest.TestCase):
    def write_sequence(
        self,
        directory: Path,
        tip_offsets: list[int],
        main_offsets: list[int],
    ) -> None:
        directory.mkdir(parents=True, exist_ok=True)
        frame_count = min(len(tip_offsets), len(main_offsets))
        for index in range(frame_count):
            image = Image.new("RGB", (100, 40), (4, 4, 8))
            draw = ImageDraw.Draw(image)
            tip_y = 20 + tip_offsets[index]
            main_y = 20 + main_offsets[index]
            draw.rectangle((14, tip_y - 2, 34, tip_y + 2), fill=(240, 240, 255))
            draw.rectangle((64, main_y - 2, 84, main_y + 2), fill=(240, 240, 255))
            image.save(directory / f"frame-{index:04d}.png")

    def write_blank_sequence(self, directory: Path, frame_count: int = 12) -> None:
        directory.mkdir(parents=True, exist_ok=True)
        for index in range(frame_count):
            image = Image.new("RGB", (100, 40), (4, 4, 8))
            image.save(directory / f"frame-{index:04d}.png")

    def classify_yakkai(
        self,
        yakkai_tip: list[int],
        yakkai_main: list[int],
    ) -> dict:
        in_sync = [0, 2, 4, 2, 0, -2, -4, -2, 0, 2, 4, 2]
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            windows_dir = root / "windows"
            yakkai_dir = root / "yakkai"
            self.write_sequence(windows_dir, in_sync, in_sync)
            self.write_sequence(yakkai_dir, yakkai_tip, yakkai_main)

            return motion.build_report(
                windows_dir,
                yakkai_dir,
                tip_roi=TIP_ROI,
                main_roi=MAIN_ROI,
                label="synthetic",
                roi_base_size=(100, 40),
            )

    def test_parse_roi_requires_four_non_negative_numbers(self):
        self.assertEqual(motion.parse_roi("1,2,3,4"), (1, 2, 3, 4))
        with self.assertRaises(ValueError):
            motion.parse_roi("1,2,3")
        with self.assertRaises(ValueError):
            motion.parse_roi("1,2,-3,4")

    def test_classifies_in_sync_motion(self):
        offsets = [0, 2, 4, 2, 0, -2, -4, -2, 0, 2, 4, 2]
        report = self.classify_yakkai(offsets, offsets)

        self.assertEqual(report["windows"]["classification"], "in-sync")
        self.assertEqual(report["yakkai"]["classification"], "in-sync")
        self.assertEqual(report["delta"]["classification"], "motion-matches-reference")

    def test_classifies_out_of_sync_motion(self):
        tip = [0, 2, 4, 2, 0, -2, -4, -2, 0, 2, 4, 2]
        main = [0, -2, -4, -2, 0, 2, 4, 2, 0, -2, -4, -2]
        report = self.classify_yakkai(tip, main)

        self.assertEqual(report["windows"]["classification"], "in-sync")
        self.assertEqual(report["yakkai"]["classification"], "out-of-sync")
        self.assertEqual(
            report["delta"]["classification"],
            "yakkai-motion-diverges-from-reference",
        )

    def test_classifies_tip_only_motion(self):
        tip = [0, 2, 4, 2, 0, -2, -4, -2, 0, 2, 4, 2]
        main = [0] * len(tip)
        report = self.classify_yakkai(tip, main)

        self.assertEqual(report["yakkai"]["classification"], "tip-only")

    def test_classifies_main_only_motion(self):
        main = [0, 2, 4, 2, 0, -2, -4, -2, 0, 2, 4, 2]
        tip = [0] * len(main)
        report = self.classify_yakkai(tip, main)

        self.assertEqual(report["yakkai"]["classification"], "main-only")

    def test_classifies_low_confidence_when_regions_have_no_signal(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            windows_dir = root / "windows"
            yakkai_dir = root / "yakkai"
            self.write_blank_sequence(windows_dir)
            self.write_blank_sequence(yakkai_dir)

            report = motion.build_report(
                windows_dir,
                yakkai_dir,
                tip_roi=TIP_ROI,
                main_roi=MAIN_ROI,
                label="blank",
                roi_base_size=(100, 40),
            )

        self.assertEqual(report["windows"]["classification"], "low-confidence")
        self.assertEqual(report["yakkai"]["classification"], "low-confidence")


if __name__ == "__main__":
    unittest.main()
