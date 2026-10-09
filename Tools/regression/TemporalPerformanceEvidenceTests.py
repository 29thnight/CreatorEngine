"""Authored byte-level reader fixtures; not executed in this change.

Synthetic bytes test rejection/codec contracts only, never GPU acceptance.
"""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib


spec = importlib.util.spec_from_file_location('material_summary', Path(__file__).with_name('summarize-material-profile.py'))
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


def pack(fmt, *values):
    return struct.pack('<' + fmt, *values)


def string(value):
    encoded = value.encode('utf-8')
    return pack('I', len(encoded)) + encoded


def measured(generated=False, width=640, spatial=True, event_view=4):
    return pack('IBBHIIQQQBBBBQQQQIIIIIBBBB',
                7, 2, 0, event_view, 99, 1, 99, 110, 160,
                2 if generated else 1, 1, 0, 1 if generated else 0,
                17, 4, 2, 23, 1 if generated else 0, width, 480, 640, 480,
                1, int(spatial), 0, 0)


def gpu_event():
    return pack('QQIIHHBBIHHQQQ', 110, 160, 1, 7, 1, 0, 4, 0, 99, 4, 0, 0, 0, 0)


def presenter(sequence=1, tick=170, valid=True, native_code=0):
    return pack('IBQQQQQQQQQqIBBBBBBIB', 7, 3, tick, sequence, 17, 23, 4, 2,
                9, 9, 0, native_code, 0, 0, 0, 0, 0, int(valid), 1, 0, 0)


def snapshot(version=4, samples=None, empty=False):
    samples = [measured()] if samples is None else samples
    count = 0 if empty else 1
    chunks = [
        (1, 1, pack('QBI', 1000, 1, 0)),
        (2, 1, pack('I', 2) + pack('BI', 0, 0) + string('') + string('') +
         pack('BI', 1, 1) + string('GPU.Pass') + string('fixture.cpp')),
        (3, 2, pack('I', 1) + pack('IIBI', 1, 123, 0, 0) + string('RenderThread')),
        (4, 2, pack('I', count) + (b'' if empty else pack('IQQQI', 7, 100, 200, 0, 1) + gpu_event())),
        (5, 2, pack('QI', 0, count) + (b'' if empty else pack('II', 7, 0))),
        (6, 1, pack('I', 0))]
    if version >= 4:
        chunks.append((7, 2 if version >= 6 else 1,
                       pack('I', count) + (b'' if empty else pack('II', 7, len(samples)) + b''.join(samples))))
    offset = 16 + len(chunks) * 32
    table, bodies = [], []
    for kind, chunk_version, data in chunks:
        table.append(pack('IIQQII', kind, chunk_version, offset, len(data), zlib.crc32(data), 0))
        bodies.append(data)
        offset += len(data)
    return b'CEPROF\0\0' + pack('II', version, len(chunks)) + b''.join(table + bodies)


def record(kind, serial, payload):
    header = pack('IIQQI', 0x4b435043, kind, serial, len(payload), zlib.crc32(payload))
    return header + pack('I', zlib.crc32(header)) + payload


def recording(version=5, finalized=True, samples=None):
    frame = pack('IQQQII', 7, 100, 200, 0, 1, 0) + gpu_event()
    if version >= 5:
        samples = [measured()] if samples is None else samples
        frame += pack('I', len(samples)) + b''.join(samples)
    value = b'CEPROF\0\0' + pack('II', version, 0)
    value += record(1, 0, snapshot(version - 1, empty=True))
    value += record(2, 1, frame)
    if finalized:
        value += record(3, 2, pack('BI' + 'Q' * 12, 1, 0, 1, 1, 100, 200, 0, 0, 0, 0, 0, 0, 0, 0))
    return value


class TemporalPerformanceEvidenceTests(unittest.TestCase):
    def test_presenter_registration_precedes_recording_admission(self):
        # Source-order guard for the first-use/Stop race. This is not an
        # executed concurrency proof; owner registration must precede both
        # generation and state sampling so Stop cannot miss an admitted owner.
        source = (Path(__file__).resolve().parents[2] /
                  'Engine/EngineDiagnostics/ProfileService.cpp').read_text(encoding='utf-8-sig')
        body = source.split('void profiler_service::publish_presenter_return(', 1)[1].split(
            'void profiler_service::retire_gpu_lane()', 1)[0]
        registration = body.index('current_stream()')
        self.assertLess(registration, body.index('m_generation.load('))
        self.assertLess(registration, body.index('m_state.load('))
        self.assertLess(body.index('m_state.load('), body.index('write_presenter_return('))
        self.assertLess(body.index('expected_generation == 0'), body.index('write_presenter_return('))
        self.assertLess(body.index('expected_generation != generation'), body.index('write_presenter_return('))
        owner = (Path(__file__).resolve().parents[2] / 'Player/PlayerTemporalDX12.cpp').read_text(encoding='utf-8-sig')
        owner = owner.split('static void PublishPresent(', 1)[1].split('void Publish(', 1)[0]
        self.assertLess(owner.index('profiler.capture_generation()'), owner.index('profiler.state()'))
        self.assertLess(owner.index('profiler.state()'), owner.index('ce::profiler_service::now()'))
        self.assertIn('publish_presenter_return(measured, expectedGeneration)', owner)

    def analyze_bytes(self, value):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.ceprof'
            path.write_bytes(value)
            return summary.analyze(path)

    def test_current_snapshot_and_recording(self):
        for value in (snapshot(), recording()):
            report = self.analyze_bytes(value)
            self.assertTrue(report['nativeRealPerformanceGateEligible'])
            self.assertTrue(report['nativeQualityGateEligible'])
            self.assertFalse(report['pixelAcceptance'])
            self.assertFalse(report['presentationPacingAcceptance'])

    def test_legacy_never_gains_provenance(self):
        for value in (snapshot(2), recording(3)):
            self.assertFalse(self.analyze_bytes(value)['temporalPerformanceGateEligible'])

    def test_presenter_roundtrip_is_separate_from_render_and_display(self):
        self.assertEqual(len(presenter()), 100)
        samples = [measured(), presenter()]
        for value in (snapshot(6, samples=samples), recording(7, samples=samples)):
            report = self.analyze_bytes(value)
            self.assertTrue(report['nativeRealPerformanceGateEligible'])
            self.assertEqual(report['renderMeasurementCount'], 1)
            self.assertEqual(report['presenterReturns']['samples'], 1)
            self.assertTrue(report['presenterReturns']['completeWithinObservedBounds'])
            self.assertFalse(report['presenterReturns']['osDisplayedTimestampsAvailable'])
            self.assertFalse(report['presentationPacingAcceptance'])

    def test_legacy_cannot_carry_presenter_axis(self):
        for value in (snapshot(4, samples=[measured(), presenter()]),
                      recording(5, samples=[measured(), presenter()])):
            with self.assertRaises(ValueError):
                self.analyze_bytes(value)

    def test_presenter_loss_duplicate_clock_and_failed_result(self):
        gap = self.analyze_bytes(snapshot(6, samples=[measured(), presenter(1), presenter(3, 180)]))
        self.assertEqual(gap['presenterReturns']['missingSequences'], 1)
        self.assertFalse(gap['presenterReturns']['completeWithinObservedBounds'])
        reversed_clock = self.analyze_bytes(snapshot(6, samples=[measured(), presenter(1, 180), presenter(2, 170)]))
        self.assertFalse(reversed_clock['presenterReturns']['completeWithinObservedBounds'])
        with self.assertRaises(ValueError):
            self.analyze_bytes(snapshot(6, samples=[measured(), presenter(), presenter()]))
        decoded = summary.measurement(summary.Reader(presenter(native_code=-1)), True)
        self.assertEqual(decoded['nativeCode'], -1)
        self.assertEqual(decoded['clockDomain'], 1)
        with self.assertRaises(ValueError):
            summary.measurement(summary.Reader(presenter()[:-1]), True)

    def test_presenter_vocabulary_and_unknown_identity(self):
        for offset, value in ((89, 4), (90, 14), (91, 9), (92, 2), (93, 2), (94, 0), (95, 1)):
            malformed = bytearray(presenter())
            malformed[offset] = value
            with self.assertRaises(ValueError):
                summary.measurement(summary.Reader(malformed), True)
        missing = bytearray(presenter())
        missing[21:29] = bytes(8)
        with self.assertRaises(ValueError):
            summary.measurement(summary.Reader(missing), True)
        missing[93] = 0
        unknown = summary.measurement(summary.Reader(missing), True)
        self.assertEqual(unknown['realFrameId'], 0)
        self.assertEqual(unknown['valid'], 0)

    def test_missing_unknown_ambiguous_and_generated_proof(self):
        for samples in ([], [measured(spatial=False)], [measured(), measured()], [measured(generated=True)]):
            self.assertFalse(self.analyze_bytes(snapshot(samples=samples))['nativeRealPerformanceGateEligible'])

    def test_extent_and_identity_mutations_are_rejected(self):
        for sample in (measured(width=0), measured(event_view=5)):
            with self.assertRaises(ValueError):
                self.analyze_bytes(snapshot(samples=[sample]))

    def test_recording_must_be_finalized_and_crc_valid(self):
        for value in (recording(finalized=False), recording()[:-1], snapshot()[:-1] + b'\xff'):
            with self.assertRaises(ValueError):
                self.analyze_bytes(value)


if __name__ == '__main__':
    unittest.main()
