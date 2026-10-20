#!/usr/bin/env python3
"""Check material-profile reader compatibility and independent RHI identities.

The fixture writes portable little-endian capture bytes independently of the
reader. Legacy summaries are compared with the pinned pre-change Python reader.
No engine, native GPU, or profiler producer execution is claimed by this test.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import types
import zlib


BASELINE = '088ea7b70c1a610e93fd2e0f6a2f35a2b9a5e175'
READER = 'Tools/regression/summarize-material-profile.py'
RHI_FORMAT = 'IBIIQQHHBBIHQQQQQQIIBBB'
RHI_FIELDS = ('engineFrame', 'axis', 'eventFrame', 'marker', 'begin', 'end', 'slot', 'depth',
              'flags', 'queue', 'eventSubmission', 'eventView', 'recordingId', 'sourceEngineFrame',
              'frameId', 'viewId', 'deviceIncarnation', 'backendGeneration', 'queueId', 'targetIndex',
              'queueKind', 'sourceEngineFrameAvailable', 'reserved')
CHECKS = 0


def check(condition, message):
    global CHECKS
    CHECKS += 1
    if not condition:
        raise AssertionError(message)


def pack(fmt, *values):
    return struct.pack('<' + fmt, *values)


def string(value):
    value = value.encode('utf-8')
    return pack('I', len(value)) + value


def load_module(source, name):
    module = types.ModuleType(name)
    exec(compile(source, name, 'exec'), module.__dict__)
    return module


def wire_event(gpu=False, version=2):
    values = ((125, 130, 2, 42, 1, 2, 4, 2, 0x89abcdef, 0x2345, 0) if gpu else
              (112, 120, 1, 41, 0, 1, 0, 0, 0, 0, 0))
    return pack('QQIIHHBBIHH', *values) + (pack('QQQ', 0, 0, 0) if version == 2 else b'')


def render_measurement(gpu=False):
    key = (42, 2 if gpu else 1, 2 if gpu else 0, 0x2345 if gpu else 0,
           0x89abcdef if gpu else 0, 2 if gpu else 0,
           (1 << 40) + 0x89abcdef, 125 if gpu else 110, 130 if gpu else 120)
    provenance = (1, 1, 0, 0, (1 << 33) + 5, (1 << 40) + 0x2345, 3, 11,
                  0, 640, 480, 640, 480, 1, 1, 0, 0)
    return pack('IBBHIIQQQBBBBQQQQIIIIIBBBB', *key, *provenance)


def presenter_measurement():
    return pack('IBQQQQQQQQQqIBBBBBBIB', 42, 3, 200, 1, 5, 11, 2, 3, 1, 1, 0, 0,
                0, 0, 0, 0, 1, 1, 1, 0, 0)


def rhi_measurement(gpu=False, **changes):
    values = (42, 4, 42 if gpu else 41, 2 if gpu else 1, 125 if gpu else 112,
              130 if gpu else 120, 1 if gpu else 0, 2 if gpu else 1, 4 if gpu else 0,
              2 if gpu else 0, 0x89abcdef if gpu else 0, 0x2345 if gpu else 0,
              (1 << 48) + 7, 0 if gpu else (1 << 41) + 71, (1 << 42) + 1,
              (1 << 43) + 2, 0 if gpu else 9, 5, 0 if gpu else 7,
              0xffffffff if gpu else 4, 0, 0 if gpu else 1, 0)
    record = dict(zip(RHI_FIELDS, values))
    record.update(changes)
    return pack(RHI_FORMAT, *(record[key] for key in RHI_FIELDS))


def snapshot(version, measurements=(), metadata=False, duplicate_event=False, chunk_version=None):
    event_version = 1 if version == 1 else 2
    events = wire_event(version=event_version) + wire_event(True, event_version)
    if duplicate_event:
        events += wire_event(version=event_version)
    frames = pack('I', 0 if metadata else 1)
    if not metadata:
        frames += pack('IQQQI', 42, 100, 200, 0, 3 if duplicate_event else 2) + events
    markers = pack('I', 3)
    for name in ('Unknown', 'CPU.Scope', 'GPU.Pass'):
        markers += pack('BI', 0, 1) + string(name) + string('fixture.cpp')
    threads = pack('I', 2)
    for slot in range(2):
        threads += pack('IIBI', slot, slot + 100, 0, slot) + string('worker' + str(slot))
    counters = pack('QI', 0, 0 if metadata else 1)
    if not metadata:
        counters += pack('II', 42, 0)
    chunks = [(1, 1, pack('QBI', 1000, 1, 0)), (2, 1, markers), (3, event_version, threads),
              (4, event_version, frames), (5, event_version, counters), (6, 1, pack('I', 0))]
    if version >= 4:
        body = pack('I', 0 if metadata else 1)
        if not metadata:
            body += pack('II', 42, len(measurements)) + b''.join(measurements)
        chunks.append((7, chunk_version if chunk_version is not None else {4: 1, 6: 2, 8: 3}[version], body))
    offset = 16 + 32 * len(chunks)
    table = b'CEPROF\0\0' + pack('II', version, len(chunks))
    for kind, chunk, body in chunks:
        table += pack('IIQQII', kind, chunk, offset, len(body), zlib.crc32(body), 0)
        offset += len(body)
    return table + b''.join(body for _, _, body in chunks)


def stream(version, measurements=()):
    bodies = [snapshot(version - 1, metadata=True)]
    frame = pack('IQQQII', 42, 100, 200, 0, 2, 0) + wire_event() + wire_event(True)
    if version >= 5:
        frame += pack('I', len(measurements)) + b''.join(measurements)
    bodies += [frame, pack('BI' + 'Q' * 12, 1, 0, 1, 1, 100, 200, *([0] * 8))]
    result = b'CEPROF\0\0' + pack('II', version, 0)
    for sequence, body in enumerate(bodies):
        header = pack('IIQQI', 0x4b435043, sequence + 1, sequence, len(body), zlib.crc32(body))
        result += header + pack('I', zlib.crc32(header)) + body
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--baseline-ref', default=BASELINE)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    sources = [READER, 'Tools/regression/verify-material-profile-format.py',
               'Engine/EngineDiagnostics/ProfileRenderMeasurement.h',
               'Engine/EngineDiagnostics/ProfileRhiContext.h']
    captured = {name: (root / name).read_bytes() for name in sources}
    original = subprocess.run(['git', 'show', args.baseline_ref + ':' + READER], cwd=root,
                              capture_output=True, check=True).stdout
    current = load_module(captured[READER], 'current_reader')
    baseline = load_module(original, 'baseline_reader')
    check(struct.calcsize('<' + RHI_FORMAT) == 100, 'RHI wire size changed')
    standard = [render_measurement(), render_measurement(True)]
    presenter = presenter_measurement()
    check(all(len(value) == 100 for value in [*standard, presenter, rhi_measurement()]),
          'Fixture measurement size is not 100 bytes')
    with tempfile.TemporaryDirectory(prefix='creator-material-profile-format-') as directory:
        path = Path(directory) / 'fixture.ceprof'

        def analyze(data, identities=False):
            path.write_bytes(data)
            return current.analyze(path, identities)

        def reject(data, label):
            try:
                analyze(data)
            except (ValueError, struct.error):
                check(True, label)
            else:
                check(False, 'Accepted malformed capture: ' + label)

        legacy_versions = (1, 2, 3, 4, 5, 6, 7)
        for version in legacy_versions:
            values = standard + ([presenter] if version >= 6 else []) if version >= 4 else []
            data = (stream if version in (3, 5, 7) else snapshot)(version, values)
            result = analyze(data)
            check(result.pop('rhiScopeIdentity')['samples'] == 0, 'Legacy file acquired RHI identity')
            check(result == baseline.analyze(path), 'Legacy summary changed for version ' + str(version))

        for version in (8, 9):
            encode = snapshot if version == 8 else stream
            values = standard + [presenter]
            plain = analyze(encode(version, values))
            raw = analyze(encode(version, values + [rhi_measurement(), rhi_measurement(True)]), True)
            compact = analyze(encode(version, values + [rhi_measurement(), rhi_measurement(True)]))
            check('records' not in compact['rhiScopeIdentity'], 'Default summary retained raw RHI records')
            identity = raw['rhiScopeIdentity']
            check(identity['samples'] == identity['exactEventMatches'] == 2, 'Exact CPU/GPU identity mismatch')
            check(identity['sourceFrameUnavailableSamples'] == identity['queueIdentityUnavailableSamples'] == 1,
                  'Unavailable independent identities were inferred')
            check(identity['records'][0]['eventFrame'] == 41 and raw['frames'] == 1,
                  'Cross-frame CPU key was replaced by the stored frame')
            check(identity['records'][0]['resolvedSourceEngineFrame'] == (1 << 41) + 71,
                  'Full source frame identity was narrowed')
            check(identity['records'][1]['resolvedSourceEngineFrame'] is None and
                  identity['records'][1]['targetIndex'] == 0xffffffff,
                  'Unknown source or aggregate target was inferred')
            for key in plain:
                if key not in ('sha256', 'rhiScopeIdentity'):
                    check(plain[key] == raw[key], 'RHI sidecar changed aggregate ' + key)
            check(raw['renderMeasurementCount'] == 2 and raw['presenterReturns']['samples'] == 1 and
                  raw['nativeRealPerformanceGateEligible'] and raw['nativeQualityGateEligible'],
                  'RHI sidecars polluted render or quality gate denominators')

        values = standard + [presenter]
        duplicate = analyze(snapshot(8, values + [rhi_measurement(), rhi_measurement()]), True)
        check(duplicate['rhiScopeIdentity']['ambiguousSamples'] == 2 and
              duplicate['rhiScopeIdentity']['exactEventMatches'] == 0 and
              all(not record['identityAvailable'] and record['resolvedSourceEngineFrame'] is None
                  for record in duplicate['rhiScopeIdentity']['records']), 'Duplicate sidecar selected latest identity')
        duplicate = analyze(snapshot(8, values + [rhi_measurement()], duplicate_event=True), True)
        check(duplicate['rhiScopeIdentity']['ambiguousSamples'] == 1, 'Duplicate event key was considered unique')
        changes = dict(eventFrame=42, marker=2, begin=111, end=121, slot=1, depth=2, flags=1,
                       queue=1, eventSubmission=1, eventView=1)
        for field, value in changes.items():
            result = analyze(snapshot(8, values + [rhi_measurement(**{field: value})]), True)
            identity = result['rhiScopeIdentity']
            check(identity['unmatchedSamples'] == 1 and identity['exactEventMatches'] == 0 and
                  identity['records'][0]['resolvedSourceEngineFrame'] is None,
                  'RHI lookup omitted exact key field ' + field)

        for version in (4, 5, 6, 7):
            reject((stream if version % 2 else snapshot)(version, [rhi_measurement()]),
                   'RHI axis in legacy version ' + str(version))
        reject(snapshot(4, [], chunk_version=3), 'v4/chunk3')
        reject(snapshot(6, [], chunk_version=3), 'v6/chunk3')
        reject(snapshot(8, [], chunk_version=2), 'v8/chunk2')
        reject(snapshot(8, [], chunk_version=4), 'future measurement chunk')
        for version in (0, 10, 11):
            reject(b'CEPROF\0\0' + pack('II', version, 0), 'unsupported version ' + str(version))
        invalid = [dict(recordingId=0), dict(marker=0), dict(end=111), dict(flags=8), dict(queueKind=3),
                   dict(sourceEngineFrameAvailable=2), dict(sourceEngineFrameAvailable=0), dict(reserved=1),
                   dict(deviceIncarnation=0), dict(queueId=0), dict(engineFrame=43)]
        for changes in invalid:
            reject(snapshot(8, [rhi_measurement(**changes)]), 'invalid RHI ' + str(changes))
        for axis in (0, 5, 255):
            reject(snapshot(8, [rhi_measurement(axis=axis)]), 'unknown axis ' + str(axis))
        reject(snapshot(8, [rhi_measurement()[:-1]]), 'truncated RHI wire')
        damaged = bytearray(snapshot(8, [rhi_measurement()]))
        damaged[-1] ^= 1
        reject(damaged, 'payload checksum')
        reject(stream(9, values)[:-1], 'truncated recording footer')

    check(all((root / name).read_bytes() == data for name, data in captured.items()), 'Inputs changed during test')
    report = dict(status='PASS', checks=CHECKS, baselineRef=args.baseline_ref,
                  legacySummaryVersions=list(legacy_versions), snapshotVersions=[1, 2, 4, 6, 8],
                  recordingVersions=[3, 5, 7, 9], measurementWireBytes=100,
                  productionExecution='Python consumer only; independent binary fixtures; no native/GPU execution',
                  coverage=['legacy exact summary equality', 'cross-frame exact RHI CPU/GPU event keys',
                            'duplicate and missing identities remain unresolved', 'full 64-bit independent identities',
                            'no render/GPU/FG/quality denominator contamination', 'unknown source/queue preservation',
                            'old-version axis rejection', 'invalid wire, CRC, footer and future-version rejection'],
                  baselineReaderSha256=hashlib.sha256(original).hexdigest(),
                  sourceSha256={name: hashlib.sha256(data).hexdigest() for name, data in captured.items()},
                  sourceUnchanged=True)
    output = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.write_text(output, encoding='utf-8')
    print(output, end='')


if __name__ == '__main__':
    main()
