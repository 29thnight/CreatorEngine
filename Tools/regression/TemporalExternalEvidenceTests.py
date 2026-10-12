"""External importer contracts; synthetic instruments never prove hardware acceptance.

Optional native Player captures exercise exact repeated-Present association with
the real reader. Originals are hash checked and never changed.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('external_evidence',
    Path(__file__).resolve().parents[1] / 'temporal-validation/analyze-temporal-evidence.py')
external = importlib.util.module_from_spec(spec)
spec.loader.exec_module(external)


class FixtureProfiler:
    """Test double at the decoded-profile boundary, not a valid .ceprof file."""
    def __init__(self, profiles):
        self.profiles = profiles

    def read_capture(self, path):
        return self.profiles[path.name]

    def analyze(self, path):
        return dict(realFramePerformanceGateEligible=True, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    presenterReturns=dict(completeWithinObservedBounds=True))


class ExternalEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.profiles, self.rows = {}, {}
        self.manifest = dict(schema='temporal.external-measurement.v1', kind='pacing',
            source=dict(instrument='SYNTHETIC CONTRACT FIXTURE', instrumentId='no-hardware', toolVersion='test',
                        clockId='synthetic', frameAssociationMethod='test exact sequence', method='sdk-present-timestamps',
                        timestampUnit='ms', axis='sdk-presentation'),
            acceptance=dict(minimumSamples=20, maximumOnP95IntervalMs=20, maximumOnIntervalMs=20), runs=[])
        for enabled in (False, True):
            name = 'on' if enabled else 'off'
            measurements, rows = [], []
            for index in range(24):
                # Two presenter attempts per real source, each with its own sequence.
                key = dict(realFrameId=1 + index // 2, publicationFrameId=100 + index // 2, viewId=1, sceneEpoch=2)
                measurements.append(dict(key, axis=1, renderWidth=640, renderHeight=480, displayWidth=640,
                    displayHeight=480, upscaler=0, resolutionState=1, spatialMode=0, deepDvcApplied=False))
                measurements.append(dict(key, axis=3, valid=True, sequence=index + 1,
                    provider=int(enabled), faultMode=0, nativeGateActive=False, status=0, nativeCode=0,
                    requestGeneration=9, playerObservedGeneration=9, interpolatedFrameCount=int(enabled), faultRevision=0))
                rows.append(dict(key, sampleId=str(index), presenterSequence=index + 1,
                    frameKind='generated' if enabled else 'real', generatedOrdinal=int(enabled),
                    presentationTimestampMs=index * 10))
            self.profiles[name + '.profile'] = dict(version=7, frames=[dict(measurements=measurements)])
            self.rows[name] = rows
            self.manifest['runs'].append(dict(runId=name, fgEnabled=enabled, conversion='synthetic test rows',
                profile=self.write(name + '.profile', b'DECODED TEST DOUBLE, NOT CEPROF'),
                presenterObservations=self.write(name + '.jsonl', b'SYNTHETIC'),
                rawArtifact=self.write(name + '.raw', b'SYNTHETIC, NOT INSTRUMENT EVIDENCE')))
        self.profiler = FixtureProfiler(self.profiles)

    def write(self, name, value):
        (self.root / name).write_bytes(value)
        return dict(path=name, sha256=hashlib.sha256(value).hexdigest())

    def analyze(self):
        for run in self.manifest['runs']:
            name = run['runId']
            path = self.root / (name + '.csv')
            with path.open('w', newline='', encoding='utf-8') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(self.rows[name][0]))
                writer.writeheader()
                writer.writerows(self.rows[name])
            run['samples'] = dict(path=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        path = self.root / 'manifest.json'
        path.write_text(json.dumps(self.manifest), encoding='utf-8')
        with patch.object(external, 'load_profiler', return_value=self.profiler):
            return external.analyze(path)

    def reject(self, message):
        with self.assertRaisesRegex(ValueError, message):
            self.analyze()

    def test_exact_repeated_attempts(self):
        result = self.analyze()
        self.assertTrue(result['passed'])
        self.assertFalse(result['physicalDisplayEvidence'])
        self.assertFalse(result['latencyAcceptance'])
        self.assertEqual(result['runs']['on']['distributionMs']['samples'], 23)

    def test_missing_sequence_is_ambiguous(self):
        del self.rows['off'][0]['presenterSequence']
        # Drop the column for every row, as a legacy CSV would.
        for row in self.rows['off'][1:]: del row['presenterSequence']
        self.reject('Ambiguous repeated presenter')

    def test_sequence_cannot_select_different_source(self):
        self.rows['off'][0]['presenterSequence'] = 3
        self.reject('does not match the recorded source')

    def test_zero_sequence(self):
        self.rows['off'][0]['presenterSequence'] = 0
        self.reject('Missing presenter sequence')

    def test_unique_legacy_association(self):
        for name in ('off', 'on'):
            samples = self.profiles[name + '.profile']['frames'][0]['measurements']
            for index in range(24):
                for value in (self.rows[name][index], samples[index * 2], samples[index * 2 + 1]):
                    value['realFrameId'] = index + 1
                    value['publicationFrameId'] = index + 100
                del self.rows[name][index]['presenterSequence']
        self.assertTrue(self.analyze()['passed'])

    def test_duplicate_presentation(self):
        self.rows['on'][1].update(self.rows['on'][0], sampleId='different')
        self.reject('Duplicate observed presentation')

    def test_generated_without_fg(self):
        self.rows['off'][0].update(frameKind='generated', generatedOrdinal=1)
        self.reject('Invalid observed presentation identity')

    def test_generated_as_real(self):
        self.rows['on'][0]['frameKind'] = 'real'
        self.reject('Invalid observed presentation identity')

    def test_generated_ordinal_exceeds_count(self):
        self.rows['on'][0]['generatedOrdinal'] = 2
        self.reject('Invalid observed presentation identity')

    def test_fg_on_needs_observed_generated(self):
        for row in self.rows['on']: row.update(frameKind='real', generatedOrdinal=0)
        self.reject('no observed generated presentations')

    def test_faulted_presenter(self):
        self.profiles['on.profile']['frames'][0]['measurements'][1]['faultMode'] = 1
        self.reject('failed, faulted')

    def test_mismatched_generation(self):
        self.profiles['on.profile']['frames'][0]['measurements'][1]['playerObservedGeneration'] = 8
        self.reject('mismatched source/presenter generations')

    def test_native_capture_override(self):
        self.profiles['on.profile']['frames'][0]['measurements'][1]['nativeGateActive'] = True
        self.reject('capture-overridden')

    def test_nonmonotonic_timestamp(self):
        self.rows['on'][1]['presentationTimestampMs'] = 0
        self.reject('strictly increasing')

    def test_nonfinite_timestamp(self):
        self.rows['on'][0]['presentationTimestampMs'] = 'nan'
        self.reject('Nonfinite')

    def test_hash_mismatch(self):
        self.manifest['runs'][0]['rawArtifact']['sha256'] = '0' * 64
        self.reject('SHA256 mismatch')

    def test_incomplete_presenter_stream(self):
        original = self.profiler.analyze
        def damaged(path):
            result = original(path)
            result['presenterReturns']['completeWithinObservedBounds'] = False
            return result
        self.profiler.analyze = damaged
        self.reject('Continuous presenter stream')

    def test_ineligible_profile(self):
        original = self.profiler.analyze
        def damaged(path):
            result = original(path)
            result['realFramePerformanceGateEligible'] = False
            return result
        self.profiler.analyze = damaged
        self.reject('incomplete/ambiguous/non-real provenance')

    def test_unmatched_renderer_identity(self):
        self.rows['on'][0]['realFrameId'] = 9999
        self.reject('not linked to this run')

    def test_duplicate_sample(self):
        self.rows['on'][1]['sampleId'] = self.rows['on'][0]['sampleId']
        self.reject('duplicate sample identity')

    def test_mixed_extent(self):
        self.profiles['on.profile']['frames'][0]['measurements'][0]['renderWidth'] = 320
        self.profiles['on.profile']['frames'][0]['measurements'][2]['renderWidth'] = 320
        self.reject('Mixed views/settings/extents')

    def test_mixed_scene(self):
        for item in (self.rows['on'][0], *self.profiles['on.profile']['frames'][0]['measurements'][:2]):
            item['sceneEpoch'] = 3
        self.reject('crosses view or scene')

    def test_off_on_settings_differ(self):
        for sample in self.profiles['on.profile']['frames'][0]['measurements']:
            if sample['axis'] == 1: sample['upscaler'] = 1
        self.reject('changed resolution, reconstruction')

    def test_fractional_minimum(self):
        self.manifest['acceptance']['minimumSamples'] = 20.5
        self.reject('JSON integer')

    def test_cpu_return_is_not_pacing(self):
        self.manifest['source'].update(method='cpu-present-return', axis='cpu-return')
        self.reject('individual observed presentations')

    def test_sdk_timestamps_are_not_photons(self):
        self.manifest.update(kind='latency')
        self.manifest['source'].update(axis='input-to-photon', sharedClock=True, clockUncertaintyMs=0)
        self.reject('physical input/photon')

    def test_over_budget(self):
        self.manifest['acceptance'].update(maximumOnP95IntervalMs=5, maximumOnIntervalMs=5)
        self.assertFalse(self.analyze()['passed'])


def native_association(path):
    before = hashlib.sha256(path.read_bytes()).hexdigest()
    profiler = external.load_profiler()
    summary = profiler.analyze(path)
    assert summary['nativeRealPerformanceGateEligible']
    profile = profiler.read_capture(path)
    observations = external.recorded_presenters(profile, summary)
    repeated, count = 0, 0
    for key, attempts in observations.items():
        row = dict(zip(('realFrameId', 'publicationFrameId', 'viewId', 'sceneEpoch'), key))
        for attempt in attempts:
            count += 1
            row['presenterSequence'] = str(attempt['presenterSequence'])
            assert external.presenter_for_row(observations, row, key) == attempt
        if len(attempts) > 1:
            repeated += 1
            row.pop('presenterSequence')
            try:
                external.presenter_for_row(observations, row, key)
            except ValueError as error:
                assert 'Ambiguous repeated presenter' in str(error)
            else:
                raise AssertionError('Ambiguous native Present attempt accepted')
    assert repeated > 0, 'Native fixture must exercise actual repeated Present'
    # Exercise the full importer with the real reader: relabelling a native
    # recording as FG-on must fail even with claimed generated CSV rows.
    real_keys = {external.identity(sample) for frame in profile['frames']
                 for sample in frame['measurements'] if sample['axis'] != 3}
    linked = [(key, attempt) for key, attempts in observations.items() if key in real_keys for attempt in attempts]
    linked.sort(key=lambda item: item[1]['presenterSequence'])
    assert len(linked) > 20
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        def reference(file):
            return dict(path=str(file.resolve()), sha256=hashlib.sha256(file.read_bytes()).hexdigest())
        raw = root / 'synthetic.raw'
        raw.write_text('Synthetic association rejection fixture, not an instrument capture', encoding='utf-8')
        manifest = dict(schema='temporal.external-measurement.v1', kind='pacing',
            source=dict(instrument='SYNTHETIC REJECTION FIXTURE', instrumentId='none', toolVersion='test',
                clockId='synthetic', frameAssociationMethod='exact recorded sequence', method='sdk-present-timestamps',
                timestampUnit='ms', axis='sdk-presentation'),
            acceptance=dict(minimumSamples=20, maximumOnP95IntervalMs=20, maximumOnIntervalMs=20), runs=[])
        for enabled in (False, True):
            rows = [dict(zip(('realFrameId', 'publicationFrameId', 'viewId', 'sceneEpoch'), key),
                         sampleId=str(index), presenterSequence=attempt['presenterSequence'],
                         frameKind='generated' if enabled else 'real', generatedOrdinal=int(enabled),
                         presentationTimestampMs=index * 10)
                    for index, (key, attempt) in enumerate(linked)]
            csv_path = root / ('on.csv' if enabled else 'off.csv')
            with csv_path.open('w', newline='', encoding='utf-8') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            manifest['runs'].append(dict(runId='on' if enabled else 'off', fgEnabled=enabled,
                profile=reference(path), presenterObservations=reference(raw), rawArtifact=reference(raw),
                samples=reference(csv_path), conversion='synthetic rejection fixture'))
        manifest_path = root / 'manifest.json'
        manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
        try:
            external.analyze(manifest_path)
        except ValueError as error:
            assert 'Actual presenter mode disagrees' in str(error), str(error)
        else:
            raise AssertionError('Native capture accepted as actual FG-on output')
    assert before == hashlib.sha256(path.read_bytes()).hexdigest()
    return dict(path=str(path.resolve()), sha256=before, exactAttempts=count,
                repeatedSourceIdentities=repeated, nativeRelabelledFgRejected=True,
                physicalAcceptance=False, generatedOutputAcceptance=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ExternalEvidenceTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful(): raise SystemExit(1)
    report = dict(passed=True, contractTests=result.testsRun,
        nativeAssociation=[native_association(path) for path in args.native],
        physicalAcceptance=False, generatedOutputAcceptance=False,
        scope='synthetic importer contracts and actual native repeated-Present association; no external instrument acceptance')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
