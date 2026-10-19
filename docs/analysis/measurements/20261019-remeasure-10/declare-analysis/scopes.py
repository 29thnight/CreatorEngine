import json,bisect,collections,statistics
from pathlib import Path
r=Path('Artifacts/remeasure-10-20261019');out={}
def stats(xs):
 xs=sorted(xs);return dict(count=len(xs),mean=statistics.mean(xs),p50=statistics.median(xs),p95=xs[min(len(xs)-1,int(len(xs)*.95))])
def union(xs):
 z=[]
 for a,b in sorted(xs):
  if z and a<=z[-1][1]:z[-1][1]=max(b,z[-1][1])
  else:z.append([a,b])
 return z
def diff(a,b,children):
 z=[];cursor=a
 for x,y in union(children):
  x=max(a,x);y=min(b,y)
  if x>cursor:z.append([cursor,x])
  cursor=max(cursor,y)
 if cursor<b:z.append([cursor,b])
 return z
for mode in ['etw','baseline']:
 d=json.loads((r/f'timeline/after-{mode}.json').read_text());window=next(w for w in d['windows'] if w['name'] in ['on','native recording ON']);names=d['markers'];lane=next(x for x in d['lanes'].values() if any('RenderThread' in n for n in x['names']));es=[e for e in lane['scopes'] if not e[4]&3];starts=[e[0] for e in es];frames=sorted(x for x in d['measurements'] if x[3]==2 and window['begin']<=x[0] and sum(x[:2])<=window['end']);fs=[x[0] for x in frames]
 if mode=='baseline':
  fg=json.loads((r/'after/baseline-foreground-history.json').read_text('utf-8-sig'));anchor=d['alignment']['qpcAnchor'];window=dict(begin=(fg['beginQpc']-anchor)*1000/d['hz'],end=(fg['endQpc']-anchor)*1000/d['hz']);frames=sorted(x for x in d['measurements'] if x[3]==2 and window['begin']<=x[0] and sum(x[:2])<=window['end']);fs=[x[0] for x in frames]
 roots=[]
 for e in es:
  if names[e[2]]!='RenderGraphDeclare':continue
  i=bisect.bisect_right(fs,e[0]+.0002)-1
  if i>=0 and sum(e[:2])<=sum(frames[i][:2])+.0002:roots.append(e)
 stateStarts=[x[0] for x in lane['states']];dpcStarts={k:[q[0] for q in xs] for k,xs in d['dpc'].items()}
 def residency(intervals):
  z=collections.Counter()
  for a,b in intervals:
   if mode!='etw':continue
   for s in lane['states'][max(0,bisect.bisect_right(stateStarts,a)-1):bisect.bisect_left(stateStarts,b)]:
    dt=max(0,min(b,sum(s[:2]))-max(a,s[0]));z[{0:'Unknown',1:'Running',2:'Ready',3:'Waiting',4:'Terminated'}.get(s[2],'Unknown')]+=dt
    if s[2]==1:
     ds=d['dpc'].get(str(s[3]),[]);dst=dpcStarts.get(str(s[3]),[]);lo=max(a,s[0]);hi=min(b,sum(s[:2]))
     for q in ds[max(0,bisect.bisect_right(dst,lo)-1):bisect.bisect_left(dst,hi)]:
      if q[0]>b:break
      z['DpcIsr']+=max(0,min(b,sum(s[:2]),sum(q[:2]))-max(a,s[0],q[0]))
  z['RunningNet']=z['Running']-z['DpcIsr'];return z
 acc=collections.defaultdict(collections.Counter);nodes=collections.defaultdict(collections.Counter);nodeTimes=collections.defaultdict(list);examples=[]
 for root in roots:
  a,b=root[0],sum(root[:2]);events=es[bisect.bisect_left(starts,a-.0001):bisect.bisect_right(starts,b)];events=[e for e in events if sum(e[:2])<=b+.0002 and e[3]>=root[3]];events.sort(key=lambda e:(e[0],e[3],-e[1]));stack=[];entries=[]
  for e in events:
   while stack and (e[3]<=entries[stack[-1]]['e'][3] or e[0]>=sum(entries[stack[-1]]['e'][:2])-.00001):stack.pop()
   parent=stack[-1] if stack else None;idx=len(entries);entries.append(dict(e=e,parent=parent,children=[]))
   if parent is not None:entries[parent]['children'].append([e[0],sum(e[:2])])
   stack.append(idx)
  nodeRows=[]
  for x in entries:
   e=x['e'];name=names[e[2]];exclusive=diff(e[0],sum(e[:2]),x['children']);z=acc[name];z['calls']+=1;z['inclusiveMs']+=e[1];z['selfMs']+=sum(y-x for x,y in exclusive);z.update({'self'+k:v for k,v in residency(exclusive).items()});z.update({'inclusive'+k:v for k,v in residency([[e[0],sum(e[:2])]]).items()})
   if name.startswith('RenderDeclare.'):
    nodes[name]['calls']+=1;nodes[name]['wallMs']+=e[1];nodes[name].update(residency([[e[0],sum(e[:2])]]));nodeTimes[name].append(e[1]);nodeRows.append(dict(name=name,begin=e[0],ms=e[1]))
  examples.append(dict(begin=a,ms=root[1],nodes=nodeRows))
 n=len(roots);out[mode]=dict(roots=n,root=stats([x[1] for x in roots]),scopes={k:{a:v/n for a,v in z.items()} for k,z in acc.items()},nodes={k:dict(perFrame={a:v/n for a,v in z.items()},perCall=stats(nodeTimes[k])) for k,z in nodes.items()},example=min(examples,key=lambda x:abs(x['ms']-statistics.median(y['ms'] for y in examples))))
 print(mode,n,sorted(((k,z['wallMs']/n) for k,z in nodes.items()),key=lambda x:-x[1])[:12],flush=True)
(r/'declare-analysis/scopes.json').write_text(json.dumps(out,indent=2),'utf-8')
