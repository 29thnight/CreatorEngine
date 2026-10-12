"""Reject corrupt transition receipts using one accepted live GPU reset capture."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path

spec=importlib.util.spec_from_file_location('transitions',Path(__file__).resolve().parents[1]/'temporal-validation/Compare-TemporalTransitions.py')
oracle=importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)

def main(root):
    verdict=json.loads((root/'pixels.json').read_text(encoding='utf-8-sig'))
    assert verdict['passed'] and verdict['captures']==12
    records=json.loads((root/'records.json').read_text(encoding='utf-8-sig'))
    original=next(r for r in records if r['name']=='quality-1')
    def check(r): return oracle.recovery.read_capture(root,r,'dlss',verdict['quality'])
    check(original)
    mutations=[
        ('not-armed',lambda r:r['arming'].update(ready=False)),
        ('wrong-ordinal',lambda r:r.update(ordinal=8)),
        ('same-quality',lambda r:r.update(fromQuality=verdict['quality'])),
        ('wrong-source-quality',lambda r:r.update(fromQuality='invalid')),
        ('unapplied-receipt',lambda r:r['receipt'].update(receiptGeneration=str(int(r['receipt']['receiptGeneration'])+1))),
        ('superseded',lambda r:r['after'].update(requestedGeneration=str(int(r['after']['requestedGeneration'])+1))),
        ('fallback',lambda r:r['after'].update(activeUpscaler='fsr')),
        ('fxaa',lambda r:r['after'].update(fxaaApplied=True)),
        ('no-temporal-aa',lambda r:r['after'].update(temporalAaApplied=False)),
        ('stale-history',lambda r:r['before'].update(historyRevision=r['after']['historyRevision'])),
        ('wrong-view',lambda r:r['before'].update(viewId=str(int(r['before']['viewId'])+1))),
        ('wrong-scene',lambda r:r['before'].update(sceneEpoch=str(int(r['before']['sceneEpoch'])+1))),
        ('wrong-trigger',lambda r:r.update(action='unknown')),
    ]
    rejected=[]
    for name,mutate in mutations:
        r=copy.deepcopy(original);mutate(r)
        try:check(r)
        except (ValueError,KeyError):rejected.append(name)
        else:raise AssertionError('Accepted mutation: '+name)
    return dict(passed=True,livePositive=1,rejected=rejected,scope='transition receipt, quality, reset and provenance admission; no cloned pixels')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    r=main(a.directory);a.output.write_text(json.dumps(r,indent=2));print(json.dumps(r))
