"""Isolated mutation checks against recorded product inputs; never edits production evidence."""
import argparse,copy,hashlib,importlib.util,json,pathlib
from unittest.mock import patch
p=argparse.ArgumentParser();p.add_argument('--work',type=pathlib.Path,required=True);a=p.parse_args()
spec=importlib.util.spec_from_file_location('pacing',pathlib.Path(__file__).with_name('verify-n3-pacing.py'));gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
work=a.work.resolve();run=work/'fps-144';original=gate.load;trace=original(run/'trace.json');managed=original(run/'managed.json');manifest=original(run/'pbr/manifest.json');validation=original(run/'validation.json')
before={p.relative_to(work).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in work.rglob('*') if p.is_file()}
gate.validate(run,144)
cases=[]
def case(name,filename,mutate,expected):
 data=copy.deepcopy(original(run/filename));mutate(data)
 def load(path):return copy.deepcopy(data) if path.resolve()==(run/filename).resolve() else original(path)
 try:
  with patch.object(gate,'load',load):gate.validate(run,144)
 except (AssertionError,ValueError) as e:
  if expected not in str(e):raise AssertionError(name+' failed for wrong reason: '+str(e))
  cases.append({'name':name,'result':'rejected','reason':str(e)});return
 raise AssertionError('Canary accepted: '+name)
case('missing tick','trace.json',lambda x:x['ticks'].pop(),'tick commit order/count')
case('SDK skipped tick','trace.json',lambda x:x['ticks'][30].update(physicsTick=0),'SDK step count')
def managed_change(x,change):
 m=json.loads(x['returnValue']);change(m);x['returnValue']=json.dumps(m)
case('duplicate OnTickSimulate','managed.json',lambda x:managed_change(x,lambda m:m['updates'].append(m['updates'][-1])),'OnTickSimulate skipped/duplicated')
case('frame delta OnTickSimulate','managed.json',lambda x:managed_change(x,lambda m:m.update(maxDt=1/144)),'OnTickSimulate frame dt leaked')
case('fake requested FPS','trace.json',lambda x:[f.update(seconds=f['seconds']*2) for f in x['frames']],'actual cadence')
case('CCT interpolation removed','trace.json',lambda x:[f.update(render=f['simulation']) for f in x['frames']],'CCT child did not inherit')
case('CCT and child snap','trace.json',lambda x:[f.update(render=f['simulation'],childRender=f['simulation']) for f in x['frames']],'CCT render interpolation error')
case('child publication removed','trace.json',lambda x:[f.update(childRender=[0,0,0]) for f in x['frames']],'CCT child did not inherit')
case('GPU consumes stale proxy','pbr/manifest.json',lambda x:[d['world'].__setitem__(12,100) for d in x['draws']],'GPU draw did not consume')
case('GPU validation failure','validation.json',lambda x:x.update(messages=1),'GPU validation failed')
case('GPU projection drift','pbr/manifest.json',lambda x:x['camera']['projection'].__setitem__(0,x['camera']['projection'][0]*2),'pixel silhouette differs')
case('camera uses simulation pose','trace.json',lambda x:[f.update(cameraEye=[f['simulation'][0],f['simulation'][1]+1.95,-10]) for f in x['frames']],'CCT camera did not consume')
case('GPU camera binding drift','pbr/manifest.json',lambda x:x['camera']['view'].__setitem__(12,x['camera']['view'][12]+.5),'GPU camera did not consume')
case('AI frustum receives render alpha','trace.json',lambda x:[f.update(simulationFrustumOrigin=f['renderFrustumOrigin']) for f in x['frames']],'simulation frustum received render interpolation')
case('removed CCT leaves stale GPU pose','transitions/remove/pbr/manifest.json',lambda x:[d['world'].__setitem__(12,100) for d in x['draws']],'CCT transition draw stale')
viewport=next(run.glob('viewport-game-*')).relative_to(run).as_posix()
case('hidden scene camera demanded',viewport+'/demand.json',lambda x:x.update(editorTarget=True),'hidden viewport still demanded')
case('resumed viewport uses stale camera',viewport+'/manifest.json',lambda x:x.update(viewId=1),'viewport resume consumed wrong camera')
after={p.relative_to(work).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in work.rglob('*') if p.is_file()}
assert before==after,'Oracle mutation checks changed product evidence'
out={'result':'N3_PACING_ORACLE_CANARIES_OK','cases':cases,'productionEvidenceChanged':False}
(work/'oracle-canaries.json').write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8');print('N3_PACING_ORACLE_CANARIES_OK cases='+str(len(cases)))
