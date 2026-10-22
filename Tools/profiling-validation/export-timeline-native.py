import sys
import importlib.util,json,gzip,hashlib,re
from pathlib import Path
from datetime import datetime
import argparse
args=argparse.ArgumentParser();args.add_argument('--root',required=True,type=Path);args.add_argument('--manifest',default='manifest.json');args.add_argument('variants',nargs='+');options=args.parse_args();root=options.root.resolve();r=root/'timeline';r.mkdir(parents=True,exist_ok=True);repo=Path(__file__).resolve().parents[2]
if len(options.variants)>1:
 import concurrent.futures,subprocess
 def export_variant(variant):
  name=variant+'-native-manifest.json'
  subprocess.run([sys.executable,str(Path(__file__).resolve()),'--root',str(root),'--manifest',name,variant],check=True)
  return json.loads((r/name).read_text('utf-8-sig'))
 with concurrent.futures.ThreadPoolExecutor(max_workers=min(4,len(options.variants))) as pool:
  parts=list(pool.map(export_variant,options.variants))
 (r/options.manifest).write_text(json.dumps([item for part in parts for item in part],indent=2),'utf-8')
 raise SystemExit(0)
spec=importlib.util.spec_from_file_location('ceprof_timeline_reader',repo/'Tools/regression/summarize-material-profile.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
variants=options.variants;manifest=[]
load=lambda x:json.loads(x.read_text('utf-8-sig'))
for variant in variants:
 for mode in ['baseline','etw']:
  if mode=='baseline' and variant in ['reference-common','current-detail']:continue
  path=root/variant/(mode+'.ceprof')
  if not path.exists():continue
  cached=r/(variant+'-'+mode+'.json')
  if cached.exists():
   old=json.loads(cached.read_text('utf-8-sig'));manifest.append(dict(id=old['id'],variant=variant,mode=mode,begin=old['begin'],end=old['end'],events=old['nativeEvents'],measurements=len(old['measurements'])));print('cached',old['id'],flush=True);continue
  c=p.read_capture(path);assert c['complete'] and not c['unacked'];events=[e for f in c['frames'] for e in f['events'] if not e['flags']&4]
  if mode=='etw':
   text=(root/variant/'trace-stats.txt').read_text('utf-8-sig');trace=datetime.strptime(re.search(r'^Start time \(UTC\)\s*:\s*(\S+)',text,re.M)[1][:26],'%Y/%m/%d:%H:%M:%S.%f');a=load(root/variant/'started.json');origin=a['qpc'];offset=(datetime.fromisoformat(a['utc'].replace('Z','+00:00')).replace(tzinfo=None)-trace).total_seconds()*1000
   ranges=load(root/variant/'ranges.json');windows=[dict(name=k,begin=v['beginUs']/1000,end=v['endUs']/1000) for k,v in ranges.items()];end=max(x['end'] for x in windows)+100;begin=0
  else:
   origin=min(f['begin'] for f in c['frames']);offset=0;windows=[];begin=0;end=(max(f['end'] for f in c['frames'])-origin)*1000/c['hz']
  def stamp(t):return round((t-origin)*1000/c['hz']+offset,4)
  names=[m['name'] for m in c['markers']];lanes={};seen=set();duplicate=0
  for e in events:
   key=(e['slot'],e['marker'],e['begin'],e['end'],e['depth'],e['cpu'])
   if key in seen:duplicate+=1;continue
   seen.add(key);tid=c['threadIds'][e['slot']];lid=str(tid) if tid else 'slot'+str(e['slot'])
   lane=lanes.setdefault(lid,dict(tid=tid,names=[],scopes=[],states=[]))
   if c['threads'][e['slot']] not in lane['names']:lane['names'].append(c['threads'][e['slot']])
   lane['scopes'].append([stamp(e['begin']),round((e['end']-e['begin'])*1000/c['hz'],4),e['marker'],e['depth'],e['flags'],e['cpu'][1],e['cpu'][2]])
  for lane in lanes.values():lane['scopes'].sort();lane['maxDuration']=max((x[1] for x in lane['scopes']),default=0)
  measurements=sorted({(stamp(m['begin']),round((m['end']-m['begin'])*1000/c['hz'],4),m['realFrameId'],m['viewId']) for f in c['frames'] for m in f['measurements'] if m['axis']==1})
  if not windows:windows=[dict(name='native recording ON',begin=min(x[0] for x in measurements),end=max(x[0]+x[1] for x in measurements))]
  data=dict(id=variant+'-'+mode,variant=variant,mode=mode,begin=begin,end=end,windows=windows,markers=names,lanes=lanes,measurements=measurements,dpc={},nativeSha256=c['sha256'],source=str(path.relative_to(root)),hz=c['hz'],duplicateScopesRemoved=duplicate,etwAvailable=mode=='etw',nativeEvents=len(seen),alignment=dict(qpcAnchor=origin,utcOffsetMs=offset),partialScopeCount=sum(bool(e['flags']&3) for e in events),losses=c['losses'])
  if mode=='baseline':
   fg=load(root/variant/'baseline-foreground-history.json');data['windows']=[dict(name='native recording ON',begin=stamp(fg['beginQpc']),end=stamp(fg['endQpc']))]
  else:data['traceUtc']=re.search(r'^Start time \(UTC\)\s*:\s*(\S+)',text,re.M)[1]+' UTC'
  identities=[]
  for frame in c['frames']:
   for m in frame['measurements']:
    if m['axis']==4:
     identities.append([stamp(m['begin']),stamp(m['end']),m['marker'],c['threadIds'][m['slot']],str(m['recordingId']),str(m['frameId']),str(m['viewId']),str(m['deviceIncarnation']),m['backendGeneration'],str(m['queueId']),m['queueKind'],m['targetIndex'],str(m['sourceEngineFrame']) if m['sourceEngineFrameAvailable'] else None])
  data['rhiIdentities']=sorted(identities)
  gpuSpec=importlib.util.spec_from_file_location('gpu_extract',repo/'Tools/profiling-validation/attach-gpu-timeline.py');gpuModule=importlib.util.module_from_spec(gpuSpec);gpuSpec.loader.exec_module(gpuModule)
  gpu,summary=gpuModule.extract(p,path,data,capture=c)
  (r/(data['id']+'-gpu.json')).write_text(json.dumps(gpu,separators=(',',':')),'utf-8')
  (r/(data['id']+'-gpu-metrics.json')).write_text(json.dumps(summary,indent=2),'utf-8')
  rawDir=root/'json'/variant;rawDir.mkdir(parents=True,exist_ok=True)
  rawFiles=[]
  def saveRaw(name,value):
   target=rawDir/name;target.parent.mkdir(parents=True,exist_ok=True);content=json.dumps(value,ensure_ascii=True,separators=(',',':'),allow_nan=False).encode();target.write_bytes(content)
   rawFiles.append(dict(path=name,bytes=len(content),sha256=hashlib.sha256(content).hexdigest()))
   return target
  saveRaw(mode+'/metadata.json',{key:value for key,value in c.items() if key!='frames'})
  counts=dict(frames=len(c['frames']),events=0,gpuEvents=0,counters=0,measurements=0)
  for start in range(0,len(c['frames']),8):
   group=c['frames'][start:start+8];part=saveRaw(f'{mode}/frames-{start:06}.json',group)
   assert load(part)==json.loads(json.dumps(group)),str(part)
   for frame in group:
    counts['events']+=len(frame['events']);counts['gpuEvents']+=sum(bool(e['flags']&4) for e in frame['events']);counts['counters']+=len(frame['counters']);counts['measurements']+=len(frame['measurements'])
  (rawDir/(mode+'-manifest.json')).write_text(json.dumps(dict(capture=dict(id=mode,counts=counts,roundTripEqual=True,sha256=c['sha256']),files=rawFiles),indent=2),'utf-8')
  (r/(data['id']+'.json')).write_text(json.dumps(data,separators=(',',':')),'utf-8');manifest.append(dict(id=data['id'],variant=variant,mode=mode,begin=begin,end=end,events=len(seen),measurements=len(measurements)))
  (r/(data['id']+'-markers.json')).write_text(json.dumps(dict(submitCandidates=[x for x in names if any(y in x.lower() for y in ['submit','present','endframe'])],threads={str(k):dict(name=v,tid=c['threadIds'][k]) for k,v in c['threads'].items()}),indent=2),'utf-8')
  print(data['id'],len(seen),len(measurements),flush=True)
(r/options.manifest).write_text(json.dumps(manifest,indent=2),'utf-8')
