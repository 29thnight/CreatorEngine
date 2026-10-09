import bisect
import json
import statistics
import sys
import re
from pathlib import Path

root = Path(sys.argv[1])
events = [json.loads(s) for s in (root / 'physics-tasks.jsonl').read_text(encoding='utf-8-sig').splitlines()]
tids = {e['osThreadId'] for e in events}
current = {}
intervals = {tid: [] for tid in tids}

def transition(tid, time, state):
    if tid not in tids:
        return
    previous = current.get(tid)
    if previous and time >= previous[0]:
        intervals[tid].append((previous[0], time, previous[1]))
    current[tid] = (time, state)

with (root / 'switches.csv').open(encoding='utf-8-sig') as source:
    for line in source:
        kind, time, incoming, outgoing, state, reason = line.strip().split(',')
        time, incoming, outgoing = float(time), int(incoming), int(outgoing)
        if kind == 'switch':
            transition(outgoing, time, 'blocked:'+reason if int(state) == 5 else 'ready' if int(state) == 1 else 'other')
            transition(incoming, time, 'scheduled')
        elif incoming in current and current[incoming][1] != 'scheduled':
            transition(incoming, time, 'ready')

starts = {tid: [r[0] for r in rows] for tid, rows in intervals.items()}
aggregate_checks = []
for line in (root / 'thread-cswitch.txt').read_text(encoding='utf-8-sig').splitlines():
    match = re.match(r'\s*(\d+),\s*Player\.exe\s*\(\s*\d+\),\s*(\d+)', line)
    if not match or int(match[2]) not in tids:
        continue
    tid = int(match[2])
    scheduled = sum(right-left for left, right, state in intervals[tid] if state == 'scheduled')
    aggregate_checks.append(dict(tid=tid, xperfScheduledUs=int(match[1]), rawScheduledUs=scheduled,
        deltaUs=scheduled-int(match[1])))
if len(aggregate_checks) != len(tids) or any(abs(r['deltaUs']) > 2 for r in aggregate_checks):
    raise RuntimeError(f'Raw switch totals differ from independent xperf totals: {aggregate_checks}')
rows = []
markers = ('Physics.PhysXTask', 'Physics.FetchResults', 'Physics.PhysXTaskRun', 'Physics.PhysXTaskRelease')
for event in events:
    if event['name'] not in markers or event['durationUs'] <= 0:
        continue
    tid, begin, end = event['osThreadId'], event['beginUs'], event['endUs']
    totals = dict(scheduled=0.0, ready=0.0, blocked=0.0, other=0.0)
    reasons = {}
    index = max(0, bisect.bisect_right(starts[tid], begin) - 1)
    for position in range(index, len(intervals[tid])):
        left, right, state = intervals[tid][position]
        if left >= end:
            break
        overlap = max(0.0, min(end, right) - max(begin, left))
        totals[state.split(':')[0]] += overlap
        if state.startswith('blocked:') and overlap:
            reason = state.split(':')[1]
            reasons[reason] = reasons.get(reason, 0.0) + overlap
    rows.append(dict(event, **{k+'Us': v for k, v in totals.items()}, waitReasonUs=reasons,
        uncoveredUs=max(0.0, end-begin-sum(totals.values()))))

groups = {}
for name in markers:
    selected = sorted((r for r in rows if r['name'] == name), key=lambda r: r['durationUs'])
    if not selected and name in ('Physics.PhysXTaskRun', 'Physics.PhysXTaskRelease'):
        continue  # Historical captures predate the split; do not invent these measurements.
    slow = selected[-max(1, len(selected)//100):]
    reason_totals = {}
    for row in selected:
        for reason, duration in row['waitReasonUs'].items():
            reason_totals[reason] = reason_totals.get(reason, 0.0) + duration
    groups[name] = dict(count=len(selected), waitReasonSumAcrossSpansUs=reason_totals,
        slowestOnePercentMedians={key: statistics.median(r[key] for r in slow) for key in ('durationUs', 'scheduledUs', 'readyUs', 'blockedUs', 'uncoveredUs')}, slowest=selected[-10:])
ticks = {}
task_rows = {(r['session'], r['tick'], r['task']): r for r in rows if r['name'] == 'Physics.PhysXTask'}
for event in events:
    ticks.setdefault((event['session'], event['tick']), []).append(event)

tick_rows = []
for fetch in (r for r in rows if r['name'] == 'Physics.FetchResults'):
    tick_events = ticks[(fetch['session'], fetch['tick'])]
    tasks = [e for e in tick_events if e['name'] == 'Physics.PhysXTask']
    submits = {e['task']: e for e in tick_events if e['name'] == 'Physics.TaskSubmit'}
    completes = {e['task']: e for e in tick_events if e['name'] == 'Physics.TaskComplete'}
    matched = [e for e in tasks if e['task'] in submits and e['task'] in completes]
    if len(matched) != len(tasks):
        continue  # Capture-boundary task endpoints are not causal evidence.
    if not matched:
        continue
    last = max(matched, key=lambda e: completes[e['task']]['beginUs'])
    last_complete = completes[last['task']]['beginUs']
    longest = max(matched, key=lambda e: e['durationUs'])
    tick_rows.append(dict(session=fetch['session'], tick=fetch['tick'], fetchUs=fetch['durationUs'],
        ownerBlockedUs=fetch['blockedUs'], ownerReadyUs=fetch['readyUs'], ownerScheduledUs=fetch['scheduledUs'],
        taskCount=len(tasks), latestTaskId=last['task'], latestTaskThread=last['osThreadId'],
        latestTaskWallUs=last['durationUs'],
        longestTask=task_rows[(longest['session'], longest['tick'], longest['task'])],
        maxSubmitToStartUs=max(e['beginUs']-submits[e['task']]['beginUs'] for e in matched),
        fetchEndMinusLastCompleteUs=fetch['endUs']-last_complete))

tick_rows.sort(key=lambda r: r['fetchUs'])
groups['tickCorrelation'] = dict(count=len(tick_rows), slowest=tick_rows[-20:])
result = dict(clock='Raw ETW QPC converted by ETL PerfFreq; engine QPC microseconds', scope='Scheduled intervals include DPC/ISR interference; not instruction-exclusive CPU time', groups=groups, maximumUncoveredUs=max(r['uncoveredUs'] for r in rows), performanceAccepted=False)
result['xperfAggregateChecks'] = aggregate_checks
child_rows = {(r['session'], r['tick'], r['task'], r['name']): r for r in rows if r['name'] in markers[2:]}
child_events = {(e['session'], e['tick'], e['task'], e['name']): e for e in events if e['name'] in markers[2:]}
split_rows = []
for parent in (r for r in rows if r['name'] == markers[0]):
    identity = (parent['session'], parent['tick'], parent['task'])
    keys = [(*identity, name) for name in markers[2:]]
    if not all(key in child_events for key in keys):
        continue
    children = [child_rows.get(key, {}) for key in keys]
    split_rows.append(dict(session=identity[0], tick=identity[1], task=identity[2],
        parentWallUs=parent['durationUs'], run=children[0], release=children[1],
        outsideChildWallUs=parent['durationUs']-sum(child_events[key]['durationUs'] for key in keys),
        outsideChildBlockedUs=parent['blockedUs']-sum(child.get('blockedUs', 0.0) for child in children)))
if split_rows:
    result['splitAttribution'] = dict(count=len(split_rows),
        outsideChildBlockedSumAcrossTasksUs=sum(row['outsideChildBlockedUs'] for row in split_rows),
        longestParents=sorted(split_rows, key=lambda row: row['parentWallUs'])[-10:])
if result['maximumUncoveredUs'] > 0.01 or any(not group['count'] for group in groups.values()):
    raise RuntimeError('Incomplete ETW coverage; CPU interval analysis is not valid')
(root / 'task-cpu-analysis.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({k: {'count': v['count'], 'slowestOnePercentMedians': v.get('slowestOnePercentMedians')} for k, v in groups.items()}, indent=2))
print('maximumUncoveredUs=', result['maximumUncoveredUs'])
