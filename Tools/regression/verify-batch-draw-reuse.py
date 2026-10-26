"""Verify warmed batch/draw reuse without discarding per-instance owners."""
import argparse, bisect, json
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument('root',type=Path); root=p.parse_args().root.resolve()
load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
a=load(root/'analysis.json'); evidence=[]
for r in a['runs']:
    if not r['name'].endswith('-on'): continue
    assert r['fullCapture']
    phases=r['allThreadFrameScopes']
    for marker,count in [('MaterialClusterRequestSort',0),('GeometryFeedbackReadbackCreate',0),('MeshPrepareOwner',1025)]:
        actual=[f['count'] for f in phases[marker]]
        assert actual==[count]*r['frames'],(r['name'],marker,actual)
    d=root/r['name']; c=load(d/'native.json'); frames=load(d/'frames.json')
    names=[m['name'] for m in c['markers']]; starts=[f['beginQpc'] for f in frames]
    counters={n:[0]*len(frames) for n in ['MaterialSceneDrawRecords.Reuse','MaterialSceneDrawRecords.Build']}
    seen=set()
    for frame in c['frames']:
        for e in frame['events']:
            n=names[e['marker']]
            if n not in counters: continue
            key=(e['slot'],e['marker'],e['begin'],e['end'])
            if key in seen: continue
            seen.add(key); i=bisect.bisect_right(starts,e['begin'])-1
            if 0<=i<len(frames) and e['begin']<=frames[i]['endQpc']: counters[n][i]+=1
    after=r['name'].startswith('after-')
    assert counters['MaterialSceneDrawRecords.Reuse']==[int(after)]*r['frames'],(r['name'],counters)
    assert counters['MaterialSceneDrawRecords.Build']==[0]*r['frames'],(r['name'],counters)
    evidence.append(dict(name=r['name'],instanceBatchObjectsPerFrame=1025,readbackCreates=0,requestMisses=0,recordCounters=counters))
assert len(evidence)==8
(root/'reuse-regression.json').write_text(json.dumps(dict(schema=1,passed=True,evidence=evidence,baselineHasRecordCounters=False,afterNewInstantPerFrame=1),ensure_ascii=False,indent=2),encoding='utf-8')
print('BATCH_DRAW_REUSE_OK captures=8 wrappers=1025 warmRecordReuse=1 rebuild=0')
