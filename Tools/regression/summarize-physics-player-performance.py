"""Summarize immutable ABBA Player receipts without accepting noisy results."""
import argparse
import json
import statistics
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('receipt', type=Path)
args = parser.parse_args()
receipt = json.loads(args.receipt.read_text(encoding='utf-8-sig'))
if receipt['status'] != 'measurements_complete' or [r['profile'] for r in receipt['runs']] != ['off', 'on', 'on', 'off']:
    raise SystemExit('Completed ABBA measurements required')

summary = []
for count in (16, 64):
    for mode in ('scalar', 'batch'):
        item = {'requests': count, 'mode': mode}
        for metric in ('meanUs', 'p99Us'):
            sides = {}
            for profile in ('off', 'on'):
                values = [row[metric] for run in receipt['runs'] if run['profile'] == profile
                          for row in run['rows'] if row['requests'] == count and row['mode'] == mode]
                if len(values) != 8 or any(value <= 0 for value in values):
                    raise SystemExit('Missing or invalid block metrics')
                cv = statistics.stdev(values) / statistics.mean(values) * 100
                sides[profile] = {'median': statistics.median(values), 'cvPercent': cv,
                                  'stable': cv <= 10, 'values': values}
            sides['comparisonStable'] = all(sides[p]['stable'] for p in ('off', 'on'))
            sides['profilingOverheadPercent'] = (sides['on']['median'] / sides['off']['median'] - 1) * 100
            item[metric] = sides
        summary.append(item)

receipt['summary'] = summary
receipt['stabilityPolicy'] = 'CV <= 10 percent across eight block means/p99 per side; retain all raw samples; unstable comparisons not accepted'
receipt['stableComparisons'] = sum(item[metric]['comparisonStable'] for item in summary for metric in ('meanUs', 'p99Us'))
receipt['totalComparisons'] = 8
receipt['performanceAccepted'] = False
receipt['scopeLimitation'] = 'Query wall cost only; does not replace fixed-step solver total, active/change ratios, Debug regression, or GPU timing distribution acceptance'
args.receipt.write_text(json.dumps(receipt, indent=2), encoding='utf-8')
print(json.dumps({'summary': summary, 'stableComparisons': receipt['stableComparisons'], 'totalComparisons': 8}, indent=2))