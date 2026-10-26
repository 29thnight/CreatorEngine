"""Check identical Seal instrumentation, boundaries and preserved warm caches."""
import argparse,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('root',type=Path);r=p.parse_args().root.resolve()
load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
a=load(r/'analysis.json');evidence=[]
for run in a['runs']:
 f=load(r/run['name']/'frames.json')
 assert all(x['beginQpc']<x['sealEndQpc']<x['prepareBeginQpc'] for x in f)
 if not run['name'].endswith('-on'):continue
 assert run['fullCapture']
 for m,c in [('MaterialSceneInputSeal',1),('GeometryScaleFixtureSeal',1),('MaterialSceneSealGeometryIdentities',1),('MaterialSceneSealPinValidation',1024),('MaterialMeshPlanForScene',1024),('MaterialClusterRequestSort',0),('GeometryFeedbackReadbackCreate',0),('MeshPrepareOwner',1025)]:
  actual=[x['count'] for x in run['allThreadFrameScopes'][m]]
  assert actual==[c]*run['frames'],(run['name'],m,actual)
 evidence.append(dict(name=run['name'],frames=run['frames'],sealCalls=1,identityValidationCalls=1,pinValidationCalls=1024,planCalls=1024,requestMisses=0,readbackCreates=0))
assert len(a['runs']) in [8,16] and len(evidence)==len(a['runs'])//2
(r/'seal-regression.json').write_text(json.dumps(dict(schema=1,passed=True,identicalTimedMarkers=True,exactSealQpcBoundaries=True,evidence=evidence),ensure_ascii=False,indent=2),encoding='utf-8')
print(f'SEAL_REUSE_OK captures={len(evidence)} identicalScopes=1 exactSealQpc=1 warmCaches=1')
