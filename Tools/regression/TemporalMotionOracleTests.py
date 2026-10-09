"""Authored, UNEXECUTED unit fixture for the independent oracle, not GPU evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "motion_oracle", ROOT / "Tools/temporal-validation/Compare-TemporalMotion.py")
ORACLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ORACLE)


class TemporalMotionOracleTests(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads((ROOT / "Tools/regression/fixtures/temporal-motion/analytic-probes.json").read_text())

    def test_authored_translation_goldens(self):
        # Closed-form 256 / 16 = 16 px/world-unit, with screen Y inverted.
        goldens = [[-4, 2], [-8, -4], [6, 3], [8, 4], [-3, -6], [-2, 1]]
        for probe, golden in zip(self.fixture["probes"], goldens):
            pixel, actual = ORACLE.expected_motion(probe, self.fixture)
            self.assertEqual(pixel, [component + .5 for component in probe["pixel"]])
            self.assertEqual(actual, golden)

    def test_camera_translation_is_included(self):
        probe = self.fixture["probes"][0]
        probe["previous"] = copy.deepcopy(probe["current"])
        self.fixture["previousCamera"]["position"] = [-.125, 0, 0]
        self.assertEqual(ORACLE.expected_motion(probe, self.fixture)[1], [2, 0])

    def test_pinhole_depth_changes_magnitude(self):
        camera = {"projection": "perspective", "position": [0, 0, 0], "verticalFovDegrees": 90}
        near = ORACLE.project([1, 0, 4], camera, [256, 256])
        far = ORACLE.project([1, 0, 8], camera, [256, 256])
        self.assertAlmostEqual(near[0] - 128, 32)
        self.assertAlmostEqual(far[0] - 128, 16)

    def test_zeroed_bone_motion_changes_expected_vector(self):
        probe = self.fixture["probes"][1]
        probe["current"]["skinTranslations"] = probe["previous"]["skinTranslations"]
        self.assertNotEqual(ORACLE.expected_motion(probe, self.fixture)[1], [-8, -4])

    def test_wrong_bone_weight_is_rejected(self):
        endpoint = self.fixture["probes"][1]["current"]
        endpoint["skinTranslations"][0]["weight"] = .5
        with self.assertRaises(ValueError):
            ORACLE.world_point(endpoint)

    def test_pixel_axes_and_endpoint_reversal(self):
        probe = self.fixture["probes"][0]
        probe["previous"], probe["current"] = probe["current"], probe["previous"]
        self.assertEqual(ORACLE.expected_motion(probe, self.fixture)[1], [4, -2])


if __name__ == "__main__":
    unittest.main()
