"""Native timing admission on actual live snapshots; no RG8 speedup acceptance."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
spec = importlib.util.spec_from_file_location('rg8_native_gate', Path(__file__).with_name('native_real_measurement.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    before = hashlib.sha256(args.results.read_bytes()).hexdigest()
    results = [json.loads(line) for line in args.results.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    snapshots = [row['data'] for row in results if row['command'] == 'dx12.live' and row['status'] == 'succeeded']
    assert snapshots
    for snapshot in snapshots: audit.verify_normal_temporal(snapshot)
    rejected = []
    def reject(name, damaged):
        try: audit.verify_normal_temporal(damaged)
        except ValueError: rejected.append(name)
        else: raise AssertionError('Invalid timing sample accepted: ' + name)
    source = snapshots[-1]
    for owner in ('temporalProvenance', 'gpuTemporalProvenance'):
        for field in source[owner]:
            if field not in ('schemaVersion', 'valid', 'frameKind', 'generatedOrdinal', 'upscaler', 'frameGenerator',
                'spatialMode', 'deepDvcApplied', 'resolutionState', 'realFrameId', 'publicationFrameId', 'viewId',
                'sceneEpoch', 'renderWidth', 'renderHeight', 'displayWidth', 'displayHeight'): continue
            damaged = copy.deepcopy(source)
            del damaged[owner][field]
            reject(owner + '.missing-' + field, damaged)
        for field, value in (('frameKind', 'generated'), ('generatedOrdinal', 1), ('deepDvcApplied', True),
            ('upscaler', 'dlss'), ('frameGenerator', 'fsr'), ('spatialMode', 'sharpen'), ('resolutionState', 'native-fallback')):
            damaged = copy.deepcopy(source)
            damaged[owner][field] = value
            reject(owner + '.invalid-' + field, damaged)
    for field in ('frame', 'viewId'):
        damaged = copy.deepcopy(source)
        damaged['gpu'][field] += 1
        reject('gpu-mismatch-' + field, damaged)
    damaged = copy.deepcopy(source)
    damaged['display']['width'] += 1
    reject('display-mismatch', damaged)
    damaged = copy.deepcopy(source)
    damaged['framesRenderedAxis'] = 'display-fps'
    reject('wrong-axis', damaged)
    assert before == hashlib.sha256(args.results.read_bytes()).hexdigest()
    args.output.write_text(json.dumps(dict(passed=True, actualSnapshots=len(snapshots), sourceSha256=before,
        rejected=rejected, performanceAdoptionAccepted=False, physicalAcceptance=False), indent=2) + '\n', encoding='utf-8')
    print(f'PASS RG8 native timing admission: {len(rejected)} rejections')


if __name__ == '__main__': main()
