"""Independent selected-draw archive checks and live transform consumer proof."""
import argparse
import json
from pathlib import Path
import struct
from base0_artifacts import load_capture, require, truth
from base0_camera_replay import seal


def prepare(capture, output):
    data = bytearray((capture / 'draw-input.bin').read_bytes())
    require(data[:8] == b'CEDRW001' and len(data) >= 136, 'draw format/nonempty fixture')
    require(data == seal(bytearray(data)), 'draw checksum')
    require(struct.unpack_from('<I', data, 8)[0] == 1, 'draw version')
    count = struct.unpack_from('<I', data, 12)[0]
    offset = 16
    pose_count = 0
    for _ in range(count):
        route = struct.unpack_from('<I', data, offset)[0]
        bones = struct.unpack_from('<I', data, offset + 44)[0]
        require(route <= 2 and bones <= 1024, 'draw route/pose')
        offset += 112 + bones * 64
        pose_count += bones
    require(offset == len(data) - 8, 'draw framing')
    output.mkdir(parents=True, exist_ok=False)
    cases = {'truncated': data[:-1]}
    bad = bytearray(data); bad[70] ^= 1; cases['checksum'] = bad
    for name, offset, value in [('version', 8, 2), ('count', 12, 0xffffffff),
                                 ('route', 16, 3), ('pose-count', 60, 0xffffffff)]:
        bad = bytearray(data); struct.pack_into('<I', bad, offset, value); cases[name] = seal(bad)
    bad = bytearray(data); struct.pack_into('<f', bad, 64, float('nan')); cases['nonfinite'] = seal(bad)
    bad = bytearray(data); bad[20] ^= 1; cases['asset'] = seal(bad)
    bad = bytearray(data); bad[52] ^= 1; cases['geometry'] = seal(bad)
    bad = bytearray(data)
    x = struct.unpack_from('<f', bad, 112)[0]
    struct.pack_into('<f', bad, 112, x + .25)
    cases['shifted-world'] = seal(bad)
    for name, packet in cases.items():
        (output / (name + '.bin')).write_bytes(packet)
    return dict(prepared=list(cases), drawCount=count, poseMatrixCount=pose_count,
                animatedLiveFixture=pose_count > 0)


def verify(original, shifted, source_file):
    a, _, ap = load_capture(original)
    b, _, bp = load_capture(shifted)
    require(b['drawInputContract'] == 'selected-transform-pose-v1' and truth(b['drawInputReplayed']), 'draw consumer missing')
    require((shifted / 'draw-input.bin').read_bytes() == source_file.read_bytes(), 'draw bytes changed')
    require(a['draws'][0]['world'] != b['draws'][0]['world'], 'draw world ignored')
    changed = sum(x != y for x, y in zip(ap['baseColor'], bp['baseColor']))
    require(changed > 0, 'draw injection did not affect GBuffer pixels')
    result = dict(passed=True, changedBaseColorComponents=changed, inputBytesExact=True)
    (shifted / 'draw-replay-verification.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', choices=['prepare', 'verify'])
    parser.add_argument('original', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('source_file', type=Path, nargs='?')
    args = parser.parse_args()
    print(json.dumps(prepare(args.original, args.output) if args.operation == 'prepare'
                     else verify(args.original, args.output, args.source_file)))
