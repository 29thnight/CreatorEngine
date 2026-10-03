import json, statistics
from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=root/'Build/Obj/Phase19T2QueryBench/Managed/result.json'
r=json.loads(p.read_text(encoding='utf-8-sig'))
rows=[row for receipt in r['receipts'] for row in receipt['rows']]
summary=[]
for count in (16,64):
    entry={'requests':count}
    for metric in ('meanUs','p99Us'):
        sides={}
        for mode in ('scalar','batch'):
            values=[x[metric] for x in rows if x['requests']==count and x['mode']==mode]
            cv=statistics.stdev(values)/statistics.mean(values)*100
            sides[mode]={'median':statistics.median(values),'cvPercent':cv,'stable':cv<=10,'values':values}
        sides['batchVsScalarPercent']=(sides['batch']['median']/sides['scalar']['median']-1)*100
        sides['comparisonStable']=all(sides[x]['stable'] for x in ('scalar','batch'))
        entry[metric]=sides
    summary.append(entry)
r['summary']=summary
r['stabilityPolicy']='CV <= 10 percent per metric, retain unstable raw values without accepting them'
r['remaining']=['Managed profiler on/off and hierarchy capture', 'Unstable p99 remeasurement', 'Representative workload and product M3 performance acceptance']
p.write_text(json.dumps(r,indent=2),encoding='utf-8')
print(json.dumps(summary,indent=2))
