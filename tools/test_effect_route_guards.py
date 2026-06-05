import importlib.util
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools" / "effect_route_guards.py"

spec = importlib.util.spec_from_file_location("effect_route_guards", MODULE_PATH)
effect_route_guards = importlib.util.module_from_spec(spec)
assert spec.loader is not None
sys.modules[spec.name] = effect_route_guards
spec.loader.exec_module(effect_route_guards)


def capture(layer_id, publish):
    return {
        "stage": "effect-input",
        "layer": {
            "layerId": layer_id,
            "layerName": f"Layer {layer_id}",
            "publish": publish,
        },
    }


def puppet_publish(*, viewport, expanded, min_pos=(-40.0, -30.0, 0.0), max_pos=(45.0, 35.0, 0.0)):
    return {
        "puppetLayer": True,
        "effectInputMeshKind": "puppet-skinned-mesh",
        "effectInputViewportSize": list(viewport),
        "effectInputViewportExpanded": expanded,
        "effectInputMeshBounds": {
            "positionMin": list(min_pos),
            "positionMax": list(max_pos),
        },
    }


class EffectRouteGuardTests(unittest.TestCase):
    def test_clipped_puppet_mesh_fails_when_viewport_was_not_expanded(self):
        manifest = {
            "captures": [
                capture(
                    22,
                    puppet_publish(
                        viewport=(3050.0, 2650.0),
                        expanded=False,
                        min_pos=(81.79, -1299.36, 0.0),
                        max_pos=(1895.96, 913.17, 0.0),
                    ),
                )
            ]
        }

        result = effect_route_guards.evaluate_manifest(manifest)

        self.assertFalse(result.passed)
        self.assertEqual(result.checked_count, 1)
        self.assertIn("22:Layer 22", result.detail)
        self.assertIn("mesh bounds exceed viewport", result.detail)

    def test_expanded_puppet_mesh_passes_and_reports_evidence(self):
        manifest = {
            "captures": [
                capture(
                    22,
                    puppet_publish(
                        viewport=(3792.0, 2650.0),
                        expanded=True,
                        min_pos=(81.79, -1299.36, 0.0),
                        max_pos=(1895.96, 913.17, 0.0),
                    ),
                )
            ]
        }

        result = effect_route_guards.evaluate_manifest(manifest)

        self.assertTrue(result.passed)
        self.assertEqual(result.checked_count, 1)
        self.assertEqual(result.expanded_count, 1)
        self.assertIn("1 puppet effect viewport expanded to mesh bounds", result.detail)

    def test_in_bounds_puppet_mesh_passes_without_expansion(self):
        manifest = {
            "captures": [
                capture(
                    405,
                    puppet_publish(
                        viewport=(100.0, 80.0),
                        expanded=False,
                        min_pos=(-40.0, -30.0, 0.0),
                        max_pos=(45.0, 35.0, 0.0),
                    ),
                )
            ]
        }

        result = effect_route_guards.evaluate_manifest(manifest)

        self.assertTrue(result.passed)
        self.assertEqual(result.checked_count, 1)
        self.assertEqual(result.expanded_count, 0)
        self.assertIn("1 puppet effect viewport checked", result.detail)

    def test_tiny_puppet_mesh_bleed_passes_without_expansion(self):
        manifest = {
            "captures": [
                capture(
                    405,
                    puppet_publish(
                        viewport=(4160.0, 2923.0),
                        expanded=False,
                        min_pos=(-2087.0, -1403.5, 0.0),
                        max_pos=(2062.0, 1527.5, 0.0),
                    ),
                )
            ]
        }

        result = effect_route_guards.evaluate_manifest(manifest)

        self.assertTrue(result.passed)
        self.assertEqual(result.checked_count, 1)
        self.assertEqual(result.expanded_count, 0)
        self.assertIn("1 puppet effect viewport checked", result.detail)

    def test_non_puppet_layers_are_ignored(self):
        manifest = {
            "captures": [
                capture(
                    124,
                    {
                        "puppetLayer": False,
                        "effectInputMeshKind": "card",
                        "effectInputViewportSize": [3840.0, 1688.0],
                        "effectInputMeshBounds": {
                            "positionMin": [-1920.0, -844.0, 0.0],
                            "positionMax": [1920.0, 844.0, 0.0],
                        },
                    },
                )
            ]
        }

        result = effect_route_guards.evaluate_manifest(manifest)

        self.assertTrue(result.passed)
        self.assertEqual(result.checked_count, 0)
        self.assertIn("no puppet effect viewports", result.detail)


if __name__ == "__main__":
    unittest.main()
