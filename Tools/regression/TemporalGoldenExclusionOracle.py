"""Require forced-native GPU captures and preserved non-native startup intent."""
import argparse
import array
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    args = parser.parse_args()
    rows = [json.loads(line) for line in (args.run / 'results.jsonl').read_text(encoding='utf-8-sig').splitlines()]
    states = [row['data'] for row in rows if row['command'] == 'temporal.upscale']
    assert len(states) == 3, 'Missing before/after intent snapshots'
    for state in states:
        assert state['requestedUpscaler'] == 'dlss' and state['requestedFrameGenerator'] == 'fsr'
        assert state['spatialPost']['requestedNisMode'] == 'sharpen' and state['spatialPost']['deepDvcRequested']
        assert not state['nativeCaptureExclusionActive'], 'Native exclusion leaked beyond capture'
    coverage = []
    for name in ('first', 'second'):
        root = args.run / name
        manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8-sig'))
        assert manifest['draws'], 'Empty golden scene'
        attachment = next(a for a in manifest['attachments'] if a['name'] == 'depth')
        depth = array.array('f', (root / attachment['file']).read_bytes())
        if sys.byteorder != 'little':
            depth.byteswap()
        count = sum(0 < value < 1 for value in depth)
        assert count >= 64, 'Golden capture has no rendered foreground'
        coverage.append(count)
    result = dict(passed=True, intentSnapshots=3, renderedDepthPixels=coverage,
                  scope='nonempty real golden capture and startup intent restoration; no physical FG proof')
    (args.run / 'exclusion-oracle.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
