import json
import math
import statistics
import sys
from pathlib import Path

source = Path(sys.argv[1])
destination = Path(sys.argv[2])
names = ('Physics.PhysXTask', 'Physics.PhysXTaskRun', 'Physics.PhysXTaskRelease')
tasks = {}
for line in source.read_text(encoding='utf-8-sig').splitlines():
    event = json.loads(line)
    if event['name'] in names:
        task = tasks.setdefault((event['session'], event['tick'], event['task']), {})
        if event['name'] in task:
            raise RuntimeError('Duplicate task scope')
        task[event['name']] = event

complete = []
boundary = 0
for identity, task in tasks.items():
    if names[0] not in task:
        boundary += 1  # Exporter omits boundary-clipped parent spans.
        continue
    if any(name not in task for name in names):
        raise RuntimeError(f'Missing child scope for complete parent {identity}')
    parent, run, release = (task[name] for name in names)
    if not (identity[2] and parent['osThreadId'] == run['osThreadId'] == release['osThreadId']):
        raise RuntimeError(f'Task identity/thread mismatch {identity}')
    ordered = (parent['beginUs'], run['beginUs'], run['endUs'], release['beginUs'], release['endUs'], parent['endUs'])
    if any(left > right + 0.001 for left, right in zip(ordered, ordered[1:])):
        raise RuntimeError(f'Task hierarchy/order mismatch {identity}')
    complete.append(task)

if len(complete) < 1000:
    raise RuntimeError('Insufficient product task samples')

metrics = {}
for name in names:
    values = sorted(task[name]['durationUs'] for task in complete)
    metrics[name] = dict(count=len(values), meanUs=statistics.mean(values), p99Us=values[math.ceil(len(values)*0.99)-1], maximumUs=values[-1])

result = dict(result='PRODUCT_TASK_SPLIT_VERIFIED', completeTasks=len(complete), boundaryParentsExcluded=boundary,
    durations=metrics, scope='Wall durations include OS waits/preemption; nested parent and child timings must not be added',
    performanceAccepted=False)
destination.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
