"""Reject lens/cut/control mixups against a retained live GPU capture, without cloning pixels."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path

spec=importlib.util.spec_from_file_location('lens',Path(__file__).resolve().parents[1]/'temporal-validation/Compare-TemporalLens.py')
oracle=importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)

def main(root):
    verdict=json.loads((root/'pixels.json').read_text(encoding='utf-8-sig'))
    assert verdict['passed'] and verdict['captures']==18
    records=json.loads((root/'records.json').read_text(encoding='utf-8-sig'))
    original=next(x for x in records if x['name']=='fov-45-1')
    def check(r):return oracle.recovery.read_capture(root,r,'dlss',verdict['quality'])
    manifest=check(original)[0]
    mutations=[
        ('not-armed',lambda r:r['arming'].update(ready=False)),
        ('wrong-ordinal',lambda r:r.update(ordinal=8)),
        ('no-lens-change',lambda r:r['receipt'].update(before=copy.deepcopy(r['receipt']['lens']))),
        ('synthetic-cut',lambda r:r['receipt']['lens'].update(cameraCutRevision='999')),
        ('wrong-fov',lambda r:r['receipt']['lens'].update(fov=90)),
        ('wrong-near',lambda r:r['receipt']['lens'].update(near=.5)),
        ('wrong-far',lambda r:r['receipt']['lens'].update(far=100)),
        ('wrong-request',lambda r:r.update(arguments=['Camera','90','.1','500'])),
        ('mixed-control',lambda r:r['after'].update(requestedGeneration='999')),
        ('mixed-reset',lambda r:r['after'].update(historyResetGeneration='999')),
        ('mixed-resize',lambda r:r['after']['displayExtent'].update(width=1)),
        ('fallback',lambda r:r['after'].update(activeUpscaler='fsr')),
        ('stale-history',lambda r:r['before'].update(historyRevision=r['after']['historyRevision'])),
        ('wrong-view',lambda r:r['before'].update(viewId='999')),
        ('wrong-scene',lambda r:r['before'].update(sceneEpoch='999')),
        ('wrong-trigger',lambda r:r.update(action='unknown')),
    ]
    rejected=[]
    for name,mutate in mutations:
        record=copy.deepcopy(original);mutate(record)
        try:check(record)
        except (ValueError,KeyError):rejected.append(name)
        else:raise AssertionError('Accepted mutation: '+name)
    for index in (0,5,10,14):
        altered=copy.deepcopy(manifest);altered['camera']['projection'][index]+=.1
        try:oracle.recovery.check_lens_projection(altered,original['receipt']['lens'])
        except ValueError:rejected.append(f'projection-{index}')
        else:raise AssertionError('Accepted wrong projection')
    return dict(passed=True,livePositive=1,rejected=rejected,scope='lens receipt/provenance and analytic projection; no cloned pixels')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=main(a.directory);a.output.write_text(json.dumps(result,indent=2));print(json.dumps(result))
