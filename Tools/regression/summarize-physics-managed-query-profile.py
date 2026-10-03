import json, statistics, sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=root/'Build/Obj/Phase19T2QueryBench'/ (sys.argv[1] if len(sys.argv)>1 else 'ManagedProfile') / 'result.json'
r=json.loads(p.read_text(encoding='utf-8-sig'))
rows=[dict(row, profileState=receipt['profile']) for receipt in r['receipts'] for row in receipt['rows']]
groups={}
for state in ('off','on'):
 for count in (16,64):
  for mode in ('scalar','batch'):
   entry={'profile':state,'requests':count,'mode':mode}
   for metric in ('meanUs','p99Us'):
    values=[x[metric] for x in rows if (x['profileState'],x['requests'],x['mode'])==(state,count,mode)]
    cv=statistics.stdev(values)/statistics.mean(values)*100
    entry[metric]={'median':statistics.median(values),'cvPercent':cv,'stable':cv<=10,'values':values}
   groups[state,count,mode]=entry
comparisons=[]
for count in (16,64):
 entry={'requests':count}
 for metric in ('meanUs','p99Us'):
  off=groups['off',count,'batch'][metric];on=groups['on',count,'batch'][metric];scalar=groups['off',count,'scalar'][metric]
  entry[metric]={'batchVsScalarPercent':(off['median']/scalar['median']-1)*100,'profileOverheadPercent':(on['median']/off['median']-1)*100,
                 'comparisonStable':off['stable'] and scalar['stable'],'overheadStable':off['stable'] and on['stable']}
 comparisons.append(entry)
r['summary']=list(groups.values());r['comparisons']=comparisons
r['stabilityPolicy']='CV <= 10 percent per metric; unstable observations retained without acceptance'
r['remaining']=['Unstable mean/p99 remeasurement where applicable','Representative workload and M3 product performance acceptance']
p.write_text(json.dumps(r,indent=2),encoding='utf-8')
print(json.dumps(comparisons,indent=2))
