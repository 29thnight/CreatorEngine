"""Actual live metadata admission for BASE-0/RG6; no performance population claim."""
import argparse
import copy
import hashlib
import json
from pathlib import Path

from rg6_acceptance import audit_live_measurement


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    original = args.results.read_bytes()
    snapshots = [row['data'] for row in map(json.loads, original.decode('utf-8-sig').splitlines())
                 if row['command'] == 'dx12.live' and row['status'] == 'succeeded']
    assert snapshots
    samples = []
    for snapshot in snapshots:
        sample = copy.deepcopy(snapshot)
        # Zero is an explicit test duration, not a measured CPU cost. GPU and
        # temporal identities remain exactly those recorded by the actual API.
        sample.update(gpuFrame=snapshot['gpu']['frame'], gpuMs=snapshot['gpu']['ms'], cpuRecordMs=0,
                      cpuAxis='native-graph-record-cpu-not-full-frame', gpuAxis='real-frame-pass-sum-not-wall-time')
        audit_live_measurement(sample)
        samples.append(sample)
    cases = []
    for owner in ('temporalProvenance', 'gpuTemporalProvenance'):
        for field in samples[0][owner]:
            damaged = copy.deepcopy(samples[0])
            del damaged[owner][field]
            # Fields outside the native contract (e.g. runtime controls) are not
            # acceptance proof and may be absent.
            if field not in ('schemaVersion', 'valid', 'frameKind', 'generatedOrdinal', 'upscaler', 'frameGenerator',
                             'spatialMode', 'deepDvcApplied', 'resolutionState', 'realFrameId', 'publicationFrameId',
                             'viewId', 'sceneEpoch', 'renderWidth', 'renderHeight', 'displayWidth', 'displayHeight'):
                continue
            cases.append((owner + '.missing-' + field, damaged))
        for field, value in (('frameKind', 'generated'), ('generatedOrdinal', 1), ('upscaler', 'dlss')):
            damaged = copy.deepcopy(samples[0])
            damaged[owner][field] = value
            cases.append((owner + '.wrong-' + field, damaged))
    for field, value in (('cpuAxis', 'display-fps'), ('gpuAxis', 'frame-wall-time'),
                         ('gpuFrame', -1), ('gpuMs', -1), ('cpuRecordMs', float('nan'))):
        damaged = copy.deepcopy(samples[0]); damaged[field] = value
        cases.append(('wrong-' + field, damaged))
    legacy = {k: samples[0][k] for k in ('gpuFrame', 'gpuMs', 'cpuRecordMs')}
    cases.append(('legacy-row-without-provenance', legacy))
    rejected = []
    for name, damaged in cases:
        try:
            audit_live_measurement(damaged)
        except (KeyError, ValueError) as error:
            rejected.append(dict(case=name, reason=str(error)))
        else:
            raise AssertionError('Invalid live timing row accepted: ' + name)
    assert args.results.read_bytes() == original
    report = dict(passed=True, snapshots=len(samples), rejections=rejected, sourceSha256=hashlib.sha256(original).hexdigest(),
                  cpuDurationIsTestValue=True, performanceAcceptance=False,
                  scope='actual native identity plus stored-row/axis admission; no BASE-0 workload or CPU measurement claim')
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'PASS BASE-0/RG6 live metadata: {len(samples)} snapshots / {len(rejected)} rejections')


if __name__ == '__main__':
    main()
