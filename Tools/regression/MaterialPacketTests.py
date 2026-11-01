"""Packet diagnostics reject corrupted identity, ranges and duplicate work."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest


spec = importlib.util.spec_from_file_location('packets', Path(__file__).with_name('verify-material-packets.py'))
packets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packets)


class MaterialPacketTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = dict(sourceFrameId=7, encoding='uint32-le-bucket-record-tile', file='packets.bin',
                         count=3, capacity=4, width=16, height=8, recordCount=4, recordBuckets=[4294967295, 0, 0, 0],
                         buckets=[dict(bucket=0, offsetPackets=0, count=3)])
        self.rows = [(0, 1, 0), (0, 2, 0), (0, 3, 1)]

    def inspect(self):
        (self.root / 'manifest.json').write_text(json.dumps(dict(frameId=7, gpuMaterialPackets=self.data)))
        (self.root / 'packets.bin').write_bytes(b''.join(struct.pack('<III', *row) for row in self.rows))
        return packets.inspect_capture(self.root)

    def test_density(self):
        result = self.inspect()
        self.assertEqual(result['uniqueBucketTiles'], 2)
        self.assertEqual(result['additionalRecordsInSameBucketTile'], 1)
        self.assertEqual(result['maxRecordsPerBucketTile'], 2)

    def test_empty_bucket(self):
        self.data['buckets'].append(dict(bucket=1, offsetPackets=3, count=0))
        self.assertTrue(self.inspect()['passed'])

    def test_metadata_mutations(self):
        for field, value in [('sourceFrameId', 8), ('count', 4), ('capacity', 2),
                             ('file', '../escape.bin'), ('width', 0), ('encoding', 'native')]:
            with self.subTest(field=field):
                old = self.data[field]
                self.data[field] = value
                with self.assertRaises(ValueError):
                    self.inspect()
                self.data[field] = old

    def test_packet_mutations(self):
        for row in [(0, 1, 0), (1, 2, 0), (0, 4, 0), (0, 2, 2), (0, 0, 0)]:
            with self.subTest(row=row):
                self.rows[1] = row
                with self.assertRaises(ValueError):
                    self.inspect()

    def test_wrong_material_bucket(self):
        self.data['recordBuckets'][2] = 1
        with self.assertRaises(ValueError):
            self.inspect()

    def test_range_mutations(self):
        for field, value in [('bucket', 1), ('offsetPackets', 1), ('count', 2), ('count', 4), ('count', -1)]:
            with self.subTest(field=field, value=value):
                old = self.data['buckets'][0][field]
                self.data['buckets'][0][field] = value
                with self.assertRaises(ValueError):
                    self.inspect()
                self.data['buckets'][0][field] = old


if __name__ == '__main__':
    unittest.main()
