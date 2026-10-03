import json, math, statistics
from pathlib import Path
root = Path(__file__).resolve().parents[2]
folder = root / 'Build/Obj/Phase19T2QueryBench/Release'
result = json.loads((folder / 'result.json').read_text(encoding='utf-8-sig'))
groups = {}
for row in result['runs']:
    key = (row['backend'], row['requests'], row['profile'], row['mode'])
    groups.setdefault(key, []).append(row)
summary = []
for key, rows in sorted(groups.items()):
    entry = dict(zip(('backend', 'requests', 'profile', 'mode'), key))
    for metric in ('meanUs', 'p99Us'):
        values = [row[metric] for row in rows]
        cv = statistics.stdev(values) / statistics.mean(values) * 100
        entry[metric] = {'median': statistics.median(values), 'cvPercent': cv, 'stable': cv <= 10, 'values': values}
    summary.append(entry)
comparisons = []
lookup = {(x['backend'], x['requests'], x['profile'], x['mode']): x for x in summary}
for backend in ('cpu', 'gpu'):
    for count in (1, 16, 64):
        entry = {'backend': backend, 'requests': count}
        for metric in ('meanUs', 'p99Us'):
            scalar = lookup[backend, count, False, 'scalar'][metric]
            batch = lookup[backend, count, False, 'batch'][metric]
            on = lookup[backend, count, True, 'batch'][metric]
            entry[metric] = {'batchVsScalarPercent': (batch['median']/scalar['median']-1)*100,
                             'profileOverheadPercent': (on['median']/batch['median']-1)*100,
                             'comparisonStable': scalar['stable'] and batch['stable'],
                             'overheadStable': on['stable'] and batch['stable']}
        comparisons.append(entry)
result['summary'] = summary
result['comparisons'] = comparisons
result['stabilityPolicy'] = 'CV > 10 percent is unaccepted per metric; retain all raw samples'
result['performanceAccepted'] = False
result['remaining'] = ['Managed query workload performance', 'Representative moving-scene workload', 'Unstable native metrics require remeasurement']
(folder / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(comparisons, indent=2))
print('stable metrics', sum(x[m]['stable'] for x in summary for m in ('meanUs','p99Us')), '/', len(summary)*2)
