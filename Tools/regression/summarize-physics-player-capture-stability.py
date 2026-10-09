import json
import statistics
import argparse
import hashlib
from pathlib import Path

root = Path(__file__).resolve().parents[2]
base = root / 'Build/Verification/ContactStream/M3Acceptance'
files = ['long-capture-distribution.json', 'long-capture-repeat-distribution.json', 'long-capture-third-distribution.json']
parser = argparse.ArgumentParser()
parser.add_argument('--base', type=Path, default=base)
parser.add_argument('--files', nargs=3, default=files)
parser.add_argument('--output', default='distribution-stability.json')
args = parser.parse_args()
base, files = args.base, args.files
runs = [json.loads((base/name).read_text(encoding='utf-8-sig')) for name in files]
for run in runs:
    if not run['complete'] or run['frames'] < 1000 or run['gpuSpans'] == 0:
        raise SystemExit('Complete long GPU capture required')
    for key in ('Capture.UnackedCpuStreams', 'Capture.FailedGpuSubmissions', 'Capture.PendingGpuSubmissions'):
        if run['closureCounters'].get(key) != 0:
            raise SystemExit('Capture closure loss')
metrics = {
    'physicsTick': [r['inclusiveMarkerDurations']['CPU:PhysicsTick'] for r in runs],
    'physicsFetchWait': [r['inclusiveMarkerDurations']['CPU:Physics.FetchWait'] for r in runs],
    'renderThread': [r['inclusiveMarkerDurations']['CPU:RenderThreadFrame'] for r in runs],
    'gpuInstrumentedUnion': [r['gpuSubmissionInstrumentedUnion'] for r in runs],
    'frameBoundaryIncludesPacing': [r['frameBoundaryDuration'] for r in runs],
}
summary = {}
for name, distributions in metrics.items():
    summary[name] = {}
    for metric in ('meanUs', 'p99Us'):
        values = [r[metric] for r in distributions]
        cv = statistics.stdev(values)/statistics.mean(values)*100
        summary[name][metric] = {'values': values, 'median': statistics.median(values), 'cvPercent': cv, 'stable': cv <= 10}
result = {'result':'M3_PLAYER_DISTRIBUTION_STABILITY_MEASURED', 'independentRuns':3,
          'analysisFiles':files, 'summary':summary, 'stabilityPolicy':'CV <= 10 percent per metric across independent process runs; unstable metrics not accepted',
          'analysisSha256':{name:hashlib.sha256((base/name).read_bytes()).hexdigest() for name in files},
          'frames':sum(r['frames'] for r in runs), 'gpuSpans':sum(r['gpuSpans'] for r in runs),
          'performanceAccepted':False,
          'scope':'Release dynamic/mixed query transition plus 1000 completed display frames; instrumented inclusive scopes; GPU interval union is not presentation latency',
          'remaining':['Explicit performance budget acceptance', 'Unstable tail attribution; no outlier deletion or threshold relaxation']}
(base/args.output).write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(summary,indent=2))
