"""Validate production warm METEOR frames against native transform receipts."""
import argparse
import importlib.util
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('ceprof_reader', Path(__file__).with_name('summarize-material-profile.py'))
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)

def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

results = []
for case in sorted(args.directory.glob('N*-R*')):
    run = load(case / 'run.json')
    warm = load(case / 'warm-reuse-verdict.json')
    before = load(case / 'warm-before.json')
    after = load(case / 'warm-after.json')
    cold = load(case / 'cold-transform-verdict.json')
    summary = load(case / 'cold.ceprof.summary.json')
    assert run['passed'] and run['normalQuit'] and run['exitCode'] == 0 and run['sourceStable']
    assert cold['passed'] and summary['complete'] and summary['nativeRealPerformanceGateEligible']
    assert warm['passed'] and warm['stableSeconds'] >= 2 and warm['gpuFramesAdvanced'] > 0 and warm['framesAdvanced'] > 0
    assert before['materialGeometry']['transforms'] == after['materialGeometry']['transforms']
    assert before['materialGeometry']['uploads'] == after['materialGeometry']['uploads']
    assert after['materialGeometry']['cachedOutputBytes'] > 32 * 1024**2
    a, b = before['gpuTemporalProvenance'], after['gpuTemporalProvenance']
    assert a['valid'] and b['valid'] and a['sceneEpoch'] == b['sceneEpoch'] and a['viewId'] == b['viewId']
    first, last, view = int(a['realFrameId']), int(b['realFrameId']), int(b['viewId'])
    assert first == before['gpu']['frame'] and last == after['gpu']['frame'] and first < last
    for snapshot in (before, after):
        assert not any(p['name'].startswith(('METEOR.TransformMeshWorld', 'METEOR.MeshWorldReady')) for p in snapshot['gpu']['passes'])
    receipts = []
    for line in (case / 'editor.stderr.log').read_text(encoding='utf-8-sig', errors='replace').splitlines():
        if not line.startswith('METEOR_TRANSFORM_DISPATCH '):
            continue
        values = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', line)}
        assert values['native'] == 1
        receipts.append(values)
    assert receipts and any(r['jobs'] >= run['count'] for r in receipts), 'Native transform audit was not exercised'
    assert not any(first <= r['frame'] <= last and r['view'] == view for r in receipts), 'Warm production frames dispatched transforms'
    capture = reader.read_capture(case / 'cold.ceprof')
    assert capture['complete'] and capture['unacked'] == 0 and not any(capture['losses'].values())
    gpu = [m for frame in capture['frames'] for m in frame['measurements']
           if m['axis'] == 2 and first <= m['realFrameId'] <= last and m['viewId'] == view]
    recorded = {m['realFrameId'] for m in gpu}
    assert {first, last}.issubset(recorded), 'Warm interval anchors were not recorded on GPU'
    assert all(m['frameKind'] == 1 and m['upscaler'] == 0 and m['frameGenerator'] == 0 for m in gpu)
    names = [capture['markers'][m['marker']]['name'] for m in gpu]
    assert not any(n.startswith(('METEOR.TransformMeshWorld', 'METEOR.MeshWorldReady')) for n in names)
    assert any(n.startswith('METEOR.MeshWorldReuse') for n in names), 'Recorded warm GPU frames did not consume reused outputs'
    result = dict(passed=True, instances=run['count'], iteration=run['iteration'],
                  firstGpuRealFrame=first, lastGpuRealFrame=last, viewId=view,
                  warmTransformPasses=0, warmReadyPasses=0, warmNativeTransformDispatches=0,
                  stableSeconds=warm['stableSeconds'], framesAdvanced=warm['framesAdvanced'],
                  recordedWarmGpuFrames=len(recorded),
                  retainedOutputBytes=after['materialGeometry']['cachedOutputBytes'],
                  sourceStable=True, normalQuit=True, cold=cold['sample'],
                  performanceImprovementAccepted=False)
    (case / 'residency-verdict.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    results.append(result)
assert results and len({r['cold']['runtimeSha256'] for r in results}) == 1
verdict = dict(passed=True, samples=results, scope='METEOR active scene residency; no old/new comparison',
               performanceImprovementAccepted=False)
(args.directory / 'residency-verdict.json').write_text(json.dumps(verdict, indent=2), encoding='utf-8')
print(json.dumps(verdict, indent=2))
