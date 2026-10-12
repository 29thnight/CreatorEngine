import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from artifact_storage import prune_verified_mutation, require_capacity


class StorageBoundaries(unittest.TestCase):
    def test_hardlinked_source_and_verdict_survive(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, case = Path(tmp)/'source', Path(tmp)/'case'
            source.mkdir(); case.mkdir()
            raw = source/'depth.f32'; raw.write_bytes(b'original pixels')
            before = hashlib.sha256(raw.read_bytes()).hexdigest()
            os.link(raw, case/'depth.f32')
            (case/'manifest.json').write_text('{}')
            prune_verified_mutation(case, source)
            self.assertFalse((case/'depth.f32').exists())
            self.assertTrue((case/'manifest.json').exists())
            self.assertEqual(hashlib.sha256(raw.read_bytes()).hexdigest(), before)
            self.assertEqual(json.loads((case/'pixel-retention.json').read_text())['state'], 'completed')

    def test_source_cleanup_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root/'depth.f32').write_bytes(b'keep')
            with self.assertRaises(ValueError): prune_verified_mutation(root, root)
            self.assertTrue((root/'depth.f32').exists())

    def test_source_child_cleanup_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            child = Path(tmp)/'child'; child.mkdir()
            with self.assertRaises(ValueError): prune_verified_mutation(child, tmp)

    def test_source_parent_cleanup_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            child = Path(tmp)/'child'; child.mkdir()
            with self.assertRaises(ValueError): prune_verified_mutation(tmp, child)

    def test_capacity_is_checked_for_missing_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            with patch('artifact_storage.shutil.disk_usage', return_value=type('Usage', (), {'free': 1})()):
                with self.assertRaises(ValueError): require_capacity(Path(tmp)/'missing', 30)


if __name__ == '__main__': unittest.main()
