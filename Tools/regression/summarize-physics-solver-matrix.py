import argparse
import json
import statistics
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('receipt', type=Path)
a = p.parse_args()
r = json.loads(a.receipt.read_text(encoding='utf-8-sig'))
if r['status'] != 'measurements_complete' or len(r['runs']) != 36:
    raise SystemExit('Complete 36-run solver matrix required')
summary = []
for active in (16, 256, 1024):
    for writes in (0, active // 2, active):
        rows = [x['data'] for x in r['runs'] if x['data']['active'] == active and x['data']['writesPerTick'] == writes]
        if [x['profile'] for x in rows] != [False, True, True, False]:
            raise SystemExit('ABBA order mismatch')
        case = {'active': active, 'total': 1024, 'writes': writes, 'activeRatio': active/1024, 'changedRatio': writes/1024}
        for metric in ('meanUs', 'p99Us'):
            result = {}
            for mode, profile in (('off', False), ('on', True)):
                values = [x[metric] for x in rows if x['profile'] == profile]
                cv = statistics.stdev(values) / statistics.mean(values) * 100
                result[mode] = {'median': statistics.median(values), 'cvPercent': cv, 'stable': cv <= 10, 'values': values}
            result['comparisonStable'] = all(result[m]['stable'] for m in ('off', 'on'))
            result['onOverheadPercent'] = (result['on']['median']/result['off']['median']-1)*100
            case[metric] = result
        summary.append(case)
r['summary'] = summary
r['stableMeanComparisons'] = sum(x['meanUs']['comparisonStable'] for x in summary)
r['stableP99Comparisons'] = sum(x['p99Us']['comparisonStable'] for x in summary)
r['stabilityPolicy'] = 'CV <= 10 percent for two independent process values per side; preliminary stability only, not a statistical guarantee'
r['performanceAccepted'] = False
r['sampleCount'] = 36*240
r['scopeLimitation'] = 'Free-motion solver/native session; excludes actual Player scheduling, contacts, CCT, queries, GPU rendering and end-to-end frame time'
a.receipt.write_text(json.dumps(r, indent=2), encoding='utf-8')
print(json.dumps({'backend':r['backend'],'stableMean':r['stableMeanComparisons'],'stableP99':r['stableP99Comparisons'],'summary':summary}, indent=2))