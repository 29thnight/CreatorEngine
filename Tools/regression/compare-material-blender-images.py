"""Measure matched Blender/native linear images without silently accepting approximations."""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import shlex
import struct
import sys
import zlib


def read(path):
    result = array.array('f')
    result.frombytes(path.read_bytes())
    if sys.byteorder != 'little':
        result.byteswap()
    if len(result) != 64 * 64 * 4 or not all(math.isfinite(v) for v in result):
        raise ValueError('Invalid RGBA32F image: ' + str(path))
    return result


def png(path, width, height, rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xffffffff)
    data = b''.join(b'\x00' + bytes(row) for row in rows)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(data, 9)) + chunk(b'IEND', b''))


def input_values(path):
    values = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        tokens = shlex.split(line)
        if len(tokens) < 3 or tokens[0] in values:
            raise ValueError('Invalid or duplicate graph input: ' + str(path))
        count = int(tokens[1])
        numbers = [float(v) for v in tokens[2:]]
        if count not in (1, 3, 4) or len(numbers) != count or not all(math.isfinite(v) for v in numbers):
            raise ValueError('Invalid graph input components: ' + str(path))
        values[tokens[0]] = numbers[0] if count == 1 else numbers
    return values


def validate_reference(root, manifest, reference_images=True):
    if manifest['schema'] != 'creator.material.matched-reference.v1' or manifest['resolution'] != [64, 64]:
        raise ValueError('Unsupported matched fixture')
    expected_camera = {'eye': [0, 0, 3], 'vertical_fov': math.pi / 4, 'near': .1, 'far': 10}
    light_length = math.sqrt(.35**2 + .2**2 + .8**2)
    direction = [.35/light_length, -.2/light_length, .8/light_length]
    if (manifest['blender'] != '5.1.1' or manifest['renderer'] != 'CYCLES' or
            manifest['camera'] != expected_camera or
            manifest['pixel_filter'] != {'type': 'GAUSSIAN', 'width': .01} or
            manifest['sun']['irradiance'] != [1, 1, 1] or manifest['sun']['angle'] != 0 or
            len(manifest['sun']['to_light']) != 3 or
            any(abs(a-b) > 1e-6 for a, b in zip(direction, manifest['sun']['to_light'])) or
            manifest['furnace'] != {'radiance': [1, 1, 1]} or
            manifest['bounces'] != {'max': 1, 'diffuse': 1, 'glossy': 1}):
        raise ValueError('Reference setup differs from native capture contract')
    expected = {case['id'] for case in manifest['cases']}
    if len(expected) != len(manifest['cases']) or {p.stem for p in root.glob('*.inputs')} != expected:
        raise ValueError('Changed or duplicate reference case list')
    if hashlib.sha256((root / 'sphere.bin').read_bytes()).hexdigest() != manifest['geometry_sha256']:
        raise ValueError('Reference geometry identity changed')
    for case in manifest['cases']:
        name = case['id']
        if (case['normal_map'] or case['texture'] or name.split('-')[0] not in ('sun', 'furnace') or
                input_values(root / (name + '.inputs')) != case['inputs']):
            raise ValueError('Reference graph inputs changed: ' + name)
        if reference_images:
            pixels = root / (name + '.f32')
            if hashlib.sha256(pixels.read_bytes()).hexdigest() != case['reference_sha256']:
                raise ValueError('Reference image identity changed: ' + name)
            read(pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('native', type=Path, nargs='?')
    parser.add_argument('report', type=Path, nargs='?')
    parser.add_argument('--validate-reference', action='store_true')
    parser.add_argument('--reference-repeat', type=Path)
    args = parser.parse_args()
    manifest = json.loads((args.reference / 'manifest.json').read_text(encoding='utf-8'))
    validate_reference(args.reference, manifest)
    if args.validate_reference:
        print('MAT9_REFERENCE_IDENTITY_OK cases=' + str(len(manifest['cases'])))
        return 0
    if args.native is None or args.report is None:
        parser.error('native and report are required for image comparison')
    native_manifest = json.loads((args.native / 'reference-manifest.json').read_text(encoding='utf-8'))
    if manifest != native_manifest:
        raise ValueError('Native input reference identity differs')
    validate_reference(args.native, manifest, reference_images=False)
    repeat = None
    if args.reference_repeat is not None:
        repeat_manifest = json.loads((args.reference_repeat / 'manifest.json').read_text(encoding='utf-8'))
        validate_reference(args.reference_repeat, repeat_manifest)
        def setup(value):
            return {**{k: v for k, v in value.items() if k not in ('cases', 'seed')},
                    'cases': [{k: v for k, v in case.items() if k != 'reference_sha256'} for case in value['cases']]}
        if setup(manifest) != setup(repeat_manifest) or manifest.get('seed', 0) == repeat_manifest.get('seed', 0):
            raise ValueError('Noise comparison requires equal inputs/settings and distinct reference seeds')
        repeat = args.reference_repeat
    masks = {}
    for mode in ('sun', 'furnace'):
        a = read(args.reference / (mode + '-control_emission.f32'))
        b = read(args.native / (mode + '-control_emission.f32'))
        covered = [a[4*p+3] > .999 and b[4*p] > 1 for p in range(4096)]
        masks[mode] = [p for p in range(4096) if 2 <= p % 64 < 62 and 2 <= p // 64 < 62 and
                       all(covered[p+dy*64+dx] for dy in range(-2, 3) for dx in range(-2, 3))]
        if len(masks[mode]) < 100:
            raise ValueError('Matched geometry/camera coverage failed: ' + mode)
    rows = []
    contact = []
    for case in manifest['cases']:
        name = case['id']
        a = read(args.reference / (name + '.f32'))
        b = read(args.native / (name + '.f32'))
        indices = masks[name.split('-')[0]]
        differences = [abs(a[4*p+c]-b[4*p+c]) for p in indices for c in range(3)]
        normalized = [abs(a[4*p+c]-b[4*p+c])/max(1,abs(a[4*p+c])) for p in indices for c in range(3)]
        reference_energy = sum(a[4*p+c]**2 for p in indices for c in range(3))
        row = {'id': name, 'interior_pixels': len(indices), 'max_abs': max(differences),
               'mean_abs': sum(differences)/len(differences), 'max_normalized': max(normalized),
               'p95_normalized': sorted(normalized)[int(.95*(len(normalized)-1))],
               'relative_rms': math.sqrt(sum(d*d for d in differences)/max(reference_energy,1e-20)),
               'reference_mean_rgb': [sum(a[4*p+c] for p in indices)/len(indices) for c in range(3)],
               'native_mean_rgb': [sum(b[4*p+c] for p in indices)/len(indices) for c in range(3)]}
        rows.append(row)
        if repeat is not None:
            repeated = read(repeat / (name + '.f32'))
            noise = [abs(a[4*p+c]-repeated[4*p+c]) for p in indices for c in range(3)]
            row['reference_repeat'] = {
                'max_abs': max(noise),
                'relative_rms': math.sqrt(sum(d*d for d in noise)/max(reference_energy, 1e-20))}
        # Display-only common sRGB transfer, clamped at one; metrics above retain HDR.
        def display(value):
            value = min(1, max(0, value))
            return round(255 * (12.92*value if value <= .0031308 else 1.055*value**(1/2.4)-.055))
        for y in reversed(range(64)):
            strip = []
            for source in (a,b):
                strip.extend(display(source[(y*64+x)*4+c]) for x in range(64) for c in range(3))
            strip.extend(min(255,round(abs(a[(y*64+x)*4+c]-b[(y*64+x)*4+c])*2550))
                         for x in range(64) for c in range(3))
            contact.append(strip)
    controls = [row for row in rows if row['id'].endswith('control_emission')]
    control_pass = all(row['max_normalized'] <= .002 for row in controls)
    report = {'schema': 'creator.material.blender-image-comparison.v1', 'reference': str(args.reference),
              'native': str(args.native), 'control_passed': control_pass,
              'reference_repeat': str(repeat) if repeat else None,
              'acceptance': 'diagnostic; material approximation tolerances remain unaccepted',
              'mask': 'common emission-control interior eroded by two pixels; no per-material cherry picking',
              'contact_order': 'manifest cases, each row Blender | native | absolute difference x10',
              'cases': rows}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
    png(args.report.with_suffix('.png'),192,64*len(rows),contact)
    for row in rows:
        print(row['id'], 'pixels='+str(row['interior_pixels']), 'rms=%.6f'%row['relative_rms'],
              'p95=%.6f'%row['p95_normalized'], 'max=%.6f'%row['max_normalized'])
    print('MAT9_BLENDER_IMAGE_MEASURED cases=%s emissionControl=%s'%(len(rows),control_pass))
    return 0 if control_pass else 1


if __name__ == '__main__':
    sys.exit(main())
