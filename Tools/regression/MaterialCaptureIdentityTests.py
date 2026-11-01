"""Input mutations that must invalidate a controlled material comparison."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('compare', Path(__file__).with_name('compare-material-captures.py'))
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class MaterialCaptureIdentityTests(unittest.TestCase):
    def setUp(self):
        self.draw = dict(meshId='mesh', world=[1, 0, 0, 1], pose=[], coverageFlags=11,
                         alphaCutoff=.5, contentSha256=[1, 2, 3], artifactSchema=9,
                         lattice=dict(slot=4, features=127, coverage=1, uniformBytes=[0, 1],
                                      textures=[dict(slot=0, assetId='texture', colorSpace=2)]))

    def identity(self, draws):
        return compare.material_draws(dict(draws=draws))

    def test_publication_order_and_runtime_slot_do_not_change_input(self):
        second = copy.deepcopy(self.draw)
        second['world'] = [1, 0, 3, 1]
        reordered = copy.deepcopy([second, self.draw])
        reordered[0]['lattice']['slot'] = 73
        self.assertEqual(self.identity([self.draw, second]), self.identity(reordered))

    def test_repeated_instances_are_not_discarded(self):
        self.assertNotEqual(self.identity([self.draw]), self.identity([self.draw, self.draw]))

    def test_geometry_and_coverage_mutations_change_input(self):
        for field, value in [('alphaCutoff', .6), ('coverageFlags', 9),
                             ('contentSha256', [1, 2, 4]), ('artifactSchema', 10)]:
            with self.subTest(field=field):
                changed = copy.deepcopy(self.draw)
                changed[field] = value
                self.assertNotEqual(self.identity([self.draw]), self.identity([changed]))

    def test_material_value_and_texture_mutations_change_input(self):
        for field, value in [('uniformBytes', [0, 2]),
                             ('textures', [dict(slot=0, assetId='other', colorSpace=2)])]:
            with self.subTest(field=field):
                changed = copy.deepcopy(self.draw)
                changed['lattice'][field] = value
                self.assertNotEqual(self.identity([self.draw]), self.identity([changed]))


if __name__ == '__main__':
    unittest.main()
