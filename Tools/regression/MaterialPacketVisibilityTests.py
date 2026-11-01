"""Visibility oracle rejects missing work even when packet structure is valid."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest


spec = importlib.util.spec_from_file_location('oracle', Path(__file__).with_name('verify-material-packet-visibility.py'))
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


class MaterialPacketVisibilityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = dict(sourceFrameId=7, encoding='uint32-le-bucket-record-tile', file='packets.bin',
                         count=3, capacity=128, width=16, height=8, recordCount=3,
                         recordBuckets=[0xffffffff, 0, 0], primitiveRanges=[[0, 0], [1, 129], [129, 257]],
                         buckets=[dict(bucket=0, offsetPackets=0, count=3)],
                         visibility=dict(file='visibility.bin', sourceFrameId=7, width=16, height=8,
                                         encoding='uint32-le-primitive-depth-row-major'))
        self.rows = [(0, 1, 0), (0, 2, 0), (0, 2, 1)]
        self.addresses = [0] * 128
        self.addresses[0], self.addresses[1], self.addresses[8] = 1, 129, 129

    def verify(self):
        self.data['count'] = len(self.rows)
        self.data['buckets'][0]['count'] = len(self.rows)
        (self.root / 'manifest.json').write_text(json.dumps(dict(frameId=7, gpuMaterialPackets=self.data)))
        (self.root / 'packets.bin').write_bytes(b''.join(struct.pack('<III', *row) for row in self.rows))
        (self.root / 'visibility.bin').write_bytes(b''.join(struct.pack('<II', address, 0) for address in self.addresses))
        return oracle.verify_capture(self.root)

    def test_complete_packet_set(self):
        report = self.verify()
        self.assertTrue(report['passed'])
        self.assertEqual(report['expectedPackets'], 3)
        self.assertEqual(report['foregroundPixels'], 3)

    def test_missing_and_extra_work(self):
        self.rows.pop()
        self.assertEqual(self.verify()['missingCount'], 1)
        self.rows += [(0, 2, 1), (0, 1, 1)]
        self.assertEqual(self.verify()['extraCount'], 1)

    def test_invalid_addresses(self):
        for address in (127, 128, 255, 256, 257):
            with self.subTest(address=address):
                self.addresses[0] = address
                with self.assertRaises(ValueError):
                    self.verify()

    def test_gaps_and_overlapping_ranges(self):
        for ranges in ([[0, 0], [1, 129], [128, 256]], [[0, 0], [1, 129], [257, 385]],
                       [[0, 0], [1, 128], [129, 257]]):
            with self.subTest(ranges=ranges):
                self.data['primitiveRanges'] = ranges
                with self.assertRaises(ValueError):
                    self.verify()

    def test_source_identity(self):
        for key, value in [('sourceFrameId', 8), ('encoding', 'float32'), ('width', 8), ('file', '../outside.bin')]:
            with self.subTest(key=key):
                old = self.data['visibility'][key]
                self.data['visibility'][key] = value
                with self.assertRaises(ValueError):
                    self.verify()
                self.data['visibility'][key] = old

    def test_partial_tiles_and_repeated_pixels(self):
        raw = b''.join(struct.pack('<II', 1, 0) for _ in range(9 * 9))
        expected, foreground = oracle.expected_packets(9, 9, raw, [[0, 0], [1, 129]], [0xffffffff, 0], 1)
        self.assertEqual(expected, {(0, 1, tile) for tile in range(4)})
        self.assertEqual(foreground, 81)

    def test_background_and_unassigned_material(self):
        raw = b''.join(struct.pack('<II', address, 0) for address in (0, 0xffffffff, 1))
        expected, foreground = oracle.expected_packets(3, 1, raw, [[0, 0], [1, 129]], [0xffffffff, 0xffffffff], 1)
        self.assertEqual(expected, set())
        self.assertEqual(foreground, 1)

    def test_truncated_payload(self):
        self.addresses.pop()
        with self.assertRaises(ValueError):
            self.verify()


if __name__ == '__main__':
    unittest.main()
