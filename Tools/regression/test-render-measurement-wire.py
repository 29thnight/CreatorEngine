"""Mutate real CEPROF v7 bytes with valid CRCs; preserve the original capture."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import zlib

spec = importlib.util.spec_from_file_location('profile_summary', Path(__file__).with_name('summarize-material-profile.py'))
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


def records(data):
    if data[:8] != b'CEPROF\0\0' or struct.unpack_from('<II', data, 8) != (7, 0):
        raise ValueError('This mutation gate requires a finalized v7 recording')
    offset = 16
    while offset < len(data):
        magic, kind, serial, size, crc, header_crc = struct.unpack_from('<IIQQII', data, offset)
        payload = offset + 32
        yield offset, kind, payload, size
        offset = payload + size


def gpu_sample(data):
    for header, kind, payload, size in records(data):
        if kind != 2:
            continue
        events, counters = struct.unpack_from('<II', data, payload + 28)
        count_offset = payload + 36 + events * 62 + counters * 34
        count = struct.unpack_from('<I', data, count_offset)[0]
        position = count_offset + 4
        for _ in range(count):
            if data[position + 4] == 2:
                return header, payload, size, position
            position += 100
    raise ValueError('No real GPU measurement in capture')


def repair_crc(data, header, payload, size):
    struct.pack_into('<I', data, header + 24, zlib.crc32(data[payload:payload + size]))
    struct.pack_into('<I', data, header + 28, zlib.crc32(data[header:header + 28]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    original = args.capture.read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    assert oracle.analyze(args.capture)['nativeRealPerformanceGateEligible'], 'Positive control failed'
    header, payload, size, sample = gpu_sample(original)
    # Offsets follow the actual 100-byte v7 render measurement wire layout.
    cases = {
        'missing-render-width': [(sample + 80, '<I', 0)],
        'missing-render-height': [(sample + 84, '<I', 0)],
        'missing-display-width': [(sample + 88, '<I', 0)],
        'missing-display-height': [(sample + 92, '<I', 0)],
        'generated-counted-as-real': [(sample + 76, '<I', 1), (sample + 43, '<B', 1)],
        'generated-render-sample': [(sample + 40, '<B', 2), (sample + 43, '<B', 1), (sample + 76, '<I', 1)],
        'missing-real-frame-id': [(sample + 44, '<Q', 0)],
        'missing-publication-id': [(sample + 68, '<Q', 0)],
        'foreign-engine-frame': [(sample, '<I', struct.unpack_from('<I', original, sample)[0] + 1)],
        'unknown-spatial-proof': [(sample + 97, '<B', 0)],
        'non-native-spatial-effect': [(sample + 98, '<B', 2)],
        'deep-dvc-applied': [(sample + 99, '<B', 1)],
    }
    results = []
    with tempfile.TemporaryDirectory(prefix='ceprof-wire-') as directory:
        path = Path(directory) / 'mutation.ceprof'
        for name, patches in cases.items():
            damaged = bytearray(original)
            for offset, fmt, value in patches:
                struct.pack_into(fmt, damaged, offset, value)
            repair_crc(damaged, header, payload, size)
            path.write_bytes(damaged)
            try:
                report = oracle.analyze(path)
                rejected = not report['nativeRealPerformanceGateEligible']
                reason = 'native-real gate rejected parsed recording'
            except ValueError as error:
                rejected = True
                reason = str(error)
                assert 'checksum' not in reason.lower(), 'Mutation rejected only by CRC'
            assert rejected, f'Mutation accepted: {name}'
            results.append(dict(mutation=name, rejected=True, reason=reason))
    assert hashlib.sha256(args.capture.read_bytes()).hexdigest() == digest, 'Original changed'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(passed=True, captureSha256=digest,
        scope='real v7 wire mutations with recomputed payload/header CRC; not physical generated output',
        mutations=results), indent=2) + '\n', encoding='utf-8')
    print(f'PASS wire measurement contract: {len(results)} mutations rejected')


if __name__ == '__main__':
    main()
