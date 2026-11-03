"""Compare GPU packets with CPU enumeration of their captured visibility input."""
import argparse
import bisect
import hashlib
import importlib.util
import json
from pathlib import Path
import struct


spec = importlib.util.spec_from_file_location('packets', Path(__file__).with_name('verify-material-packets.py'))
packets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packets)


def expected_packets(width, height, visibility, ranges, buckets, bucket_count):
    if width <= 0 or height <= 0 or len(visibility) != width * height * 8:
        raise ValueError('Visibility extent/payload mismatch')
    if len(ranges) != len(buckets) or not ranges or ranges[0] != [0, 0]:
        raise ValueError('Primitive range table shape mismatch')

    starts = []
    previous_end = 1
    for begin, end in ranges[1:]:
        if not 1 <= begin < end <= 0xffffffff or begin < previous_end or (end - begin) % 128:
            raise ValueError('Overlapping or invalid primitive ranges')
        starts.append(begin)
        previous_end = end
    if any(bucket != 0xffffffff and not 0 <= bucket < bucket_count for bucket in buckets):
        raise ValueError('Invalid record material bucket')

    expected = set()
    grid = (width + 7) // 8
    foreground = 0
    for pixel, (address, _) in enumerate(struct.iter_unpack('<II', visibility)):
        if address in (0, 0xffffffff):
            continue
        record = bisect.bisect_right(starts, address)
        if record == 0 or address >= ranges[record][1]:
            raise ValueError('Visibility address has no owning record')
        if (address - ranges[record][0]) % 128 >= 126:
            raise ValueError('Visibility references a reserved primitive slot')
        foreground += 1
        bucket = buckets[record]
        if bucket == 0xffffffff:
            continue
        x, y = pixel % width, pixel // width
        expected.add((bucket, record, (y // 8) * grid + x // 8))
    return expected, foreground


def verify_capture(directory):
    directory = directory.resolve()
    structure = packets.inspect_capture(directory)
    manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
    data = manifest['gpuMaterialPackets']
    source = data['visibility']
    if source['sourceFrameId'] != manifest['frameId'] or source['encoding'] != 'uint32-le-primitive-depth-row-major':
        raise ValueError('Visibility frame identity or encoding mismatch')
    if (source['width'], source['height']) != (data['width'], data['height']):
        raise ValueError('Visibility and packet extents differ')
    path = (directory / source['file']).resolve()
    if not path.is_relative_to(directory):
        raise ValueError('Visibility path escapes capture')
    raw = path.read_bytes()
    expected, foreground = expected_packets(data['width'], data['height'], raw,
        data['primitiveRanges'], data['recordBuckets'], len(data['buckets']))
    actual = set(struct.iter_unpack('<III', (directory / data['file']).read_bytes()))
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    return {'passed': not missing and not extra, 'capture': str(directory),
            'visibilitySha256': hashlib.sha256(raw).hexdigest(), 'packetSha256': structure['sha256'],
            'foregroundPixels': foreground, 'expectedPackets': len(expected), 'actualPackets': len(actual),
            'missingCount': len(missing), 'extraCount': len(extra),
            'missingExamples': missing[:8], 'extraExamples': extra[:8],
            'scope': 'Packet completeness for captured GPU visibility and addressing inputs; not independent rasterization or depth correctness'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    report = verify_capture(args.capture)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if report['passed'] else 1)
