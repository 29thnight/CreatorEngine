"""Independent real-time N3 gate: wall cadence, tick trajectories, interpolated frames, GPU silhouettes."""
import argparse,json,pathlib,math,copy
import numpy as np
from PIL import Image,ImageDraw

def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def require(v,msg):
 if not v:raise AssertionError(msg)
def validate(run,fps,moving=False):
 pacing=load(run/'pacing.json');require(pacing['playFps']==fps and pacing['appliedMode']=='fixed' and pacing['appliedFps']==fps,'Play pacing not applied')
 d=load(run/'trace.json');ticks=d['ticks'];frames=d['frames'];first=d['firstTick'];last=d['lastTick']
 require(len(ticks)==120 and [t['tick'] for t in ticks]==list(range(first,last+1)),'tick commit order/count')
 require([t['physicsTick']-ticks[0]['physicsTick'] for t in ticks]==list(range(120)),'SDK step count differs from fixed clock')
 managed=load(run/'managed.json');m=json.loads(managed['returnValue']);updates=[{'tick':u[0],'frame':u[1]} for u in m['updates'] if first<=u[0]<=last]
 require(m['failures']==0 and [u['tick'] for u in updates]==list(range(first,last+1)),'OnTickSimulate skipped/duplicated or bad dt')
 require(abs(m['minDt']-1/60)<1e-7 and abs(m['maxDt']-1/60)<1e-7,'OnTickSimulate frame dt leaked')
 by={t['tick']:t for t in ticks};active=[f for f in frames if first+4<=f['committed']<last-2]
 require(len(active)>fps,'insufficient real frames')
 elapsed=active[-1]['seconds']-active[0]['seconds'];measured=(len(active)-1)/elapsed
 if not moving:require(abs(measured/fps-1)<.05,f'actual cadence {measured:.2f} outside requested {fps} +/-5%')
 wall=np.diff([f['seconds'] for f in active]);published=np.diff([f['published'] for f in active])
 require(np.all(wall>0) and np.all(published>=0) and np.count_nonzero(published)>len(published)*.90,'not observing real published frames')
 errors=[];body_errors=[];camera_errors=[];zero=multi=moving_zero=0
 for prev,f in zip(frames,frames[1:]):
  t=f['committed'];a=f['alpha'];require(0<=a<=1,'invalid interpolation alpha')
  if t not in by or t==first:continue
  cur=np.array(by[t]['position']);prior=np.array(by[t-1]['position'])
  if t-first+1==100:prior=np.array([-2,1.05,0])
  expected=prior+(cur-prior)*a
  errors.append(float(np.max(np.abs(np.array(f['render'])-expected))))
  camera_errors.append(float(np.max(np.abs(np.array(f['cameraEye'])-(expected+np.array([0,1.95,-10]))))))
  require(camera_errors[-1]<2e-4,'CCT camera did not consume interpolated pose')
  require(abs(f['cameraLodDistance']-np.linalg.norm([0,1.95,-10]))<2e-4,'camera LOD uses a different time axis')
  require(np.max(np.abs(np.array(f['renderFrustumOrigin'])-np.array(f['cameraEye'])))<2e-4,'render frustum uses a different time axis')
  require(np.max(np.abs(np.array(f['simulationFrustumOrigin'])-(np.array(f['simulation'])+np.array([0,1.95,-10]))))<2e-4,'simulation frustum received render interpolation')
  require(f['cameraCut']==frames[0]['cameraCut']+(1 if t-first+1>=100 else 0),'camera cut changed on ordinary interpolation or missed teleport')
  require(np.max(np.abs(np.array(f['childRender'])-np.array(f['render'])))<2e-4,'CCT child did not inherit interpolated matrix')
  bc=np.array(by[t]['body']);bp=np.array(by[t-1]['body']);body_errors.append(float(np.max(np.abs(np.array(f['bodyRender'])-(bp+(bc-bp)*a)))))
  step=t-prev['committed'];zero+=step==0;multi+=step>1
  if step==0 and np.linalg.norm(np.array(f['render'])-np.array(prev['render']))>1e-4:moving_zero+=1
 require(max(errors)<2e-4,f'CCT render interpolation error {max(errors):.6f}')
 require(max(body_errors)<2e-4,f'rigid render interpolation error {max(body_errors):.6f}')
 if fps==144:require(zero>80 and moving_zero>60,'zero-tick frames must still publish moving interpolated CCT')
 if fps==30:require(multi>35,'30fps did not exercise multiple ticks per frame')
 positions=np.array([t['position'] for t in ticks]);require(positions[50,0]>1 and positions[90,0]<positions[59,0],'CCT acceleration/reversal missing')
 require(positions[40,1]>1.5 and ticks[20]['below'],'ground/jump path missing')
 val=load(run/'validation.json');require(val['layerEnabled'] and not any(val[k] for k in ('problems','messages','droppedMessages')),'GPU validation failed')
 capture='moving-pbr' if moving else 'pbr'
 manifest=load(run/capture/'manifest.json');require(manifest['source']=='product-live' and len(manifest['draws'])>=2,'real diagnostic draws missing')
 base=next(a for a in manifest['attachments'] if a['name']=='baseColor');w,h=base['width'],base['height'];c=base['channels']
 rgb=np.fromfile(run/capture/base['file'],dtype='<f4').reshape(h,w,c)[:,:,:3];require(np.isfinite(rgb).all(),'nonfinite GPU pixels')
 view=np.array(manifest['camera']['view']).reshape(4,4);proj=np.array(manifest['camera']['projection']).reshape(4,4)
 quality=[];lastframe=frames[-1];gpu_zero=False
 if moving:
  matching=[(i,f) for i,f in enumerate(frames) if f['engineFrame']==manifest['sourceEngineFrame']]
  require(bool(matching),'moving GPU frame missing from native trace')
  index,lastframe=matching[0];gpu_zero=index>0 and lastframe['committed']==frames[index-1]['committed']
  require(gpu_zero,'moving GPU capture must consume a zero-tick presentation frame')
 require(np.max(np.abs(np.linalg.inv(view)[3,:3]-np.array(lastframe['cameraEye'])))<3e-4,'GPU camera did not consume recorded interpolated pose')
 for color,channel,key in [('green',1,'childRender'),('red',0,'bodyRender')]:
  expected=np.array(lastframe[key]);draw=min(manifest['draws'],key=lambda x:np.linalg.norm(np.array(x['world'][12:15])-expected))
  require(np.max(np.abs(np.array(draw['world'][12:15])-expected))<3e-4,'GPU draw did not consume recorded interpolated pose')
  world=np.array(draw['world']).reshape(4,4);corners=np.array([[x,y,z,1] for x in (-.4,.4) for y in (-.4,.4) for z in (-.4,.4)])
  clip=corners@world@view@proj;require((clip[:,3]>0).all(),'fixture behind camera');ndc=clip[:,:2]/clip[:,3:4]
  pixels=(ndc*np.array([.5,-.5])+np.array([.5,.5]))*np.array([w,h]);lo=pixels.min(0);hi=pixels.max(0)
  other=[v for v in range(3) if v!=channel];mask=(rgb[:,:,channel]>.15)&(rgb[:,:,channel]>3*rgb[:,:,other[0]])&(rgb[:,:,channel]>3*rgb[:,:,other[1]])
  ys,xs=np.where(mask);require(len(xs)>30,'visible '+color+' GPU silhouette missing')
  observed=np.array([xs.min(),ys.min(),xs.max()+1,ys.max()+1]);predicted=np.r_[lo,hi]
  require(np.max(np.abs(observed-predicted))<3.5,f'{color} pixel silhouette differs from projection: {observed} vs {predicted}')
  quality.append({'color':color,'pixels':len(xs),'maxBoundsErrorPx':float(np.max(np.abs(observed-predicted)))})
 Image.fromarray((np.clip(rgb,0,1)*255).astype('uint8')).save(run/'baseColor.png')
 lifetime=transitions(run,d) if fps==144 else []
 if fps==144:viewport_transitions(run)
 return {'fps':fps,'measuredFps':measured,'frameP95ms':float(np.percentile(wall,95)*1000),'frames':len(frames),'zeroTickFrames':zero,'multiTickFrames':multi,'movingZeroTickFrames':moving_zero,'maxCctInterpolationError':max(errors),'maxRigidInterpolationError':max(body_errors),'maxCameraInterpolationError':max(camera_errors),'quality':quality,'movingGpuZeroTick':gpu_zero,'lifetime':lifetime,'positions':positions,'body':np.array([t['body'] for t in ticks])}
def viewport_transitions(run):
 folders=list(run.glob('viewport-*'))
 require(len(folders)==4,'missing viewport resume captures')
 for folder in folders:
  demand=load(folder/'demand.json');manifest=load(folder/'manifest.json')
  scene=demand['mode']=='scene'
  require(demand['mode'] in ('scene','game') and demand['hostPresent'] and demand['editorTarget']==scene and demand['gameTarget']!=scene,'hidden viewport still demanded')
  require(manifest['source']=='product-live' and manifest['sourceEngineFrameAvailable'] and manifest['finite'] and not manifest['validationCount'],'viewport resume GPU capture invalid')
  require(manifest['viewId']==(1 if scene else 2),'viewport resume consumed wrong camera')

def transitions(run,d):
 result=[];current=np.array(d['ticks'][-1]['position']);previous=np.array(d['ticks'][-2]['position'])
 for directory in sorted((run/'transitions').iterdir()):
  expected=load(directory/'expected.json');require(np.max(np.abs(np.array(expected['simulation'])-current))<2e-4,'lifetime transition changed simulation position')
  target=previous if directory.name=='prepare' else current
  manifest=load(directory/'pbr/manifest.json');draw=min(manifest['draws'],key=lambda x:np.linalg.norm(np.array(x['world'][12:15])-target))
  require(np.max(np.abs(np.array(draw['world'][12:15])-target))<2e-4,'CCT transition draw stale: '+directory.name)
  view=np.array(manifest['camera']['view']).reshape(4,4);proj=np.array(manifest['camera']['projection']).reshape(4,4)
  require(np.max(np.abs(np.linalg.inv(view)[3,:3]-(target+np.array([0,1.95,-10]))))<3e-4,'CCT transition camera stale: '+directory.name)
  attachment=next(a for a in manifest['attachments'] if a['name']=='baseColor');w,h=attachment['width'],attachment['height']
  rgb=np.fromfile(directory/'pbr'/attachment['file'],dtype='<f4').reshape(h,w,attachment['channels'])[:,:,:3];require(np.isfinite(rgb).all(),'lifetime nonfinite pixels')
  mask=(rgb[:,:,1]>.15)&(rgb[:,:,1]>3*rgb[:,:,0])&(rgb[:,:,1]>3*rgb[:,:,2]);ys,xs=np.where(mask);require(len(xs)>30,'lifetime green mesh missing')
  corners=np.array([[x,y,z,1] for x in (-.4,.4) for y in (-.4,.4) for z in (-.4,.4)])
  clip=corners@np.array(draw['world']).reshape(4,4)@view@proj;ndc=clip[:,:2]/clip[:,3:4];pixels=(ndc*np.array([.5,-.5])+np.array([.5,.5]))*np.array([w,h]);predicted=np.r_[pixels.min(0),pixels.max(0)];observed=np.array([xs.min(),ys.min(),xs.max()+1,ys.max()+1]);error=float(np.max(np.abs(observed-predicted)))
  require(error<3.5,'lifetime pixel silhouette mismatch')
  result.append({'transition':directory.name,'pixels':len(xs),'boundsErrorPx':error})
 require(bool(result),'lifetime captures missing');return result

def check(work):
 record=load(work/'recorded.json');results=[validate(work/f'fps-{fps}',fps,record.get('movingQuality',False)) for fps in record['rates']]
 baseline=results[0];comparisons=[]
 for value in results[1:]:
  error=float(np.max(np.abs(value['positions']-baseline['positions'])));require(error<2e-4,'FPS-dependent CCT tick trajectory')
  a=value['body']-value['body'][0];b=baseline['body']-baseline['body'][0];be=float(np.max(np.abs(a-b)));require(be<2e-4,'FPS-dependent rigid tick trajectory')
  comparisons.append({'fps':value['fps'],'cctMaxDelta':error,'rigidRelativeMaxDelta':be})
 for value in results:del value['positions'];del value['body']
 out={'result':'N3_PACING_PRODUCT_OK','runtimeHash':record['runtimeHash'],'matrix':results,'comparisons':comparisons,'scope':'Actual DX12 Editor cadence/CCT fixed ticks/render matrix and GPU silhouettes; server/transport and other GPU devices separate'}
 (work/'result.json').write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8');print(json.dumps(out,indent=2));return out
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--work',type=pathlib.Path,required=True);a=p.parse_args()
 try:check(a.work)
 except (AssertionError,ValueError,KeyError,OSError) as e:print('N3_PACING_FAILED: '+str(e));raise SystemExit(1)
