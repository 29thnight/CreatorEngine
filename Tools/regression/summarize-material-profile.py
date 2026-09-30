"""Summarize complete scopes from the engine's version 1 continuous .ceprof format.

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
 b=Path(path).read_bytes();assert b[:8]==b'CEPROF\0\0';version,n=struct.unpack_from('<II',b,8);assert version==1
 chunks={}
 for i in range(n):
  t,v,o,s,c,r=struct.unpack_from('<IIQQII',b,16+32*i);data=b[o:o+s];assert zlib.crc32(data)==c;chunks[t]=Reader(data)
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
 out={'path':str(path),'frames':len(frames),'frameMs':stats(frames),'complete':complete,'unacked':unacked,'droppedEvents':drops,'excludedBoundaryEvents':boundary,'markers':rows}
 Path(str(path)+'.summary.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
 print(json.dumps({**out,'markers':rows[:35]},indent=2))
for path in sys.argv[1:]:analyze(path)
