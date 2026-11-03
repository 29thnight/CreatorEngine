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

    def test_proven_recook_preserves_other_input_checks(self):
        original = copy.deepcopy(self.draw)
        original.update(contentSha256=[1] * 32, artifactBytes=100, resolverRevision=7)
        recooked = copy.deepcopy(original)
        recooked.update(contentSha256=[2] * 32, artifactSchema=10, artifactBytes=136, resolverRevision=8)
        replacement = {bytes(recooked['contentSha256']).hex(): (original['contentSha256'], 100)}

        def canonical(draws):
            return compare.material_draws(dict(draws=draws), replacement)

        self.assertNotEqual(self.identity([original]), self.identity([recooked]))
        self.assertEqual(canonical([original]), canonical([recooked]))
        self.assertNotEqual(canonical([original]), canonical([recooked, recooked]))
        for field, value in [('world', [1, 0, 4, 1]), ('pose', [0, 1]),
                             ('coverageFlags', 0), ('alphaCutoff', .1), ('assetId', 'other')]:
            with self.subTest(field=field):
                changed = copy.deepcopy(recooked)
                changed[field] = value
                self.assertNotEqual(canonical([original]), canonical([changed]))
        changed = copy.deepcopy(recooked)
        changed['lattice']['textures'][0]['assetId'] = 'other'
        self.assertNotEqual(canonical([original]), canonical([changed]))


if __name__ == '__main__':
    unittest.main()
