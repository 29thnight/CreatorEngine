"""Measure descriptor-page bucket splits from explicit captured producer identities."""
import argparse
import collections
import hashlib
import importlib.util
import json
from pathlib import Path
import struct


spec = importlib.util.spec_from_file_location('packets', Path(__file__).with_name('verify-material-packets.py'))
packets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packets)


def inspect_capture(directory):
    directory = directory.resolve()
    structure = packets.inspect_capture(directory)
    manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
    data = manifest['gpuMaterialPackets']
    descriptors = data['descriptorPages']
    if descriptors['sourceFrameId'] != manifest['frameId'] or descriptors['capacity'] != 64:
        raise ValueError('Descriptor page frame or capacity contract mismatch')
    pages = descriptors['pages']
    for index, page in enumerate(pages):
        if page['page'] != index or not 1 <= page['vertexCount'] <= 64 or not 1 <= page['topologyCount'] <= 64:
            raise ValueError('Invalid descriptor page extent')

    records = descriptors['records']
    if len(records) != data['recordCount'] - 1:
        raise ValueError('Descriptor record table is incomplete')
    bucket_keys = {}
    for record, entry in enumerate(records, 1):
        page_index = entry['page']
        if entry['record'] != record:
            raise ValueError('Descriptor record/page identity mismatch')
        bucket = data['recordBuckets'][record]
        # Volume-only records do not call AssignGeometry and have no raster
        # descriptor page. They must not belong to a material packet bucket.
        if page_index == 0xffffffff and bucket == 0xffffffff:
            continue
        if not 0 <= page_index < len(pages):
            raise ValueError('Descriptor record/page identity mismatch')
        page = pages[page_index]
        if not (0 <= entry['currentVertex'] < page['vertexCount'] and
                0 <= entry['previousVertex'] < page['vertexCount'] and
                0 <= entry['topology'] < page['topologyCount']):
            raise ValueError('GPU record descriptor index exceeds its retained page')
        if bucket == 0xffffffff:
            continue
        if not isinstance(entry['semanticKey'], str) or not entry['semanticKey'] or type(entry['doubleSided']) is not bool:
            raise ValueError('Missing exact shader/raster identity')
        if not 0 <= bucket < len(data['buckets']):
            raise ValueError('Descriptor record has an invalid material bucket')
        key = (entry['semanticKey'], entry['doubleSided'], page_index)
        if bucket in bucket_keys and bucket_keys[bucket] != key:
            raise ValueError('One bucket mixes shader, raster state or descriptor pages')
        bucket_keys[bucket] = key
    if set(bucket_keys) != set(range(len(data['buckets']))):
        raise ValueError('A material bucket has no source records')
    if len(set(bucket_keys.values())) != len(bucket_keys):
        raise ValueError('Identical shader/raster/page keys were split into separate buckets')

    groups = collections.defaultdict(list)
    for bucket, key in bucket_keys.items():
        groups[key[:2]].append((bucket, key[2]))
    raw = (directory / data['file']).read_bytes()
    bucket_tiles = set()
    program_tiles = set()
    for bucket, _, tile in struct.iter_unpack('<III', raw):
        bucket_tiles.add((bucket, tile))
        program_tiles.add((*bucket_keys[bucket][:2], tile))
    splits = []
    for (semantic, sided), buckets in sorted(groups.items()):
        splits.append({'programSha256': hashlib.sha256(semantic.encode()).hexdigest(),
                       'doubleSided': sided, 'pages': sorted(page for _, page in buckets),
                       'buckets': sorted(bucket for bucket, _ in buckets),
                       'packets': sum(data['buckets'][bucket]['count'] for bucket, _ in buckets)})
    return {'passed': True, 'capture': str(directory), 'packetSha256': structure['sha256'],
            'allocatedPages': len(pages), 'materialPages': len({key[2] for key in bucket_keys.values()}),
            'vertexDescriptors': sum(page['vertexCount'] for page in pages),
            'topologyDescriptors': sum(page['topologyCount'] for page in pages),
            'materialBuckets': len(bucket_keys), 'shaderRasterGroups': len(groups),
            'additionalBucketsFromPages': len(bucket_keys) - len(groups),
            'bucketTilePairs': len(bucket_tiles), 'shaderRasterTilePairs': len(program_tiles),
            'additionalBucketTilePairsFromPages': len(bucket_tiles) - len(program_tiles),
            'additionalRecordsInSameBucketTile': structure['additionalRecordsInSameBucketTile'],
            'groups': splits,
            'scope': 'Observed descriptor-page partitioning; removing page boundaries is a counterfactual count, not proof that cross-page packets may be merged safely'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = inspect_capture(args.capture)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
