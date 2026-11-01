"""Input budgets remain explicit and never bypass capture format validation."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('reader', Path(__file__).with_name('summarize-material-profile.py'))
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


class CaptureBudgetTests(unittest.TestCase):
    def test_default_rejects_oversize_before_read(self):
        path = Mock()
        path.stat.return_value.st_size = 256 * 1024 * 1024 + 1
        with self.assertRaisesRegex(ValueError, '256 MiB read budget'):
            reader.read_capture(path)
        path.read_bytes.assert_not_called()

    def test_explicit_budget_preserves_format_validation(self):
        path = Mock()
        path.stat.return_value.st_size = 300 * 1024 * 1024
        path.read_bytes.return_value = b'invalid format!!'
        with self.assertRaisesRegex(ValueError, 'Not a CEPROF capture'):
            reader.read_capture(path, 512)
        path.read_bytes.assert_called_once()

    def test_invalid_budget_is_rejected_before_access(self):
        for budget in (0, -1, 1025, 512.5):
            with self.subTest(budget=budget):
                path = Mock()
                with self.assertRaisesRegex(ValueError, 'read budget must be an integer'):
                    reader.read_capture(path, budget)
                path.stat.assert_not_called()
                path.read_bytes.assert_not_called()

    def test_analysis_forwards_the_explicit_budget(self):
        with patch.object(reader, 'read_capture', side_effect=ValueError('sentinel')) as read:
            with self.assertRaisesRegex(ValueError, 'sentinel'):
                reader.analyze('capture.ceprof', max_capture_mib=512)
            read.assert_called_once_with(Path('capture.ceprof'), 512)


if __name__ == '__main__':
    unittest.main()
