"""Page diagnostics distinguish real page splits from different shader/raster keys."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest


spec = importlib.util.spec_from_file_location('pages', Path(__file__).with_name('verify-material-packet-pages.py'))
pages = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pages)


class MaterialPacketPageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rows = [(0, 1, 0), (1, 2, 0), (2, 3, 1)]
        self.descriptors = dict(sourceFrameId=7, capacity=64,
            pages=[dict(page=0, vertexCount=2, topologyCount=2), dict(page=1, vertexCount=1, topologyCount=1)],
            records=[dict(record=1, page=0, semanticKey='program-a', doubleSided=False, currentVertex=0, previousVertex=0, topology=0),
                     dict(record=2, page=1, semanticKey='program-a', doubleSided=False, currentVertex=0, previousVertex=0, topology=0),
                     dict(record=3, page=0, semanticKey='program-b', doubleSided=True, currentVertex=1, previousVertex=0, topology=1)])
        self.data = dict(sourceFrameId=7, encoding='uint32-le-bucket-record-tile', file='packets.bin',
                         count=3, capacity=4, width=16, height=8, recordCount=4,
                         recordBuckets=[0xffffffff, 0, 1, 2], descriptorPages=self.descriptors,
                         buckets=[dict(bucket=i, offsetPackets=i, count=1) for i in range(3)])

    def inspect(self):
        (self.root / 'manifest.json').write_text(json.dumps(dict(frameId=7, gpuMaterialPackets=self.data)))
        (self.root / 'packets.bin').write_bytes(b''.join(struct.pack('<III', *row) for row in self.rows))
        return pages.inspect_capture(self.root)

    def test_page_splits(self):
        result = self.inspect()
        self.assertEqual(result['additionalBucketsFromPages'], 1)
        self.assertEqual(result['additionalBucketTilePairsFromPages'], 1)
        self.assertEqual(result['vertexDescriptors'], 3)

    def test_raster_difference_is_not_page_split(self):
        self.descriptors['records'][1]['doubleSided'] = True
        result = self.inspect()
        self.assertEqual(result['additionalBucketsFromPages'], 0)
        self.assertEqual(result['additionalBucketTilePairsFromPages'], 0)

    def test_disjoint_tiles_do_not_duplicate_tile_work(self):
        self.rows[1] = (1, 2, 1)
        result = self.inspect()
        self.assertEqual(result['additionalBucketsFromPages'], 1)
        self.assertEqual(result['additionalBucketTilePairsFromPages'], 0)

    def test_empty_bucket_does_not_add_active_tile_work(self):
        self.rows.pop(1)
        self.data['count'] = 2
        self.data['buckets'][1]['count'] = 0
        self.data['buckets'][2]['offsetPackets'] = 1
        result = self.inspect()
        self.assertEqual(result['additionalBucketsFromPages'], 1)
        self.assertEqual(result['additionalBucketTilePairsFromPages'], 0)
        self.assertEqual(result['bucketTilePairs'], 2)

    def test_page_and_record_mutations(self):
        record = self.descriptors['records'][0]
        for key, value in [('record', 2), ('page', 2), ('currentVertex', 2), ('previousVertex', -1),
                           ('topology', 2), ('semanticKey', ''), ('doubleSided', 1)]:
            with self.subTest(key=key):
                old = record[key]
                record[key] = value
                with self.assertRaises(ValueError):
                    self.inspect()
                record[key] = old

    def test_volume_record_without_raster_page(self):
        self.data['recordCount'] = 5
        self.data['recordBuckets'].append(0xffffffff)
        self.descriptors['records'].append(dict(record=4, page=0xffffffff))
        result = self.inspect()
        self.assertEqual(result['materialBuckets'], 3)
        self.assertEqual(result['additionalBucketsFromPages'], 1)

    def test_material_record_requires_raster_page(self):
        self.descriptors['records'][0]['page'] = 0xffffffff
        with self.assertRaisesRegex(ValueError, 'record/page'):
            self.inspect()

    def test_mixed_page_bucket(self):
        self.data['recordBuckets'][2] = 0
        self.rows[1] = (0, 2, 0)
        self.data['buckets'][0]['count'] = 2
        self.data['buckets'][1].update(offsetPackets=2, count=0)
        with self.assertRaisesRegex(ValueError, 'mixes'):
            self.inspect()

    def test_duplicate_keys(self):
        self.descriptors['records'][1]['page'] = 0
        with self.assertRaisesRegex(ValueError, 'separate buckets'):
            self.inspect()

    def test_frame_capacity_and_missing_records(self):
        for key, value in [('sourceFrameId', 8), ('capacity', 65), ('records', [])]:
            with self.subTest(key=key):
                old = self.descriptors[key]
                self.descriptors[key] = value
                with self.assertRaises(ValueError):
                    self.inspect()
                self.descriptors[key] = old


if __name__ == '__main__':
    unittest.main()
