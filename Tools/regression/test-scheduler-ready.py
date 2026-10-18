"""실제 ETW 수집 없이 이벤트 단위 스케줄러 분석의 경계·누락·수명을 검증한다."""

import csv
import importlib.util
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name('analyze-scheduler-ready.py')
SPEC = importlib.util.spec_from_file_location('scheduler_ready', MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def process(pid, instance=None, name=None):
    return dict(name=name or f'process{pid}', pid=pid, instance=instance, lifetimeKnown=instance is not None)


def switch(time, old, new, state='Ready', cpu=0, old_process=None, new_process=None):
    return dict(timeUs=time, oldTid=old, newTid=new, cpu=cpu, oldState=state, reason='test',
                oldPriority=8, newPriority=9, idealCpu=cpu,
                oldProcess=old_process or process(old), newProcess=new_process or process(new))


def fixture():
    return dict(switches=[switch(0, 0, 1), switch(10, 1, 2), switch(40, 2, 1),
                          switch(50, 1, 3, 'Waiting'), switch(80, 3, 1), switch(100, 1, 2)],
                ready=[dict(timeUs=60, targetTid=1, targetProcess=process(1), readierTid=3,
                            readierProcess=process(3), reason='Unwait', increment=1, cpu=0, inDpc=False)],
                priority=[], lifetimes=[])


class SchedulerReadyTests(unittest.TestCase):
    def test_exact_partition_and_preemption(self):
        result = MODULE.analyze(fixture(), 1, 5, 95)
        self.assertAlmostEqual(result['statesMs']['Running'], .030)
        self.assertAlmostEqual(result['statesMs']['Ready'], .050)
        self.assertAlmostEqual(result['statesMs']['Waiting'], .010)
        self.assertEqual(result['statesMs']['Unknown'], 0)
        self.assertAlmostEqual(sum(result['statesMs'].values()), result['wallMs'])
        self.assertEqual(result['readyIntervals'][0]['out']['newTid'], 2)
        self.assertEqual(result['readyIntervals'][1]['readier']['readierTid'], 3)

    def test_clip_left_ready_boundary(self):
        result = MODULE.analyze(fixture(), 1, 20, 35)
        self.assertAlmostEqual(result['statesMs']['Ready'], .015)
        self.assertEqual(result['statesMs']['Running'], 0)
        self.assertEqual(result['readyIntervals'][0]['beginUs'], 20)

    def test_do_not_carry_previous_wait_into_running_window(self):
        result = MODULE.analyze(fixture(), 1, 85, 95)
        self.assertAlmostEqual(result['statesMs']['Running'], .010)
        self.assertEqual(result['statesMs']['Ready'], 0)
        self.assertEqual(result['statesMs']['Waiting'], 0)

    def test_clip_right_wait_boundary(self):
        result = MODULE.analyze(fixture(), 1, 55, 65)
        self.assertAlmostEqual(result['statesMs']['Waiting'], .005)
        self.assertAlmostEqual(result['statesMs']['Ready'], .005)

    def test_missing_notification_is_unknown(self):
        events = fixture()
        events['ready'] = []
        result = MODULE.analyze(events, 1, 55, 75)
        self.assertAlmostEqual(result['statesMs']['Unknown'], .020)
        self.assertFalse(result['stateCoverageComplete'])
        self.assertEqual(result['statesMs']['Ready'], 0)

    def test_truncated_target_boundaries_are_unknown(self):
        events = fixture()
        events['switches'] = events['switches'][1:-1]
        result = MODULE.analyze(events, 1, 5, 95)
        self.assertAlmostEqual(result['statesMs']['Unknown'], .020)
        self.assertFalse(result['stateCoverageComplete'])
        self.assertTrue(result['warnings'])

    def test_multiple_cpus_have_cpu_time_not_wall_time(self):
        events = fixture()
        events['switches'] += [switch(0, 0, 99, cpu=1), switch(100, 99, 0, cpu=1)]
        events['switches'].sort(key=lambda event: event['timeUs'])
        result = MODULE.analyze(events, 1, 5, 95)
        self.assertAlmostEqual(sum(row['cpuMs'] for row in result['allCpuOwners']), .180)
        self.assertAlmostEqual(result['wallMs'], .090)
        self.assertAlmostEqual(sum(row['cpuMs'] for row in result['allCpuOwnersDuringReady']), .100)
        self.assertEqual(result['observedCpus'], [0, 1])

    def test_thread_id_reuse_requires_explicit_interval(self):
        events = fixture()
        events['lifetimes'] = [dict(timeUs=45, kind='T-End', tid=1, process=process(1)),
                               dict(timeUs=46, kind='T-Start', tid=1, process=process(7))]
        with self.assertRaisesRegex(ValueError, 'lifetime changes'):
            MODULE.analyze(events, 1, 5, 95)

    def test_process_instances_do_not_merge_reused_pid(self):
        runs = [dict(beginUs=0, endUs=10, cpu=0, process=process(5, 'first')),
                dict(beginUs=10, endUs=20, cpu=0, process=process(5, 'second'))]
        owners = MODULE.aggregate_owners(runs, [(0, 20)])
        self.assertEqual(len(owners), 2)
        self.assertEqual({row['process']['instance'] for row in owners}, {'first', 'second'})

    def test_context_switch_identity_gap_is_not_assigned(self):
        events = fixture()
        events['switches'][2]['oldTid'] = 7
        result = MODULE.analyze(events, 1, 20, 35)
        self.assertEqual(result['allCpuOwners'], [])
        self.assertTrue(any('identity gap' in warning for warning in result['warnings']))

    def test_parser_lifecycle_and_quoted_process_name(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'events.csv'
            with path.open('w', encoding='utf-8', newline='') as target:
                writer = csv.writer(target)
                writer.writerow(['BeginHeader'])
                writer.writerow(['CSwitch', 'TimeStamp'])
                writer.writerow(['P-Start', 0, 'name,with-comma.exe (5)', 0, 0, 'unique-key'])
                writer.writerow(['CSwitch', 10, 'name,with-comma.exe (5)', 5, 8, -1, 0, 0,
                                 'Idle (0)', 0, 0, -1, 'Running', 'Executive', 'NonSwap', 0, 0, 0])
            event = MODULE.read_events(path)['switches'][0]
            self.assertEqual(event['newProcess']['name'], 'name,with-comma.exe')
            self.assertEqual(event['newProcess']['instance'], '0:unique-key')
            self.assertTrue(event['newProcess']['lifetimeKnown'])

    def test_standby_counts_ready_and_priority_events_preserved(self):
        events = fixture()
        events['switches'][1]['oldState'] = 'Standby'
        events['priority'] = [dict(timeUs=25, targetTid=1, oldPriority=8, newPriority=9)]
        result = MODULE.analyze(events, 1, 20, 35)
        self.assertAlmostEqual(result['statesMs']['Ready'], .015)
        self.assertEqual(result['explicitPriorityChanges'], events['priority'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
