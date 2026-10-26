"""Check production cache behavior in each warmed ON frame of the ABBA run."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    options = parser.parse_args()
    root = options.root.resolve()
    analysis = json.loads((root / 'analysis.json').read_text(encoding='utf-8-sig'))
    evidence = []
    for run in analysis['runs']:
        if not run['name'].endswith('-on'):
            continue
        assert run['fullCapture'], run['name']
        phases = run['allThreadFrameScopes']
        expected = 1024 if run['name'].startswith('before-') else 0
        for marker, count in [('MaterialClusterRequestSort', expected), ('MeshClusterRequests', 1025),
                              ('MeshPrepareStaticCache', 1025), ('GeometryFeedbackReadbackCreate', 0)]:
            actual = [row['count'] for row in phases[marker]]
            assert actual == [count] * run['frames'], (run['name'], marker, actual)
        evidence.append(dict(name=run['name'], frames=run['frames'],
                             requestMissesPerFrame=[row['count'] for row in phases['MaterialClusterRequestSort']],
                             requestLookupCallsPerFrame=1025, staticCacheCallsPerFrame=1025,
                             readbackCreationPerFrame=0))
    assert len(evidence) == 8
    (root / 'cache-regression.json').write_text(json.dumps(dict(schema=1, passed=True, evidence=evidence),
                                                         ensure_ascii=False, indent=2), encoding='utf-8')
    print('MESH_CACHE_MAINTENANCE_OK captures=8 beforeMisses=1024 afterMisses=0 readbackCreates=0')


if __name__ == '__main__':
    main()
