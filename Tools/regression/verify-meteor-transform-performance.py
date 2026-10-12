"""Join native dispatch receipts to exact first-cold-frame CEPROF measurements."""
import argparse
import csv
from datetime import datetime, timedelta, timezone
import importlib.util
import json
import math
from pathlib import Path
import re
import statistics

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
parser.add_argument('--iterations', type=int, default=4)
parser.add_argument('--counts', type=int, nargs='+')
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('ceprof_reader', Path(__file__).with_name('summarize-material-profile.py'))
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def summarize(values):
    mean = statistics.mean(values)
    deviation = statistics.stdev(values) if len(values) > 1 else 0
    return dict(samples=values, meanMs=mean, minMs=min(values), maxMs=max(values),
                cvPercent=deviation / mean * 100 if mean else 0)


def power_bracket(case, measurement, frequency):
    anchor_file = case / 'power-clock-anchor.json'
    telemetry_file = case / 'gpu-power.csv'
    if not anchor_file.exists() or not telemetry_file.exists():
        return dict(available=False, exactKernelClockKnown=False)
    anchor = json.loads(anchor_file.read_text(encoding='utf-8-sig'))
    assert anchor['frequency'] == frequency
    utc = datetime.fromisoformat(anchor['utc'])
    begin = utc + timedelta(seconds=(measurement['begin'] - anchor['qpc']) / frequency)
    end = utc + timedelta(seconds=(measurement['end'] - anchor['qpc']) / frequency)
    rows = []
    for cells in csv.reader(telemetry_file.read_text(encoding='utf-8-sig').splitlines()):
        if len(cells) != 9:
            continue
        cells = [c.strip() for c in cells]
        try:
            # NVML CSV wall timestamps are local; anchor and comparisons are UTC.
            stamp = datetime.strptime(cells[0], '%Y/%m/%d %H:%M:%S.%f').astimezone(timezone.utc)
            rows.append(dict(utc=stamp.isoformat(), name=cells[1], driver=cells[2], pstate=cells[3],
                             graphicsMHz=int(cells[4]), memoryMHz=int(cells[5]), utilizationPercent=int(cells[6]),
                             powerWatts=float(cells[7]), temperatureC=float(cells[8])))
        except ValueError:
            continue
    before = [r for r in rows if datetime.fromisoformat(r['utc']) <= begin]
    after = [r for r in rows if datetime.fromisoformat(r['utc']) >= end]
    a, b = (before[-1] if before else None), (after[0] if after else None)
    intervals = [(datetime.fromisoformat(b['utc']) - datetime.fromisoformat(a['utc'])).total_seconds() * 1000
                 for a, b in zip(rows, rows[1:])]
    return dict(available=bool(a and b), before=a, after=b, exactKernelClockKnown=False,
                nominalSamplingMilliseconds=100,
                observedMedianSamplingMilliseconds=statistics.median(intervals) if intervals else None,
                kernelBeginUtc=begin.isoformat(), kernelEndUtc=end.isoformat(),
                pstateBracketEqual=bool(a and b and a['pstate'] == b['pstate']),
                memoryClockBracketEqual=bool(a and b and a['memoryMHz'] == b['memoryMHz']))


samples = []
for run_path in sorted(args.directory.glob('N*-R*/run.json')):
    case = run_path.parent
    run = json.loads(run_path.read_text(encoding='utf-8-sig'))
    if args.counts and run['count'] not in args.counts:
        continue
    assert run['passed'] and run['normalQuit'] and run['exitCode'] == 0 and run['validation'] == 'off'
    assert run.get('sourceStable', True)
    if args.iterations >= 4:
        assert run.get('sourceStable') is True, 'Missing current source identity guard'
    summary = json.loads((case / 'cold.ceprof.summary.json').read_text(encoding='utf-8-sig'))
    assert summary['nativeRealPerformanceGateEligible'], 'Incomplete native-real recording provenance'
    capture = reader.read_capture(case / 'cold.ceprof')
    assert capture['complete'] and capture['unacked'] == 0
    assert not any(capture['losses'].values()), 'Recording lost events/frames/counters'
    idle_path = case / 'recording-idle.json'
    if idle_path.exists():
        idle = json.loads(idle_path.read_text(encoding='utf-8-sig'))
        after = json.loads((case / 'after.json').read_text(encoding='utf-8-sig'))
        assert int(idle['gpuTemporalProvenance']['sceneEpoch']) > int(after['temporalProvenance']['sceneEpoch'])
        assert not any(p['name'].startswith('METEOR.TransformMeshWorld') for p in idle['gpu']['passes'])
    native = []
    for line in (case / 'editor.stderr.log').read_text(encoding='utf-8-sig', errors='replace').splitlines():
        if line.startswith('METEOR_TRANSFORM_DISPATCH '):
            receipt = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', line)}
            assert receipt['native'] == 1
            if receipt['jobs'] >= run['count']:
                native.append(receipt)
    assert native, 'No simultaneous first-transform work list was recorded'
    measurements = [m for frame in capture['frames'] for m in frame['measurements']]
    def name(measurement):
        return capture['markers'][measurement['marker']]['name']
    cold = None
    for receipt in native:
        matched = [m for m in measurements if m['realFrameId'] == receipt['frame'] and
                   m['viewId'] == receipt['view'] and m['axis'] == 2 and name(m).startswith('METEOR.TransformMeshWorld')]
        if matched:
            assert len(matched) == 1, 'Transform pass count grew within the exact frame/view'
            cold = receipt, matched[0]
            break
    assert cold, 'Native receipt cannot be joined to an exact recorded GPU frame/view'
    receipt, transform = cold
    same = [m for m in measurements if m['realFrameId'] == receipt['frame'] and m['viewId'] == receipt['view']]
    gpu = [m for m in same if m['axis'] == 2]
    ready = [m for m in gpu if name(m).startswith('METEOR.MeshWorldReady')]
    cpu = [m for m in same if m['axis'] == 1]
    assert len(ready) == 1 and len(cpu) == 1
    assert all(m['frameKind'] == 1 and m['upscaler'] == 0 and m['frameGenerator'] == 0 for m in same)
    milliseconds = lambda m: (m['end'] - m['begin']) * 1000 / capture['hz']
    pixel = json.loads((case / 'live/manifest.json').read_text(encoding='utf-8-sig'))
    assert pixel['finite'] and pixel['validationCount'] == 0
    assert not any('Deferred' in p['name'] for p in pixel['compiledGraph']['passes'])
    selected = pixel['geometryRoutes']['selected']
    assert selected and all(p['meshShader'] and p['indirect'] for p in selected)
    sample = dict(count=run['count'], iteration=run['iteration'], runtimeSha256=run['runtimeSha256'],
                  profileSha256=summary['sha256'], firstCold=receipt, transformPasses=1, readyPasses=1,
                  nativeDispatches=1, transformGpuMs=milliseconds(transform),
                  coldViewCpuRecordSubmitMs=milliseconds(cpu[0]),
                  coldViewGpuPassSumMs=sum(map(milliseconds, gpu)),
                  gpuPassSumIsWallTime=False, recordedGpuPassCount=len(gpu),
                  directory=str(case), normalQuit=True, worldOutputCold=True, shaderAndStaticSourceWarmed=True)
    sample['powerTelemetry'] = power_bracket(case, transform, capture['hz'])
    assert math.isfinite(sample['transformGpuMs']) and sample['transformGpuMs'] > 0
    (case / 'cold-transform-verdict.json').write_text(json.dumps(dict(passed=True, sample=sample), indent=2), encoding='utf-8')
    samples.append(sample)

assert samples and len({row['runtimeSha256'] for row in samples}) == 1
groups = []
for count in sorted({row['count'] for row in samples}):
    rows = sorted((row for row in samples if row['count'] == count), key=lambda row: row['iteration'])
    assert len(rows) == args.iterations and [r['iteration'] for r in rows] == list(range(1, args.iterations + 1))
    assert len({(r['firstCold']['jobs'], r['firstCold']['points'], r['firstCold']['groups']) for r in rows}) == 1
    gpu = summarize([row['transformGpuMs'] for row in rows])
    cpu = summarize([row['coldViewCpuRecordSubmitMs'] for row in rows])
    groups.append(dict(instances=count, jobs=rows[0]['firstCold']['jobs'], points=rows[0]['firstCold']['points'],
                       transformPasses=1, readyPasses=1, nativeDispatches=1, transformGpu=gpu,
                       fullColdViewCpuRecordSubmit=cpu, gpuRepeatStable=len(rows) >= 4 and gpu['cvPercent'] <= 10,
                       cpuRepeatStable=len(rows) >= 4 and cpu['cvPercent'] <= 10))

verdict = dict(passed=True, countsAndProvenancePassed=True, groups=groups, samples=samples,
               allGpuRepeatsStable=all(g['gpuRepeatStable'] for g in groups),
               oldNewPerformanceComparison=False, performanceImprovementAccepted=False,
               latencyAcceptance=False, productBudgetAccepted=False,
               scope='New METEOR dispatch only; first new world outputs after helper PSO/static-source warmup; independent process repeats')
(args.directory / 'performance-verdict.json').write_text(json.dumps(verdict, indent=2), encoding='utf-8')
print(json.dumps(dict(verdict, samples=[]), indent=2))
