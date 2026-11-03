"""Validate captured GPU-list structure; this does not prove culling correctness."""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import sys


def inspect_capture(directory):
    directory = directory.resolve()
    manifest = json.loads((directory / 'manifest.json').read_text())
    stages = manifest.get('gpuMeshletLists', [])
    if not stages or len({stage['stage'] for stage in stages}) != len(stages):
        raise ValueError('Missing or duplicate GPU-list stages')
    result = []
    for stage in stages:
        if stage['sourceFrameId'] != manifest['frameId']:
            raise ValueError('GPU list belongs to another frame')
        if stage['encoding'] != 'uint32-le-local-meshlet-id':
            raise ValueError('Unsupported GPU-list encoding')
        mode = stage['spatialCulling']
        if mode not in ('enabled', 'bypassed-diagnostic') or not stage['bins']:
            raise ValueError('Missing GPU-list bins or unsupported culling mode')
        path = (directory / stage['file']).resolve()
        if not path.is_relative_to(directory):
            raise ValueError('GPU-list file escapes capture directory')
        raw = path.read_bytes()
        words = array.array('I')
        words.frombytes(raw)
        if sys.byteorder != 'little':
            words.byteswap()
        offset = 0
        capacity_total = 0
        nonempty = 0
        for index, entry in enumerate(stage['bins']):
            count, capacity = entry['count'], entry['capacity']
            if entry['bin'] != index or entry['offsetWords'] != offset:
                raise ValueError('GPU-list bins are not contiguous')
            if not 0 <= count <= capacity or offset + count > len(words):
                raise ValueError('GPU-list count exceeds capacity or payload')
            selected = words[offset:offset + count]
            if len(set(selected)) != count or any(value >= capacity for value in selected):
                raise ValueError('GPU-list IDs are duplicated or out of range')
            if len(entry['world']) != 16 or not all(math.isfinite(value) for value in entry['world']):
                raise ValueError('GPU-list world transform is invalid')
            if not entry['meshId']:
                raise ValueError('GPU-list mesh identity is missing')
            offset += count
            capacity_total += capacity
            nonempty += count != 0
        if offset != len(words) or offset != stage['acceptedMeshlets']:
            raise ValueError('GPU-list total differs from payload')
        result.append(dict(stage=stage['stage'], spatialCulling=mode, bins=len(stage['bins']), nonemptyBins=nonempty,
                           capacity=capacity_total, acceptedMeshlets=offset,
                           sha256=hashlib.sha256(raw).hexdigest()))
    return dict(passed=True, capture=str(directory), stages=result,
                scope='GPU accepted-list structure only; not independent visibility or pixel coverage')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    report = inspect_capture(args.capture)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
