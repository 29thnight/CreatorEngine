"""CPU-only contracts for GPU timeline conversion; no RHI or Editor launch."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('gpu_timeline', Path(__file__).with_name('attach-gpu-timeline.py'))
gpu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gpu)


class Reader:
    def __init__(self, capture):
        self.capture = capture

    def read_capture(self, _):
        return self.capture

    @staticmethod
    def sample_key(value):
        return (value['engineFrame'], value['marker'], value['begin'], value['end'],
                value['queue'], value['eventSubmission'], value['eventView'])


class GpuTimelineTests(unittest.TestCase):
    def setUp(self):
        self.event = dict(begin=2000, end=2500, marker=1, queue=0, submission=7, view=2,
                          flags=4, slot=16)
        self.measurement = dict(engineFrame=13, axis=2, begin=2000, end=2500, marker=1,
                                queue=0, eventSubmission=7, eventView=2,
                                submissionId=(1 << 60) + 7, realFrameId=88, viewId=2,
                                frameKind=1, generatedOrdinal=0)
        self.capture = dict(complete=True, unacked=0, losses={'droppedEvents': 0},
                            sha256='identity', hz=1000, threads={16: '[GPU Graphics]'},
                            markers=[dict(name='unused'), dict(name='Pass [gpu scope 1]')],
                            frames=[dict(id=13, events=[self.event], measurements=[self.measurement])])
        self.cpu = dict(nativeSha256='identity', hz=1000, mode='etw',
                        alignment=dict(qpcAnchor=1000, utcOffsetMs=7),
                        windows=[dict(name='on', begin=0, end=10000)])

    def extract(self):
        return gpu.extract(Reader(self.capture), Path('unused.ceprof'), self.cpu)

    def test_calibration_and_full_identity(self):
        data, summary = self.extract()
        event = data['events'][0]
        self.assertEqual(event[:2], [1007, 500])
        self.assertEqual(event[5], str((1 << 60) + 7))
        self.assertEqual(event[6:8], [13, '88'])
        self.assertEqual(event[10:12], ['2000', '2500'])
        self.assertEqual(summary['exactProvenanceMatches'], 1)

    def test_zero_duration_is_preserved(self):
        self.event['end'] = self.measurement['end'] = 2000
        data, summary = self.extract()
        self.assertEqual(data['events'][0][1], 0)
        self.assertEqual(summary['zeroDurationSpans'], 1)

    def test_missing_or_wrong_submission_is_rejected(self):
        self.measurement['eventSubmission'] = 8
        with self.assertRaises(ValueError):
            self.extract()

    def test_capture_identity_mismatch_is_rejected(self):
        self.cpu['nativeSha256'] = 'other'
        with self.assertRaises(ValueError):
            self.extract()

    def test_loss_is_not_silently_accepted(self):
        self.capture['losses']['droppedEvents'] = 1
        with self.assertRaises(ValueError):
            self.extract()

    def test_duplicate_identity_is_rejected(self):
        self.capture['frames'][0]['events'].append(copy.copy(self.event))
        with self.assertRaises(ValueError):
            self.extract()

    def test_unsafe_cpu_viewer_identity_is_rejected(self):
        self.measurement['realFrameId'] = (1 << 60) + 1
        with self.assertRaises(ValueError):
            self.extract()

    def test_partial_gpu_event_is_rejected(self):
        self.event['flags'] |= 1
        with self.assertRaises(ValueError):
            self.extract()

    def test_boundary_submission_is_excluded_from_statistics(self):
        self.cpu['windows'][0]['end'] = 1200
        data, summary = self.extract()
        self.assertEqual(len(data['events']), 1)
        self.assertEqual(summary['views'], {})
        self.assertEqual(summary['passMs'], [])

    def test_union_is_not_sum_and_queues_stay_separate(self):
        self.assertEqual(gpu.union_ms([(0, 2), (1, 3), (4, 5)]), 4)
        event = dict(self.event, queue=1, submission=8)
        measurement = dict(self.measurement, queue=1, eventSubmission=8, submissionId=8)
        self.capture['frames'][0]['events'].append(event)
        self.capture['frames'][0]['measurements'].append(measurement)
        data, summary = self.extract()
        self.assertEqual({event[3] for event in data['events']}, {0, 1})
        self.assertEqual(summary['views']['2']['submissions'], 2)


if __name__ == '__main__':
    unittest.main()
