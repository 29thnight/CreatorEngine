import json, statistics, math, sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=root/'Build/Obj/Phase19T2QueryBench'/(sys.argv[1] if len(sys.argv)>1 else 'ManagedProfileCpuAccounting')/'result.json'
r=json.loads(p.read_text(encoding='utf-8-sig'))
rows=[dict(row, profileState=receipt['profile']) for receipt in r['receipts'] for row in receipt['rows']]

def stats(values):
    mean=statistics.mean(values)
    return {'median':statistics.median(values),'cvPercent':statistics.stdev(values)/mean*100 if mean else None,'values':values}

def correlation(a,b):
    if not statistics.stdev(a) or not statistics.stdev(b): return None
    am,bm=statistics.mean(a),statistics.mean(b)
    return sum((x-am)*(y-bm) for x,y in zip(a,b))/math.sqrt(sum((x-am)**2 for x in a)*sum((y-bm)**2 for y in b))

summary=[]
for state in ('off','on'):
 for count in (16,64):
  for mode in ('scalar','batch'):
   group=[x for x in rows if (x['profileState'],x['requests'],x['mode'])==(state,count,mode)]
   wall=[x['blockWallUs']/600 for x in group]
   cycles=[x['threadCycles']/600 for x in group]
   cpu=[x['threadCpuUs']/600 for x in group]
   tails=[]
   for x in group:
    raw=x['rawUs'];median=statistics.median(raw)
    tails.append({'block':x['block'],'maxUs':max(raw),'medianUs':median,
                  'over3MedianIndices':[i for i,v in enumerate(raw) if v>median*3],
                  'firstQuartileMedianUs':statistics.median(raw[:150]),'lastQuartileMedianUs':statistics.median(raw[-150:]),
                  'cpuMinusWallUs':x['threadCpuUs']-x['blockWallUs']})
   summary.append({'profile':state,'requests':count,'mode':mode,'blockWallPerLoopUs':stats(wall),
                   'cpuPerLoopUs':stats(cpu),'cyclesPerLoop':stats(cycles),'wallCycleCorrelation':correlation(wall,cycles),'tails':tails})
r['cpuDiagnostic']=summary
r['cpuDiagnosticLimits']=['CPU block includes loop/timer and accounting-boundary overhead, not just query scopes',
                         'GetThreadTimes representation is 100ns; effective accounting resolution is not assumed',
                         'Cycle counts are not converted to elapsed time or GHz',
                         'CPU-minus-wall can be positive due to boundary/resolution differences; no exact waiting-time inference',
                         'Eight blocks per group; correlations are diagnostic and not causal proof']
p.write_text(json.dumps(r,indent=2),encoding='utf-8')
for x in summary:
 print(x['profile'],x['requests'],x['mode'],'wallCV',round(x['blockWallPerLoopUs']['cvPercent'],1),'cyclesCV',round(x['cyclesPerLoop']['cvPercent'],1),
       'cpuCV',None if x['cpuPerLoopUs']['cvPercent'] is None else round(x['cpuPerLoopUs']['cvPercent'],1),'r',x['wallCycleCorrelation'],
       'tailSamples',sum(len(t['over3MedianIndices']) for t in x['tails']))
