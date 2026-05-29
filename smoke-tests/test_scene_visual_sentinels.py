import importlib.util
from pathlib import Path
import sys
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools" / "scene_visual_sentinels.py"

spec = importlib.util.spec_from_file_location("scene_visual_sentinels", MODULE_PATH)
scene_visual_sentinels = importlib.util.module_from_spec(spec)
assert spec.loader is not None
sys.modules[spec.name] = scene_visual_sentinels
spec.loader.exec_module(scene_visual_sentinels)


class SceneVisualSentinelTests(unittest.TestCase):
    def test_clear_color_leak_detects_flat_region_near_clear_color(self):
        stats = scene_visual_sentinels.RegionStats(
            name="mid_wall",
            mean_rgb=(126.0, 136.0, 166.0),
            stddev_rgb=(6.0, 7.0, 5.0),
            unique_colors=90,
        )

        self.assertTrue(
            scene_visual_sentinels.is_clear_color_leak(stats, (126.0, 136.0, 166.0))
        )

    def test_clear_color_leak_rejects_detailed_region(self):
        stats = scene_visual_sentinels.RegionStats(
            name="window",
            mean_rgb=(126.0, 136.0, 166.0),
            stddev_rgb=(25.0, 26.0, 22.0),
            unique_colors=900,
        )

        self.assertFalse(
            scene_visual_sentinels.is_clear_color_leak(stats, (126.0, 136.0, 166.0))
        )

    def test_clear_color_leak_rejects_wrong_color(self):
        stats = scene_visual_sentinels.RegionStats(
            name="desk",
            mean_rgb=(80.0, 105.0, 150.0),
            stddev_rgb=(4.0, 5.0, 4.0),
            unique_colors=80,
        )

        self.assertFalse(
            scene_visual_sentinels.is_clear_color_leak(stats, (126.0, 136.0, 166.0))
        )

    def test_3476236738_fails_when_multiple_background_regions_leak(self):
        stats = {
            "mid_wall_between_chars": scene_visual_sentinels.RegionStats(
                name="mid_wall_between_chars",
                mean_rgb=(125.3, 135.3, 165.2),
                stddev_rgb=(9.6, 9.5, 10.0),
                unique_colors=81,
            ),
            "right_wall_under_window": scene_visual_sentinels.RegionStats(
                name="right_wall_under_window",
                mean_rgb=(126.1, 135.8, 166.1),
                stddev_rgb=(8.0, 8.4, 6.6),
                unique_colors=149,
            ),
            "gray_band_right": scene_visual_sentinels.RegionStats(
                name="gray_band_right",
                mean_rgb=(126.5, 136.2, 166.4),
                stddev_rgb=(9.6, 9.6, 7.2),
                unique_colors=165,
            ),
            "desk_area": scene_visual_sentinels.RegionStats(
                name="desk_area",
                mean_rgb=(93.0, 108.1, 153.7),
                stddev_rgb=(47.4, 47.4, 41.7),
                unique_colors=2917,
            ),
        }

        result = scene_visual_sentinels.evaluate_scene_sentinel(
            "3476236738",
            stats,
            clear_rgb=(126.0, 136.0, 166.0),
        )

        self.assertFalse(result.passed)
        self.assertIn("mid_wall_between_chars", result.detail)
        self.assertIn("right_wall_under_window", result.detail)

    def test_3476236738_passes_when_only_one_region_leaks(self):
        stats = {
            "mid_wall_between_chars": scene_visual_sentinels.RegionStats(
                name="mid_wall_between_chars",
                mean_rgb=(125.3, 135.3, 165.2),
                stddev_rgb=(9.6, 9.5, 10.0),
                unique_colors=81,
            ),
            "right_wall_under_window": scene_visual_sentinels.RegionStats(
                name="right_wall_under_window",
                mean_rgb=(120.0, 150.0, 190.0),
                stddev_rgb=(25.0, 30.0, 28.0),
                unique_colors=900,
            ),
            "gray_band_right": scene_visual_sentinels.RegionStats(
                name="gray_band_right",
                mean_rgb=(100.0, 120.0, 190.0),
                stddev_rgb=(20.0, 25.0, 24.0),
                unique_colors=700,
            ),
            "desk_area": scene_visual_sentinels.RegionStats(
                name="desk_area",
                mean_rgb=(93.0, 108.1, 153.7),
                stddev_rgb=(47.4, 47.4, 41.7),
                unique_colors=2917,
            ),
        }

        result = scene_visual_sentinels.evaluate_scene_sentinel(
            "3476236738",
            stats,
            clear_rgb=(126.0, 136.0, 166.0),
        )

        self.assertTrue(result.passed)
        self.assertIn("1 clear-color-like region", result.detail)


if __name__ == "__main__":
    unittest.main()
