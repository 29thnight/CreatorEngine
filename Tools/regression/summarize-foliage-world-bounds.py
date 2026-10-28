"""Summarize owned Editor measurements, retaining trial variability and axes."""
import json
import math
import pathlib
import statistics
import sys

root = pathlib.Path(sys.argv[1])
rows = json.loads((root / 'measurements.json').read_text(encoding='utf-8-sig'))


def stats(values):
    values = sorted(float(v) for v in values if math.isfinite(float(v)) and float(v) > 0)
    return dict(n=len(values), meanMs=statistics.mean(values), medianMs=statistics.median(values),
                p95Ms=values[math.ceil(.95 * len(values)) - 1], minimumMs=values[0], maximumMs=values[-1])


summary = dict(environment=json.loads((root / 'environment.json').read_text(encoding='utf-8-sig')),
               condition='Fixed camera 10,15,-20; 60-degree FOV; far=60; no shadow light; same visible subset; native resolution',
               methodology='Same Release executable, runtime cache toggle; ABBA; 64 warm frames discarded; 128 culling/game-loop samples per trial; two trials per mode',
               renderAxis='wall time / completed real-view renders; throughput interval, not GPU duration or display present time',
               memoryAxis='allocated component cache array bytes; excludes diagnostic buffers and render caches',
               cases=[])
for count in sorted({r['count'] for r in rows}):
    seed = json.loads((root / f'N{count}-seed.json').read_text(encoding='utf-8-sig'))
    case = dict(count=count, visible=seed['visible'], modes={})
    for mode in ('baseline', 'cached'):
        selected = [r for r in rows if r['count'] == count and r['mode'] == mode]
        assert len(selected) == 2
        assert all(r['observedRenderedFrames'] > 0 for r in selected)
        lives = [json.loads((root / f'N{count}-{r["trial"]}-{mode}-live.json').read_text(encoding='utf-8-sig')) for r in selected]
        assert all(l['gpu']['clockValid'] and l['materialGeometry']['entries'] > 0 for l in lives)
        assert all(l['temporalProvenance']['frameKind'] == 'real' for l in lives)
        trials = [statistics.mean(r['cullingMs']) for r in selected]
        case['modes'][mode] = dict(culling=stats(v for r in selected for v in r['cullingMs']),
                                  gameFrameInterval=stats(v for r in selected for v in r['gameFrameIntervalMs']),
                                  renderedFrameInterval=stats(r['renderedFrameIntervalMs'] for r in selected),
                                  cacheBytes=sorted({r['cacheBytes'] for r in selected}),
                                  cullingTrialMeansMs=trials,
                                  cullingTrialMeanCv=statistics.stdev(trials) / statistics.mean(trials),
                                  sampledGpuBusyMs=[l['gpu']['ms'] for l in lives],
                                  sampledNativeResolution=[l['temporalProvenance'] for l in lives],
                                  processPrivateBytes=[r['processPrivateBytes'] for r in selected])
    b, c = case['modes']['baseline'], case['modes']['cached']
    case['cullingMedianReductionPercent'] = 100 * (1 - c['culling']['medianMs']/b['culling']['medianMs'])
    case['gameFrameMedianReductionPercent'] = 100 * (1 - c['gameFrameInterval']['medianMs']/b['gameFrameInterval']['medianMs'])
    case['renderIntervalMeanReductionPercent'] = 100 * (1 - c['renderedFrameInterval']['meanMs']/b['renderedFrameInterval']['meanMs'])
    summary['cases'].append(case)
(root / 'summary.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding='utf-8')
for case in summary['cases']:
    b, c = case['modes']['baseline'], case['modes']['cached']
    print(json.dumps(dict(N=case['count'], visible=case['visible'],
                         cullMedian=[b['culling']['medianMs'], c['culling']['medianMs']],
                         cullP95=[b['culling']['p95Ms'], c['culling']['p95Ms']],
                         gameMedian=[b['gameFrameInterval']['medianMs'], c['gameFrameInterval']['medianMs']],
                         renderIntervalMean=[b['renderedFrameInterval']['meanMs'], c['renderedFrameInterval']['meanMs']],
                         cacheBytes=[b['cacheBytes'], c['cacheBytes']],
                         cullTrialCV=[b['cullingTrialMeanCv'], c['cullingTrialMeanCv']]), ensure_ascii=False))
