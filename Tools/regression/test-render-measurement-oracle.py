"""Reject damaged identities in a real, lossless native GPU capture without rewriting it."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('material_profile', Path(__file__).with_name('summarize-material-profile.py'))
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    original = oracle.read_capture(args.capture)
    assert oracle.analyze(args.capture)['nativeRealPerformanceGateEligible'], 'positive control rejected'
    results = []
    reader = oracle.read_capture
    for name in ('missing', 'duplicate', 'foreign-frame', 'wrong-submission', 'wrong-view',
                 'unknown-spatial', 'generated', 'lossy', 'unfinished'):
        damaged = copy.deepcopy(original)
        frame = next(f for f in damaged['frames'] if any(m['axis'] == 2 for m in f['measurements']))
        sample = next(m for m in frame['measurements'] if m['axis'] == 2)
        if name == 'missing': frame['measurements'].remove(sample)
        elif name == 'duplicate': frame['measurements'].append(copy.deepcopy(sample))
        elif name == 'foreign-frame': sample['engineFrame'] += 1
        elif name == 'wrong-submission':
            sample['submissionId'] += 1
            sample['eventSubmission'] = sample['submissionId'] & 0xffffffff
        elif name == 'wrong-view':
            sample['viewId'] += 1
            sample['eventView'] = sample['viewId'] & 0xffff
        elif name == 'unknown-spatial': sample['spatialProvenanceAvailable'] = False
        elif name == 'generated': sample['frameKind'] = 2
        elif name == 'lossy': frame['dropped'] += 1
        elif name == 'unfinished': damaged['complete'] = False
        oracle.read_capture = lambda _: damaged
        try:
            rejected = not oracle.analyze(args.capture)['nativeRealPerformanceGateEligible']
        except ValueError:
            rejected = True
        finally:
            oracle.read_capture = reader
        assert rejected, f'mutation accepted: {name}'
        results.append(dict(mutation=name, rejected=True))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(passed=True, captureSha256=original['sha256'],
        scope='analysis identities after parsing; original bytes unchanged', mutations=results), indent=2), encoding='utf-8')
    print(f'PASS render measurement oracle: {len(results)} mutations rejected')


if __name__ == '__main__':
    main()
