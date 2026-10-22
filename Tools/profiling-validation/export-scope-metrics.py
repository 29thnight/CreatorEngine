"""Small report summaries, independent of the full raw ETW JSON archive."""
import argparse
import bisect
import json
import statistics
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--root',required=True,type=Path)
parser.add_argument('variants',nargs='+')
args=parser.parse_args()
out=args.root/'report-scopes'
out.mkdir(exist_ok=True)
for variant in args.variants:
    data=json.loads((args.root/'timeline'/f'{variant}-etw.json').read_text('utf-8-sig'))
    window=next(w for w in data['windows'] if w['name']=='on')
    scopes={}
    for lane in data['lanes'].values():
        states=lane['states']
        starts=[s[0] for s in states]
        for event in lane['scopes']:
            a,dt,marker=event[:3]
            b=a+dt
            if event[4]&3 or a<window['begin'] or b>window['end']:
                continue
            record=scopes.setdefault(data['markers'][marker],dict(wall=[],Running=0.,Ready=0.,Waiting=0.,Unknown=0.))
            record['wall'].append(dt)
            for state in states[max(0,bisect.bisect_right(starts,a)-1):bisect.bisect_left(starts,b)]:
                record[{1:'Running',2:'Ready',3:'Waiting'}.get(state[2],'Unknown')]+=max(0,min(b,state[0]+state[1])-max(a,state[0]))
    for record in scopes.values():
        values=sorted(record['wall']);count=len(values)
        record['wall']=dict(count=count,mean=statistics.mean(values),p50=statistics.median(values),p95=values[min(count-1,int(count*.95))],p99=values[min(count-1,int(count*.99))],max=max(values))
        record['meanStateMs']={k:record.pop(k)/count for k in ['Running','Ready','Waiting','Unknown']}
    (out/f'{variant}.json').write_text(json.dumps(scopes,separators=(',',':')),'utf-8')
    print(variant,len(scopes),flush=True)
