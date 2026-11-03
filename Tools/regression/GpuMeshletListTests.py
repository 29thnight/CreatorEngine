"""Reject corrupt or misattributed GPU accepted-list evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('lists', Path(__file__).with_name('verify-gpu-meshlet-lists.py'))
lists = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lists)


class GpuMeshletListTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='creator-gpu-lists-')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.manifest = dict(frameId=7, gpuMeshletLists=[dict(
            stage='camera-final', sourceFrameId=7, file='ids.u32',
            encoding='uint32-le-local-meshlet-id', spatialCulling='enabled', acceptedMeshlets=2,
            bins=[dict(bin=0, offsetWords=0, count=2, capacity=3, meshId='mesh-a', world=[0.0] * 16),
                  dict(bin=1, offsetWords=2, count=0, capacity=1, meshId='mesh-b', world=[0.0] * 16)])])

    def inspect(self, manifest=None, words=(0, 2)):
        (self.directory / 'manifest.json').write_text(json.dumps(manifest or self.manifest), encoding='utf-8')
        (self.directory / 'ids.u32').write_bytes(struct.pack('<' + 'I' * len(words), *words))
        return lists.inspect_capture(self.directory)

    def test_valid_lists_include_rejected_bins(self):
        stage = self.inspect()['stages'][0]
        self.assertEqual((stage['bins'], stage['nonemptyBins'], stage['acceptedMeshlets']), (2, 1, 2))

    def test_payload_corruption_is_rejected(self):
        for words in [(0,), (0, 0), (0, 3), (0, 2, 1)]:
            with self.subTest(words=words), self.assertRaises(ValueError):
                self.inspect(words=words)

    def test_stage_identity_and_totals_are_checked(self):
        for field, value in [('sourceFrameId', 8), ('acceptedMeshlets', 3),
                             ('spatialCulling', 'unknown'), ('bins', [])]:
            manifest = copy.deepcopy(self.manifest)
            manifest['gpuMeshletLists'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.inspect(manifest)

    def test_bin_layout_and_transform_are_checked(self):
        for field, value in [('offsetWords', 1), ('count', 4), ('capacity', 1),
                             ('world', [float('nan')] * 16)]:
            manifest = copy.deepcopy(self.manifest)
            manifest['gpuMeshletLists'][0]['bins'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.inspect(manifest)

    def test_missing_or_duplicate_stages_are_rejected(self):
        for stages in [[], self.manifest['gpuMeshletLists'] * 2]:
            with self.subTest(stages=len(stages)), self.assertRaises(ValueError):
                self.inspect(dict(frameId=7, gpuMeshletLists=stages))


if __name__ == '__main__':
    unittest.main()
