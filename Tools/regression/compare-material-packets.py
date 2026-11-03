"""Compare unordered material work after both captures pass structural validation."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location('packet_reader', Path(__file__).with_name('verify-material-packets.py'))
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def load(directory):
    reader.inspect_capture(directory)
    manifest = json.loads((directory / 'manifest.json').read_text())
    packets = manifest['gpuMaterialPackets']
    records = packets['records']
    if len(records) != packets['recordCount']:
        raise ValueError('Missing record identities')
    materials = {}
    for draw in manifest['draws']:
        if 'lattice' not in draw:
            continue
        material = draw['lattice']
        slot = material['slot']
        value = {key: item for key, item in material.items() if key != 'slot'}
        if slot in materials and materials[slot] != value:
            raise ValueError('Ambiguous captured material slot')
        materials[slot] = value
    keys = {}
    for index, entry in enumerate(records):
        if entry['record'] != index:
            raise ValueError('Record identity index mismatch')
        if index == 0:
            continue
        required = ('meshId', 'meshDomain', 'materialSlot', 'lodIndex', 'chunkIndex', 'worldColumns')
        if any(key not in entry for key in required) or len(entry['worldColumns']) != 12:
            raise ValueError('Incomplete stable record identity')
        identity = {key: value for key, value in entry.items() if key not in ('record', 'materialSlot')}
        if entry['materialSlot'] not in materials:
            raise ValueError('Missing captured material slot input')
        identity['material'] = materials[entry['materialSlot']]
        keys[index] = json.dumps(identity, sort_keys=True, separators=(',', ':'))
    if len(set(keys.values())) != len(keys):
        raise ValueError('Ambiguous duplicate record identities; cannot correlate across captures')
    groups = {}
    for bucket in range(len(packets['buckets'])):
        groups[bucket] = tuple(sorted(keys[index] for index in keys if packets['recordBuckets'][index] == bucket))
    if len(set(groups.values())) != len(groups):
        raise ValueError('Ambiguous bucket membership')
    shape = {'width': packets['width'], 'height': packets['height'], 'records': sorted(keys.values()),
             'bucketGroups': sorted(groups.values())}
    rows = sorted((groups[bucket], keys[record], tile) for bucket, record, tile in
                  struct.iter_unpack('<III', (directory / packets['file']).read_bytes()))
    return shape, rows


def compare(left, right):
    a, a_rows = load(left)
    b, b_rows = load(right)
    missing = sorted(set(a_rows) - set(b_rows))
    added = sorted(set(b_rows) - set(a_rows))
    return {'passed': a == b and a_rows == b_rows, 'shapeEqual': a == b,
            'leftPackets': len(a_rows), 'rightPackets': len(b_rows),
            'missingCount': len(missing), 'addedCount': len(added),
            'missingTiles': [row[2] for row in missing[:8]], 'addedTiles': [row[2] for row in added[:8]],
            'scope': 'Stable mesh/world/LOD/chunk and captured material input equality with bucket membership; rendered identity requires separate capture comparison'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('left', type=Path)
    parser.add_argument('right', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = compare(args.left, args.right)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    raise SystemExit(0 if result['passed'] else 1)
