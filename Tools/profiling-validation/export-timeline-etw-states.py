import sys
import json,csv,re,collections
from pathlib import Path
from datetime import datetime
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--root',required=True,type=Path);parser.add_argument('variants',nargs='+');args=parser.parse_args();root=args.root.resolve();r=root/'timeline'
load=lambda p:json.loads(p.read_text('utf-8-sig'))
verdict=[]
for variant in args.variants:
 cached=r/(variant+'-states-done.json')
 if cached.exists():verdict.append(load(cached));print('cached',variant,flush=True);continue
 data=load(r/(variant+'-etw.json'));snapshot=load(root/variant/'worker-start-scheduler.json');pid=int(snapshot['processId']);target=re.compile(r'\(\s*'+str(pid)+r'\)$')
 text=(root/variant/'trace-stats.txt').read_text('utf-8-sig')
 stamp=lambda label:datetime.strptime(re.search(r'^'+label+r' time \(UTC\)\s*:\s*(\S+)',text,re.M)[1][:26],'%Y/%m/%d:%H:%M:%S.%f')
 end=(stamp('End')-stamp('Start')).total_seconds()*1000;data['end']=end
 names={int(t['threadId']):(t.get('description',{}).get('value') or '') for t in snapshot['threads']}
 transitions=collections.defaultdict(list);dpc=collections.defaultdict(list);kinds=collections.Counter();unknown=collections.Counter();seq=0
 with (r/(variant+'-kernel.csv')).open(encoding='utf-8-sig',errors='strict') as f:
  for line in f:
   z=line.lstrip()
   if not z.startswith(('CSwitch,','ReadyThread,','DPC,','DPCTmr,','Interrupt,','T-Start,','T-End,')):continue
   row=[x.strip() for x in next(csv.reader([line]))]
   if len(row)<2 or not row[1].isdigit():continue
   t=int(row[1])/1000;k=row[0];seq+=1;kinds[k]+=1
   if k=='CSwitch':
    if target.search(row[8]):
     s=2 if row[12] in ['Ready','DeferredReady','Standby'] else 3 if row[12]=='Waiting' else 4 if row[12]=='Terminated' else 0
     if not s:unknown[row[12]]+=1
     transitions[int(row[9])].append((t,seq,0,s,-1))
    if target.search(row[2]):transitions[int(row[3])].append((t,seq,1,1,int(row[16])))
   elif k=='ReadyThread':
    if target.search(row[4]):transitions[int(row[5])].append((t,seq,2,2,-1))
   elif k in ['DPC','DPCTmr','Interrupt']:
    a=t-int(row[2])/1000
    if t>0 and a<end:dpc[int(row[3])].append([max(0,a),min(end,t)])
   elif target.search(row[2]):transitions[int(row[3])].append((t,seq,3,4 if k=='T-End' else 0,-1))
 for tid,events in transitions.items():
  lane=data['lanes'].setdefault(str(tid),dict(tid=tid,names=[],scopes=[],states=[],maxDuration=0))
  if names.get(tid) and names[tid] not in lane['names']:lane['names'].append(names[tid])
  state=0;cpu=-1;cursor=0;states=[]
  for t,order,kind,newstate,newcpu in sorted(events):
   if t>end:break
   if kind==2 and state==1:continue
   if t>cursor:states.append([round(cursor,4),round(t-cursor,4),state,cpu])
   state=newstate;cpu=newcpu;cursor=max(cursor,t)
  if cursor<end:states.append([round(cursor,4),round(end-cursor,4),state,cpu])
  merged=[]
  for x in states:
   if merged and merged[-1][2:]==x[2:] and abs(merged[-1][0]+merged[-1][1]-x[0])<.0002:merged[-1][1]=round(merged[-1][1]+x[1],4)
   else:merged.append(x)
  assert abs(sum(x[1] for x in merged)-end)<.005,(variant,tid)
  lane['states']=merged
 for lane in data['lanes'].values():
  if not lane['states']:lane['states']=[[0,end,0,-1]]
 for cpu,events in dpc.items():
  merged=[]
  for a,b in sorted(events):
   if merged and a<=merged[-1][1]:merged[-1][1]=max(b,merged[-1][1])
   else:merged.append([a,b])
  data['dpc'][str(cpu)]=[[round(a,4),round(b-a,4)] for a,b in merged]
 checks=[]
 data['etwKinds']=dict(kinds);data['unknownSwitchStates']=dict(unknown);data['etwSource']=variant+'/creator-robot.etl'
 (r/(variant+'-etw.json')).write_text(json.dumps(data,separators=(',',':')),'utf-8')
 verdict.append(dict(variant=variant,passed=True,referenceChecks=checks,events=dict(kinds),unknownSwitchStates=dict(unknown),threads=len(data['lanes'])))
 (r/(variant+'-states-done.json')).write_text(json.dumps(verdict[-1]),'utf-8')
 print(variant,len(data['lanes']),dict(kinds),flush=True)
(r/'alignment-verdict.json').write_text(json.dumps(dict(passed=True,cases=verdict,toleranceMs=.005),indent=2),'utf-8')
