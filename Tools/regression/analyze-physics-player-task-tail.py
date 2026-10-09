import json
import math
import statistics
from pathlib import Path

root = Path(__file__).resolve().parents[2]/'Build/Verification/ContactStream/M3Acceptance'
ids = ['36d55636375e4577afab318ee1ab92f7','c77b2e439628413abf41004a8031dc93','48676553acbe4719bcf09125cb640c8e']
results = []
for identity in ids:
    ticks = {}
    for line in (root/f'task-{identity}.jsonl').read_text(encoding='utf-8-sig').splitlines():
        e = json.loads(line)
        ticks.setdefault((e['session'],e['tick']),[]).append(e)
    rows = []
    missing_owner = 0
    unmatched = 0
    for key, events in ticks.items():
        owners = {e['name']:e for e in events if e['task']==0}
        if 'Physics.FetchWait' not in owners:
            missing_owner += 1
            continue
        submissions = {e['task']:e for e in events if e['name']=='Physics.TaskSubmit'}
        completions = {e['task']:e for e in events if e['name']=='Physics.TaskComplete'}
        tasks = [e for e in events if e['name']=='Physics.PhysXTask']
        queues = []
        task_wall = []
        invalid = 0
        for task in tasks:
            submit, done = submissions.get(task['task']), completions.get(task['task'])
            if not submit or not done or submit['beginUs']>task['beginUs']+.001 or done['beginUs']+.001<task['endUs']:
                invalid += 1
                continue
            queues.append(task['beginUs']-submit['beginUs'])
            task_wall.append(task['durationUs'])
        unmatched += invalid
        row = {'session':key[0],'tick':key[1],'frame':owners['Physics.FetchWait']['frame'],
               'fetchWaitUs':owners['Physics.FetchWait']['durationUs'],'tasks':len(tasks),'unmatched':invalid,
               'queueMaxUs':max(queues,default=0),'queueMeanUs':statistics.mean(queues) if queues else 0,
               'taskWallMaxUs':max(task_wall,default=0),'taskWallSumAcrossWorkersUs':sum(task_wall),
               'fetchResultsUs':owners.get('Physics.FetchResults',{}).get('durationUs',0),
               'dispatcherDrainUs':owners.get('Physics.DispatcherDrain',{}).get('durationUs',0),
               'stagesUs':{n:e['durationUs'] for n,e in owners.items()}}
        rows.append(row)
    rows.sort(key=lambda r:r['fetchWaitUs'])
    tail = rows[-max(1,math.ceil(len(rows)*.01)):]
    baseline = rows[:max(1,math.floor(len(rows)*.5))]
    def group_summary(group):
        return {metric:statistics.median(r[metric] for r in group) for metric in ('fetchWaitUs','queueMaxUs','taskWallMaxUs','taskWallSumAcrossWorkersUs','fetchResultsUs','dispatcherDrainUs','tasks')}
    results.append({'captureId':identity,'ticks':len(rows),'unmatchedTasks':unmatched,'ticksWithoutCompleteFetchWait':missing_owner,
                    'baselineLowerHalfMedian':group_summary(baseline),'slowestOnePercentMedian':group_summary(tail),
                    'slowestTicks':tail,'rows':rows})
report = {'result':'M3_PLAYER_PHYSICS_TASK_TAIL_ANALYZED','runs':results,
          'scope':'Session/tick/task correlated timestamps; queue is submit-to-task-start wall latency; task duration is wall time, not OS on-CPU time; worker sum is not owner total',
          'performanceAccepted':False}
(root/'task-tail-analysis.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
for r in results:
    print(r['captureId'], 'ticks',r['ticks'],'unmatched',r['unmatchedTasks'],'boundary',r['ticksWithoutCompleteFetchWait'])
    print('baseline',r['baselineLowerHalfMedian'])
    print('tail',r['slowestOnePercentMedian'])