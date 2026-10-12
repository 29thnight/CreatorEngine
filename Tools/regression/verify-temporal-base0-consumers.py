"""Actual native BASE-0 populations consumed by the RG6 timing contract.

This is a current-product workload gate, not the legacy/dependency A/B cutover
or a product frame-time budget. No synthetic CPU durations are substituted.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path

import base0_artifacts as artifacts
from rg6_acceptance import audit_live_population


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def reject_population_mutations(samples):
    variants = {'missing-row': samples[:-1], 'duplicate-row': [samples[0]] + samples[:-1],
                'reversed-order': list(reversed(samples))}
    for name, owner, field, value in (
            ('generated', 'gpuTemporalProvenance', 'frameKind', 'generated'),
            ('missing-extent', 'gpuTemporalProvenance', 'displayWidth', None),
            ('display-fps-axis', None, 'gpuAxis', 'display-fps'),
            ('nonfinite-cpu', None, 'cpuRecordMs', float('nan')),
            ('lost-gpu-coverage', None, 'dropped', 1)):
        damaged = copy.deepcopy(samples)
        target = damaged[len(damaged) // 2]
        if owner:
            target = target[owner]
        target[field] = value
        variants[name] = damaged
    for name, field, value in (('missing-cpu-identity', 'temporalProvenance', {}),
                               ('unmeasured-cpu', 'samples', 0),
                               ('cpu-axis-mixed', 'axis', 'full-frame-cpu'),
                               ('cpu-value-relabelled', 'ms', -1)):
        damaged = copy.deepcopy(samples)
        damaged[len(damaged) // 2]['nativeRecord'][field] = value
        variants[name] = damaged
    for owner in ('cpu', 'gpu'):
        damaged = copy.deepcopy(samples)
        row = damaged[len(damaged) // 2]
        origin = row['nativeRecord']['temporalProvenance'] if owner == 'cpu' else row['gpuTemporalProvenance']
        origin['nativeGateActive'] = True
        variants[owner + '-capture-cost-in-normal'] = damaged
    damaged = copy.deepcopy(samples)
    damaged[len(damaged) // 2]['nativeRecord'] = copy.deepcopy(damaged[0]['nativeRecord'])
    damaged[len(damaged) // 2]['cpuRecordMs'] = damaged[0]['cpuRecordMs']
    variants['duplicate-cpu-window'] = damaged
    rejections = {}
    for name, rows in variants.items():
        try:
            audit_live_population(rows)
        except (ValueError, KeyError, TypeError) as error:
            rejections[name] = str(error)
        else:
            raise ValueError('population mutation survived: ' + name)
    return rejections


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    configs = {}
    representatives = []
    source_identity = None
    for config in ('Debug', 'Release'):
        directory = root / config
        result = read(directory / 'result.json')
        native_tests = read(root / ('native-' + config) / 'verification.json')
        artifacts.require(native_tests['passed'] and native_tests['assertionsEnabled'] and
                          len(native_tests['cases']) == 3, 'native diagnostics regressions incomplete')
        artifacts.require(result['complete'] and result['artifactGatesPassed'] and
                          not result['failures'] and result['sourceChanges'] == 0,
                          config + ' BASE-0 failed')
        artifacts.require(result['graphSamples'] == result['performanceSamples'] == 100 and
                          result['expectedGraphContract'] == 'dependency-order', 'workload scope')
        exe = Path(result['executable'])
        artifacts.require(sha(exe) == result['executableSha256'] and
                          sha(exe.with_name('CreatorEditor.runtime.dll')) == result['runtimeSha256'],
                          'current executable/runtime differs')
        sources = read(directory / 'source-hashes.json')
        identity = {source['path']: source['sha256'] for source in sources}
        artifacts.require(source_identity is None or identity == source_identity, 'configuration source drift')
        source_identity = identity
        for path, expected in identity.items():
            artifacts.require(sha(Path(path)) == expected, 'source changed: ' + path)
        runs = result['runs']
        artifacts.require(len(runs) == 2 and len({run['pid'] for run in runs}) == 2,
                          'two independent processes required')
        populations = []
        for run in runs:
            artifacts.require(run['passed'] and run['exitCode'] == 0 and not run['forcedTermination'],
                              'unclean process')
            artifacts.require(run['scenePreparationState'] == 'Ready' and int(run['sceneRequestId']) > 0,
                              'scene receipt is not completed preparation')
            case = Path(run['directory'])
            samples = read(case / 'live-performance.json')
            population = audit_live_population(samples)
            raw_rows = [json.loads(line) for line in (case / 'responses.jsonl').read_text(
                encoding='utf-8-sig').splitlines()]
            snapshots = [row['data'] for row in raw_rows
                         if row['command'] == 'dx12.live' and row['status'] == 'succeeded']
            for sample in samples:
                matching = []
                for snapshot in snapshots:
                    if all(sample[field] == snapshot[field] for field in
                                   ('gpu', 'display', 'temporalProvenance', 'gpuTemporalProvenance',
                                    'framesRenderedAxis', 'nativeRecord')) and sample['cpuRecordMs'] == snapshot['nativeRecord']['ms']:
                        matching.append(snapshot)
                artifacts.require(matching, 'stored CPU/GPU row lacks actual command response proof')
            artifacts.require(population['cpuRecordMs']['maximum'] > 0 and
                              population['gpuPassSumMs']['maximum'] > 0, 'actual timing population empty')
            validation = read(case / 'validation.json')
            artifacts.require(validation['layerEnabled'] and validation['mode'] == 'gpu' and
                              validation['problems'] == validation['droppedMessages'] == 0,
                              'GPU validation failed')
            determinism = read(case / 'determinism.json')
            expected_count = 100 if run['repeat'] == 0 else 2
            artifacts.require(determinism['passed'] and determinism['samples'] == expected_count and
                              len(run['captures']) == expected_count, 'graph sample population')
            for capture in run['captures']:
                artifacts.audit_manifest(read(Path(capture) / 'manifest.json'))
            mutations = read(case / 'mutations.json')
            artifacts.require(mutations['passed'] and mutations['mutations'], 'artifact mutations missing')
            comparison = read(case / 'repeatability/comparison.json')
            artifacts.require(comparison['passed'], 'repeat pixels failed')
            population.update(repeat=run['repeat'], pid=run['pid'], graphSamples=expected_count,
                              actualTimingResponses=len(snapshots), cpuSnapshotPendingRetries=run['cpuSnapshotPendingRetries'],
                              captureTimingRetries=run['captureTimingRetries'],
                              textStatusBusyResponses=sum('retry status shortly' in s['status'] for s in snapshots),
                              mutationRejections=reject_population_mutations(samples),
                              artifactMutationCount=len(mutations['mutations']))
            populations.append(population)
        artifacts.require(read(directory / 'dx12-process-repeatability/comparison.json')['passed'],
                          'independent-process pixels failed')
        representative = Path(runs[0]['captures'][0])
        manifest, _, pixels = artifacts.load_capture(representative)
        depth = pixels['depth']
        foreground = sum(0 < value < 1 for value in depth)
        artifacts.require(foreground >= 64 and manifest['draws'], 'empty representative geometry')
        representatives.append(representative)
        configs[config] = dict(passed=True, runtimeSha256=result['runtimeSha256'],
                               nativeRegressionCases=native_tests['cases'],
                               populations=populations, extent=[manifest['width'], manifest['height']],
                               actualEnvironmentPath=manifest['skyBoxPath'],
                               actualEnvironmentSha256=runs[0]['environmentSha256'],
                               foregroundDepthPixels=foreground,
                               representativePayloads={path.name: sha(path) for path in representative.glob('*.f32')})
    comparison = artifacts.compare(*representatives, root / 'Debug-Release')
    report = dict(schema='temporal.base0.consumer-workload.v1', passed=True,
                  configurations=configs, crossConfiguration=comparison,
                  liveSamples=400, graphCaptures=204, independentProcesses=4,
                  rg6StoredPopulationConsumerVerified=True,
                  typedNativeCpuSurfaceVerified=True,
                  earnedDays=0, TR0Status='progress', phase45EarnedDays=12, phase45TotalDays=71,
                  consumerSourceSha256={name: sha(Path(__file__).parent / name) for name in
                      ('verify-temporal-base0-consumers.py', 'rg6_acceptance.py',
                       'new-temporal-base0-fixture.ps1', 'verify-temporal-base0-workload.ps1')},
                  fullLegacyProductRg6CutoverVerified=False, TR0Complete=False,
                  limits=['Current static authored plane fixture, not the historical lattice LX fixture',
                          'GPU validation enabled; CPU recording and GPU pass sum are asynchronous axes',
                          'No full-frame CPU/submit measurement, master comparison or product budget',
                          'No UI screenshot, actual generated output or external physical acceptance'])
    (root / 'checkpoint.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS actual Debug/Release BASE-0/RG6 consumer workload: 400 live samples, 204 graph captures')


if __name__ == '__main__':
    main()
