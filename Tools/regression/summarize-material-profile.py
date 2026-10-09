"""Summarize complete scopes from the engine's version 1/2 continuous .ceprof format.

RenderThreadFrame is a render cost; frameMs describes engine frames and must not
be reported as display FPS. Throughput is measured separately by the HTTP runner.
"""
import sys, struct, zlib, json, statistics, collections
from pathlib import Path
class Reader:
 def __init__(self,b):self.b=b;self.p=0
 def get(self,f):
  s=struct.calcsize('<'+f);v=struct.unpack_from('<'+f,self.b,self.p);self.p+=s;return v[0] if len(v)==1 else v
 def string(self):
  n=self.get('I');v=self.b[self.p:self.p+n].decode('utf-8');self.p+=n;return v
def analyze(path):
 b=Path(path).read_bytes();assert b[:8]==b'CEPROF\0\0';version,n=struct.unpack_from('<II',b,8);assert version in (1,2)
 chunks={};chunk_versions={};cpu_sessions=set();cpu_ticks=set();cpu_tasks=set()
 for i in range(n):
  t,v,o,s,c,r=struct.unpack_from('<IIQQII',b,16+32*i);data=b[o:o+s];assert zlib.crc32(data)==c;chunks[t]=Reader(data);chunk_versions[t]=v
 hz=chunks[1].get('Q');complete=chunks[1].get('B');unacked=chunks[1].get('I')
 m=chunks[2];markers=[]
 for i in range(m.get('I')):kind=m.get('B');line=m.get('I');name=m.string();file=m.string();markers.append((name,file,line))
 t=chunks[3];threads={}
 for i in range(t.get('I')):slot,osid,kind,order=t.get('IIBI');threads[slot]=t.string()
 f=chunks[4];frames=[];events=collections.defaultdict(list);framevals=collections.defaultdict(lambda:collections.defaultdict(float));drops=0;boundary=0
 for i in range(f.get('I')):
  fid,a,e,d,count=f.get('IQQQI');drops+=d;frames.append((e-a)*1000/hz)
  for j in range(count):
   a,e,mid,start,slot,dep,flags,q,sub,view,res=f.get('QQIIHHBBIHH');ms=(e-a)*1000/hz
   cpu=f.get('QQQ') if chunk_versions[4]>=2 else (0,0,0)
   if cpu[0]:
    cpu_sessions.add(cpu[0]);cpu_ticks.add(cpu[:2])
    if cpu[2]:cpu_tasks.add(cpu)
   if flags&8:continue
   if flags&3:
    boundary+=1
    continue
   name=markers[mid][0];key=(threads.get(slot,str(slot)),name, bool(flags&4),view)
   events[key].append(ms);framevals[key][fid]+=ms
 def stats(v):
  v=sorted(v);return {'mean':statistics.mean(v),'p50':statistics.median(v),'p95':v[min(len(v)-1,int(len(v)*.95))],'max':max(v)}
 rows=[]
 for key,v in events.items():
  rows.append({'thread':key[0],'name':key[1],'gpu':key[2],'view':key[3],'calls':len(v),**stats(v),'perEngineFrame':sum(v)/len(frames),'file': next((m[1] for m in markers if m[0]==key[1]),'')})
 rows.sort(key=lambda x:x['perEngineFrame'],reverse=True)
 counter_rows=[];counter_owners=set();dropped_counters=0
 if 5 in chunks:
  descriptors={}
  if 6 in chunks:
   cr=chunks[6]
   for _ in range(cr.get('I')):
    cid,category=cr.get('HI');descriptors[cid]=(cr.string(),cr.string(),category)
  cr=chunks[5];dropped_counters,counter_frames=cr.get('QI')
  values=collections.defaultdict(list)
  for _ in range(counter_frames):
   fid,count=cr.get('II')
   for _ in range(count):
    cid,value=cr.get('Hd');owner=cr.get('QQQ') if chunk_versions[5]>=2 else (0,0,0)
    if owner[0]:counter_owners.add(owner[:2])
    values[(cid,owner[0])].append((fid,owner[1],value))
  assert cr.p==len(cr.b)
  for (cid,session),v in sorted(values.items()):
   name,unit,category=descriptors.get(cid,(str(cid),'',0))
   counter_rows.append({'name':name,'unit':unit,'category':category,'session':session,'samples':len(v),'min':min(x[2] for x in v),'max':max(x[2] for x in v),'last':v[-1][2],'lastTick':v[-1][1],'lastFrame':v[-1][0]})
 out={'path':str(path),'frames':len(frames),'frameMs':stats(frames),'complete':complete,'unacked':unacked,'droppedEvents':drops,'excludedBoundaryEvents':boundary,'markers':rows,'droppedCounters':dropped_counters,'counterTicks':len(counter_owners),'counters':counter_rows,'cpuOwnership':{'sessions':len(cpu_sessions),'ticks':len(cpu_ticks),'tasks':len(cpu_tasks)}}
 # Legacy CEPROF v1/v2 does not carry render/display extents or temporal frame kind.
 # Keep engine diagnostics useful, but never promote this artifact to a TR0 gate.
 out['temporalPerformanceGateEligible']=False
 out['temporalProvenanceStatus']='unavailable-in-ceprof-v1-v2'
 out['frameMsAxis']='engine-frame-boundaries-not-real-render-or-presented-fps'
 Path(str(path)+'.summary.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
 print(json.dumps({**out,'markers':rows[:35]},indent=2))
for path in sys.argv[1:]:analyze(path)
