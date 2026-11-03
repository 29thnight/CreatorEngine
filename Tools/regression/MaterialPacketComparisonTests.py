"""Frame-local renumbering must not hide material, geometry or packet mutations."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('compare_packets', Path(__file__).with_name('compare-material-packets.py'))
compare_packets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare_packets)


class MaterialPacketComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self, name, reverse=False, change=None):
        path = self.root / name
        path.mkdir()
        ids = [2, 1] if reverse else [1, 2]
        slot = 99 if reverse else 10
        records = [dict(record=0)] + [dict(record=index + 1, meshId=str(key), meshDomain='aggregate',
                   materialSlot=slot, lodIndex=0, chunkIndex=0, worldColumns=[0.] * 12) for index, key in enumerate(ids)]
        material = dict(slot=slot, graphId='material', generation=1, features=0, coverage=0,
                        uniformBytes=[1, 2], textures=[dict(assetId='texture')])
        data = dict(sourceFrameId=1, encoding='uint32-le-bucket-record-tile', file='packets.bin', count=2,
                    capacity=4, width=16, height=8, recordCount=3, recordBuckets=[4294967295, 0, 0],
                    records=records, buckets=[dict(bucket=0, offsetPackets=0, count=2)])
        rows = [(0, ids.index(1) + 1, 0), (0, ids.index(2) + 1, 1)]
        if change == 'uniform': material['uniformBytes'][0] = 3
        if change == 'texture': material['textures'][0]['assetId'] = 'different'
        if change == 'world': records[1]['worldColumns'][3] = 1.
        if change == 'tile': rows[1] = (rows[1][0], rows[1][1], 0)
        if change == 'duplicate': records[2] = dict(records[1], record=2)
        draws = [] if change == 'missing_material' else [dict(lattice=material)]
        (path / 'manifest.json').write_text(json.dumps(dict(frameId=1, gpuMaterialPackets=data, draws=draws)))
        (path / 'packets.bin').write_bytes(b''.join(struct.pack('<III', *row) for row in rows))
        return path

    def test_renumbering(self):
        self.assertTrue(compare_packets.compare(self.fixture('a'), self.fixture('b', True))['passed'])

    def test_changes_are_not_normalized_away(self):
        left = self.fixture('reference')
        for change in ('uniform', 'texture', 'world', 'tile'):
            with self.subTest(change=change):
                self.assertFalse(compare_packets.compare(left, self.fixture(change, True, change))['passed'])

    def test_ambiguous_or_missing_identity(self):
        left = self.fixture('reference')
        for change in ('duplicate', 'missing_material'):
            with self.subTest(change=change):
                with self.assertRaises(ValueError):
                    compare_packets.compare(left, self.fixture(change, True, change))


if __name__ == '__main__':
    unittest.main()
