"""RG6 live cutover comparison, with the mode-dependent geometry delta explicit."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import statistics

import base0_artifacts as artifacts


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def distribution(values):
    values = sorted(values)
    artifacts.require(values and all(math.isfinite(v) and v >= 0 for v in values), 'invalid timing')
    return dict(count=len(values), mean=statistics.mean(values), median=statistics.median(values),
                p95=values[math.ceil(.95 * len(values)) - 1], minimum=values[0], maximum=values[-1])


def compare(reference, product, output):
    a, aid, ap = artifacts.load_capture(reference)
    b, bid, bp = artifacts.load_capture(product)
    artifacts.require(aid['inputHash'] == bid['inputHash'], 'sealed inputs differ')
    artifacts.require(a['backend'] == b['backend'] == 'dx12', 'same DX12 backend required')
    ag, bg = a['compiledGraph'], b['compiledGraph']
    artifacts.require(ag['orderContract'] == 'legacy-declaration-order' and
                      bg['orderContract'] == 'dependency-order', 'cutover graph contracts')
    before_passes, after_passes = [Counter(p['name'] for p in g['passes']) for g in (ag, bg)]
    before_resources, after_resources = [Counter(r['name'] for r in g['resources']) for g in (ag, bg)]
    expected_passes = Counter({f'Geometry.Occlusion.{i}': 1 for i in range(6)})
    expected_passes['Geometry.Visibility.CullOcclusion'] = 1
    expected_resources = Counter({f'Geometry.Occlusion.{i}': 1 for i in range(6)})
    expected_resources['GizmoIcon.Texture'] = 2
    artifacts.require(after_passes - before_passes == expected_passes and
                      before_passes - after_passes == Counter({'Geometry.Visibility.Cull': 1}),
                      'unexpected cutover pass delta')
    artifacts.require(after_resources - before_resources == expected_resources and
                      not before_resources - after_resources, 'unexpected cutover resource delta')
    artifacts.require(set(ap) == set(bp), 'stage set changed')
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for name in sorted(ap):
        channels = 1 if name == 'depth' else 4
        maximum = squared = 0.0
        changed = exceeded = 0
        diff = bytearray(a['width'] * a['height'] * 3)
        for pixel in range(a['width'] * a['height']):
            deltas = []
            over = False
            for c in range(channels):
                x, y = ap[name][pixel * channels + c], bp[name][pixel * channels + c]
                delta = abs(x - y)
                maximum = max(maximum, delta)
                squared += delta * delta
                over |= delta > .002 + .005 * max(abs(x), abs(y))
                deltas.append(delta)
            changed += any(d > 0 for d in deltas)
            exceeded += over
            rgb = deltas[:3] if channels == 4 else deltas * 3
            diff[pixel * 3:pixel * 3 + 3] = bytes(min(255, round(d * 255 * 16)) for d in rgb)
        artifacts.png(output / (name + '-difference-x16.png'), a['width'], a['height'], diff)
        results.append(dict(attachment=name, maxError=maximum, rmse=math.sqrt(squared / len(ap[name])),
                            changedPixels=changed, exceededPixels=exceeded))
    for images, label in ((ap, 'reference'), (bp, 'product')):
        rgb = bytes(min(255, max(0, round(v * 255))) for i, v in enumerate(images['display']) if i % 4 != 3)
        artifacts.png(output / (label + '-final.png'), a['width'], a['height'], rgb)
    result = dict(passed=all(r['exceededPixels'] == 0 for r in results), inputHash=aid['inputHash'],
                  reference=str(reference.resolve()), product=str(product.resolve()), outputDirectory=str(output.resolve()),
                  referenceGraphHash=aid['graphHash'], productGraphHash=bid['graphHash'],
                  referenceStats=a['graph'], productStats=b['graph'],
                  productCriticalPath=[bg['passes'][i]['name'] for i in bg['criticalPath']],
                  productDependencyWaves=bg['dependencyWaves'],
                  graphComparison='audited-mode-dependent-geometry-and-icon-declarations-v1',
                  addedPasses=dict(after_passes - before_passes), removedPasses=dict(before_passes - after_passes),
                  addedResources=dict(after_resources - before_resources), attachments=results)
    (output / 'comparison.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    artifacts.require(result['passed'], 'cutover pixel regression')
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    results, comparisons, timings, input_hashes = {}, {}, {}, set()
    source_snapshot = None
    fixture_identity = None
    hardware_identity = None
    for kind in ('Reference', 'Product'):
        for config in ('Debug', 'Release'):
            label = f'{kind}-{config}'
            directory = root / label
            result = read(directory / 'result.json')
            artifacts.require(result['complete'] and result['artifactGatesPassed'] and
                              not result['failures'] and result['sourceChanges'] == 0, label + ' baseline failed')
            artifacts.require(len(result['runs']) == 2 and result['graphSamples'] == 100 and
                              result['performanceSamples'] == 100, 'acceptance sample contract')
            expected = 'legacy-declaration-order' if kind == 'Reference' else 'dependency-order'
            artifacts.require(result['expectedGraphContract'] == expected, label + ' mode mismatch')
            sources = {s['path']: s['sha256'] for s in read(directory / 'source-hashes.json')}
            artifacts.require(source_snapshot is None or sources == source_snapshot, 'A/B source inputs differ')
            source_snapshot = sources
            hardware = read(directory / 'hardware.json')
            artifacts.require(hardware_identity is None or hardware == hardware_identity, 'GPU/driver identity differs')
            hardware_identity = hardware
            for path, expected_sha in sources.items():
                artifacts.require(sha(Path(path)) == expected_sha, 'current source changed: ' + path)
            exe = (root / 'Reference' / f'x64-{config}' / 'Editor' / 'CreatorEditor.exe') if kind == 'Reference' else Path(result['executable'])
            artifacts.require(sha(exe) == result['executableSha256'] and
                              sha(exe.with_name('CreatorEditor.runtime.dll')) == result['runtimeSha256'], label + ' binary mismatch')
            live_samples, capture_samples, per_process = [], [], []
            for run in result['runs']:
                artifacts.require(run['passed'] and run['exitCode'] == 0 and not run['forcedTermination'], 'unclean exit')
                ledger = (run['assetIdentity'], run['environmentSha256'], run['tuningIdentity'])
                artifacts.require(fixture_identity is None or ledger == fixture_identity, 'asset/environment/tuning identity differs')
                fixture_identity = ledger
                case = Path(run['directory'])
                deterministic = read(case / 'determinism.json')
                artifacts.require(deterministic['passed'] and deterministic['samples'] == (100 if run['repeat'] == 0 else 2), 'determinism samples')
                input_hashes.add(deterministic['inputHash'])
                samples = read(case / 'live-performance.json')
                artifacts.require(len(samples) == 100 and len({s['gpuFrame'] for s in samples}) == 100, 'unique live GPU samples')
                artifacts.require(all(s['queryOverflow'] == s['dropped'] == 0 for s in samples), 'GPU coverage lost')
                live_samples.extend(samples)
                captures = [read(Path(p) / 'manifest.json') for p in run['captures']]
                artifacts.require(all(m['compiledGraph']['orderContract'] == expected for m in captures), 'captured mode differs')
                capture_samples.extend(captures)
                per_process.append(dict(repeat=run['repeat'], cpuRecordMs=distribution([s['cpuRecordMs'] for s in samples]),
                                        gpuMs=distribution([s['gpuMs'] for s in samples])))
            pass_times = {}
            for sample in live_samples:
                for p in sample['passes']:
                    pass_times.setdefault(p['name'], []).append(p['ms'])
            timings[label] = dict(scope='GPU-validation-on; live CPU/GPU windows are asynchronous, not paired frame values',
                                 liveCpuRecordMs=distribution([s['cpuRecordMs'] for s in live_samples]),
                                 liveGpuMs=distribution([s['gpuMs'] for s in live_samples]), perProcess=per_process,
                                 livePassGpuMs={name: distribution(v) for name, v in pass_times.items()},
                                 captureCpuRecordMs=distribution([m['measurement']['cpuRecordMs'] for m in capture_samples]),
                                 captureCompileMs=distribution([m['measurement']['cpuGraphCompileMs'] for m in capture_samples]),
                                 captureGpuMs=distribution([m['measurement']['gpuTotalMs'] for m in capture_samples]),
                                 captureQueueSpanMs=distribution([m['measurement']['gpuQueueSpanMs'] for m in capture_samples]),
                                 captureMemoryMB=distribution([m['measurement']['memory']['usedMB'] for m in capture_samples]))
            results[label] = str(directory / 'result.json')
    artifacts.require(len(input_hashes) == 1, 'inputs differ across builds/processes')
    artifacts.require(read(root / 'cutover-mutations.json')['passed'], 'cutover comparator mutation gate')
    for config in ('Debug', 'Release'):
        for repeat in (0, 1):
            label = f'{config}-process-{repeat}'
            comparisons[label] = compare(root / f'Reference-{config}' / f'dx12-{repeat}' / 'capture-0',
                                         root / f'Product-{config}' / f'dx12-{repeat}' / 'capture-0', root / label)
    cross = artifacts.compare(root / 'Product-Debug/dx12-0/capture-0', root / 'Product-Release/dx12-0/capture-0', root / 'Debug-Release')
    artifacts.require(read(root / 'baseline-phase-complete.json')['phaseComplete'], 'joint BASE-0 gate missing')
    verdict = dict(schemaVersion=1, rg6Complete=True, earnedDays=4, results=results,
                   inputHash=next(iter(input_hashes)), comparisons=comparisons, productCrossConfiguration=cross,
                   timings=timings, comparatorSha256=sha(Path(__file__)),
                   scope='same current source, diagnostic legacy build versus default dependency build; mode-dependent geometry included',
                   excluded=['historical whole-PR performance attribution', 'MAT-9 performance/quality', 'RG-V UI', 'PHASE 4.9 Vulkan'])
    (root / 'final-result.json').write_text(json.dumps(verdict, indent=2), encoding='utf-8')
    print(json.dumps(dict(rg6Complete=True, output=str(root / 'final-result.json'))))


if __name__ == '__main__':
    main()
