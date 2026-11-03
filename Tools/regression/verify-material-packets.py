"""Inspect captured packet density, without claiming independent visibility truth."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct


def inspect_capture(directory):
    directory = directory.resolve()
    manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
    data = manifest['gpuMaterialPackets']
    if data['sourceFrameId'] != manifest['frameId']:
        raise ValueError('Packet frame identity differs from capture')
    if data['encoding'] != 'uint32-le-bucket-record-tile':
        raise ValueError('Unsupported packet encoding')
    path = (directory / data['file']).resolve()
    if not path.is_relative_to(directory):
        raise ValueError('Packet path escapes capture')
    raw = path.read_bytes()
    if len(raw) != data['count'] * 12 or not 0 <= data['count'] <= data['capacity']:
        raise ValueError('Packet payload/count/capacity mismatch')
    if min(data['width'], data['height'], data['recordCount']) <= 0:
        raise ValueError('Invalid source extent')
    record_buckets = data['recordBuckets']
    if len(record_buckets) != data['recordCount']:
        raise ValueError('Record bucket table size mismatch')
    tiles = ((data['width'] + 7) // 8) * ((data['height'] + 7) // 8)
    storage = {}
    if 'storageBytes' in data or 'packedTileBits' in data:
        bits = data['packedTileBits']
        if not 0 <= bits < 32 or (bits and (tiles > 1 << bits or (data['recordCount'] - 1) << bits > 0xffffffff)):
            raise ValueError('Packed packet IDs exceed their lossless bit allocation')
        expected_bytes = ((data['capacity'] + 1) // 2 if bits else data['capacity']) * 8
        if data['storageBytes'] != expected_bytes:
            raise ValueError('Packet storage size does not match its encoding and capacity')
        storage = {'storageBytes': expected_bytes, 'packedTileBits': bits,
                   'unpackedStorageBytes': data['capacity'] * 8, 'logicalPacketBytes': 4 if bits else 8}
    packets = list(struct.iter_unpack('<III', raw))
    offset = 0
    seen = set()
    bucket_tiles = collections.Counter()
    occupied_tiles = set()
    buckets = []
    for bucket, entry in enumerate(data['buckets']):
        count = entry['count']
        if entry['bucket'] != bucket or entry['offsetPackets'] != offset or count < 0 or offset + count > len(packets):
            raise ValueError('Invalid bucket range')
        for actual, record, tile in packets[offset:offset + count]:
            if actual != bucket or not 0 < record < data['recordCount'] or tile >= tiles or record_buckets[record] != bucket:
                raise ValueError('Packet bucket/record/tile mismatch')
            if (record, tile) in seen:
                raise ValueError('Duplicate record/tile packet')
            seen.add((record, tile))
            bucket_tiles[bucket, tile] += 1
            occupied_tiles.add(tile)
        buckets.append({'bucket': bucket, 'packets': count})
        offset += count
    if offset != len(packets):
        raise ValueError('Unclaimed packet payload')
    return {'capture': str(directory), 'passed': True, 'sha256': hashlib.sha256(raw).hexdigest(), **storage,
            'packets': len(packets), 'capacity': data['capacity'], 'occupiedTiles': len(occupied_tiles),
            'uniqueBucketTiles': len(bucket_tiles), 'additionalRecordsInSameBucketTile': len(packets) - len(bucket_tiles),
            'maxRecordsPerBucketTile': max(bucket_tiles.values(), default=0), 'buckets': buckets,
            'scope': 'Packet structure and density only; no proof of visibility, material equivalence, or safe packet merging'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    report = inspect_capture(args.capture)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
