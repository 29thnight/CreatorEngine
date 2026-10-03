"""Independent byte-format and product-consumer checks for the camera replay slice."""
import argparse
import json
from pathlib import Path
import struct

from base0_artifacts import load_capture, require, truth


def seal(data):
    h = 14695981039346656037
    for byte in data[:-8]:
        h = ((h ^ byte) * 1099511628211) & ((1 << 64) - 1)
    struct.pack_into('<Q', data, len(data) - 8, h)
    return data


def prepare(capture, output):
    data = bytearray((capture / 'camera-input.bin').read_bytes())
    require(len(data) == 368 and data[:8] == b'CECAM001', 'camera input format')
    require(data == seal(bytearray(data)), 'camera input checksum')
    m, _, _ = load_capture(capture)
    require(struct.unpack_from('<III', data, 8) == (1, m['width'], m['height']), 'camera input dimensions')
    # Camera begins at byte 44: four row-major matrices, then eye/forward/right/up.
    for offset, name in ((44, 'view'), (108, 'projection')):
        require(all(abs(a - b) <= 1e-5 * max(1, abs(a), abs(b))
                    for a, b in zip(struct.unpack_from('<16f', data, offset), m['camera'][name])),
                'camera packet and manifest disagree: ' + name)
    output.mkdir(parents=True, exist_ok=False)
    cases = {'truncated': data[:-1]}
    bad = bytearray(data); bad[80] ^= 1; cases['checksum'] = bad
    bad = bytearray(data); struct.pack_into('<I', bad, 8, 2); cases['version'] = seal(bad)
    bad = bytearray(data); struct.pack_into('<I', bad, 20, 1); cases['target'] = seal(bad)
    bad = bytearray(data); struct.pack_into('<I', bad, 12, m['width'] + 1); cases['extent'] = seal(bad)
    bad = bytearray(data); struct.pack_into('<f', bad, 44, float('nan')); cases['nonfinite'] = seal(bad)
    bad = bytearray(data)
    view = list(struct.unpack_from('<16f', bad, 44))
    inverse = list(struct.unpack_from('<16f', bad, 172))
    eye = list(struct.unpack_from('<3f', bad, 300))
    right = struct.unpack_from('<3f', bad, 324)
    delta = [v * .25 for v in right]
    for col in range(4):
        view[12 + col] -= sum(delta[row] * view[row * 4 + col] for row in range(3))
    for i in range(3):
        inverse[12 + i] += delta[i]
        eye[i] += delta[i]
    struct.pack_into('<16f', bad, 44, *view)
    struct.pack_into('<16f', bad, 172, *inverse)
    struct.pack_into('<3f', bad, 300, *eye)
    cases['shifted-camera'] = seal(bad)
    bad = bytearray(data)
    struct.pack_into('<ff', bad, 32, 17.25, 1 / 60)
    cases['clock'] = seal(bad)
    for name, value in cases.items():
        (output / (name + '.bin')).write_bytes(value)
    return dict(prepared=list(cases), size=len(data))


def verify(original, shifted, source_file):
    a, _, ap = load_capture(original)
    b, _, bp = load_capture(shifted)
    require(b['cameraInputContract'] == 'camera-clock-v1' and truth(b['cameraInputReplayed']), 'missing replay consumer')
    require((shifted / 'camera-input.bin').read_bytes() == source_file.read_bytes(), 'consumer did not preserve exact replay input')
    require(a['camera'] != b['camera'], 'camera injection ignored')
    changed = sum(x != y for x, y in zip(ap['baseColor'], bp['baseColor']))
    require(changed > 0, 'camera injection did not affect actual GBuffer pixels')
    result = dict(passed=True, changedBaseColorComponents=changed, inputBytesExact=True)
    (shifted / 'camera-replay-verification.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', choices=['prepare', 'verify'])
    parser.add_argument('original', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('source_file', type=Path, nargs='?')
    args = parser.parse_args()
    result = prepare(args.original, args.output) if args.operation == 'prepare' else verify(args.original, args.output, args.source_file)
    print(json.dumps(result))
