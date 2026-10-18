"""xperf의 CSwitch/ReadyThread 시각으로 ETW 구간의 실행·Ready·대기를 분해한다.

분석 구간보다 넓게 이벤트를 내보낸다. xperf의 구간 단위는 마이크로초다.
  xperf -i trace.etl -o events.csv -a dumper -range 18700000 18850000
측정 프레임을 변경하거나 제외하지 않고, 초 단위의 정확한 구간을 분석한다.
  python analyze-scheduler-ready.py --events events.csv --thread 34924 \
      --begin 18.7605336 --end 18.8363875 --output scheduler.json

WPA의 집계 Waits/Ready 열과 달리 모든 시간을 요청 구간 안으로 잘라 계산한다.
여러 CPU의 합은 CPU 밀리초이며 경과 시간이 아니다. 관측하지 못한 상태는
Unknown으로 남긴다. 컨텍스트 스위치로 구한 CPU 점유 시간은 별도로 수집하지
않은 인터럽트·DPC 시간도 포함하므로 순수 명령 실행 시간으로 해석하지 않는다.
xperf 텍스트 시각의 정밀도는 마이크로초다. 수명 이벤트가 있으면 프로세스의
재사용된 PID를 분리하고, 없으면 PID 수명을 확인할 수 없다고 표시한다.
"""

import argparse
import collections
import csv
import json
import re
from pathlib import Path


READY_STATES = frozenset(('Ready', 'DeferredReady', 'Standby'))
PROCESS_ID = re.compile(r'^(.*)\(\s*(\d+)\)$')


def overlap(begin, end, other_begin, other_end):
    return max(0.0, min(end, other_end) - max(begin, other_begin))


def read_events(path):
    switches = []
    ready = []
    priority = []
    lifetimes = []
    instances = {}

    def process(label):
        match = PROCESS_ID.match(label)
        if not match:
            raise ValueError(f'Unrecognized process identity: {label!r}')
        pid = int(match.group(2))
        instance = instances.get(pid)
        return dict(name=match.group(1).strip(), pid=pid,
                    instance=instance, lifetimeKnown=instance is not None)

    with Path(path).open(encoding='utf-8-sig', newline='') as source:
        for line_number, row in enumerate(csv.reader(source, skipinitialspace=True), 1):
            row = [item.strip() for item in row]
            if len(row) < 2 or not row[1].isdigit():
                continue
            kind, time = row[0], int(row[1])
            try:
                if kind in ('P-Start', 'P-DCStart'):
                    identity = process(row[2])
                    instances[identity['pid']] = f'{time}:{row[5]}'
                elif kind == 'CSwitch':
                    switches.append(dict(timeUs=time, newProcess=process(row[2]), newTid=int(row[3]),
                                         newPriority=int(row[4]), oldProcess=process(row[8]),
                                         oldTid=int(row[9]), oldPriority=int(row[10]), oldState=row[12],
                                         reason=row[13], cpu=int(row[16]), idealCpu=int(row[17])))
                elif kind == 'ReadyThread':
                    ready.append(dict(timeUs=time, readierProcess=process(row[2]), readierTid=int(row[3]),
                                      targetProcess=process(row[4]), targetTid=int(row[5]), reason=row[6],
                                      increment=int(row[7]), cpu=int(row[8]), inDpc=bool(row[9])))
                elif kind in ('T-SetPriority', 'T-SetBasePriority'):
                    priority.append(dict(timeUs=time, kind=kind, callerProcess=process(row[2]),
                                         callerTid=int(row[3]), targetProcess=process(row[4]),
                                         targetTid=int(row[5]), oldPriority=int(row[6]), newPriority=int(row[7])))
                elif kind in ('T-Start', 'T-End', 'T-DCStart', 'T-DCEnd'):
                    lifetimes.append(dict(timeUs=time, kind=kind, process=process(row[2]), tid=int(row[3])))
            except (ValueError, IndexError) as error:
                raise ValueError(f'{path}:{line_number}: malformed {kind}: {error}') from error
    for events in (switches, ready, priority, lifetimes):
        events.sort(key=lambda event: event['timeUs'])
    return dict(switches=switches, ready=ready, priority=priority, lifetimes=lifetimes)


def process_key(process):
    return process['pid'], process['name'], process.get('instance')


def aggregate_owners(running, windows, cpu=None):
    totals = collections.defaultdict(float)
    identities = {}
    for run in running:
        if cpu is not None and run['cpu'] != cpu:
            continue
        duration = sum(overlap(begin, end, run['beginUs'], run['endUs']) for begin, end in windows)
        if duration:
            key = process_key(run['process'])
            identities[key] = run['process']
            totals[key] += duration / 1000
    return [dict(process=identities[key], cpuMs=round(value, 6))
            for key, value in sorted(totals.items(), key=lambda item: -item[1])]


def analyze(events, thread, begin, end):
    if not 0 <= begin < end:
        raise ValueError('Expected 0 <= begin < end in microseconds')
    switches, ready = events['switches'], events['ready']
    target_events = [event for event in switches if thread in (event['newTid'], event['oldTid'])]
    if not target_events:
        raise ValueError(f'No context switches for thread {thread}')
    # 같은 TID가 재사용되면 번호만으로 분석할 스레드 수명을 선택할 수 없다.
    lifetime_boundaries = [event for event in events['lifetimes'] if event['tid'] == thread
                           and event['kind'] in ('T-Start', 'T-End')]
    if any(begin < event['timeUs'] < end for event in lifetime_boundaries):
        raise ValueError('Target thread lifetime changes inside the window; choose one thread incarnation')

    cpu_events = collections.defaultdict(list)
    for event in switches:
        cpu_events[event['cpu']].append(event)
    running = []
    validated_running_brackets = set()
    warnings = []
    for cpu, rows in cpu_events.items():
        for first, second in zip(rows, rows[1:]):
            if first['newTid'] != second['oldTid'] or process_key(first['newProcess']) != process_key(second['oldProcess']):
                if overlap(begin, end, first['timeUs'], second['timeUs']):
                    warnings.append(f'CPU {cpu}: context-switch identity gap at {first["timeUs"]}..{second["timeUs"]} us')
                continue
            running.append(dict(beginUs=first['timeUs'], endUs=second['timeUs'], cpu=cpu,
                                tid=first['newTid'], process=first['newProcess'], priority=first['newPriority']))
            if first['newTid'] == thread:
                validated_running_brackets.add((id(first), id(second)))

    intervals = []
    target_priority = []
    for first, second in zip(target_events, target_events[1:]):
        interval_begin, interval_end = first['timeUs'], second['timeUs']
        if not overlap(begin, end, interval_begin, interval_end):
            continue
        state, readier = 'Unknown', None
        # 요청 창 밖의 종료·재생성도 양쪽 switch가 같은 스레드의 증거인지 바꾼다.
        # 같은 마이크로초의 선후는 확증할 수 없으므로 bracket 양 끝의 경계도 보수적으로 거부한다.
        if any(interval_begin <= event['timeUs'] <= interval_end for event in lifetime_boundaries):
            warnings.append(f'Target thread lifetime boundary in state bracket {interval_begin}..{interval_end} us')
        elif first['newTid'] == thread and second['oldTid'] == thread:
            # target 이벤트만 연결하면 다른 CSwitch에서 발견한 CPU 정체성 단절을 건너뛸 수 있다.
            # 같은 CPU에서 검증한 연속 switch 쌍만 실제 실행 상태의 근거로 사용한다.
            if (first['cpu'] == second['cpu']
                    and process_key(first['newProcess']) == process_key(second['oldProcess'])
                    and (id(first), id(second)) in validated_running_brackets):
                state = 'Running'
            else:
                warnings.append(f'Unverified target Running bracket {interval_begin}..{interval_end} us')
        elif first['oldTid'] == thread and second['newTid'] == thread:
            if process_key(first['oldProcess']) == process_key(second['newProcess']):
                if first['oldState'] in READY_STATES:
                    state = 'Ready'
                elif first['oldState'] == 'Waiting':
                    notifications = [event for event in ready if event['targetTid'] == thread
                                     and interval_begin <= event['timeUs'] <= interval_end
                                     and process_key(event['targetProcess']) == process_key(first['oldProcess'])]
                    if notifications:
                        readier = notifications[0]
                        if len(notifications) > 1:
                            warnings.append(f'Multiple ready notifications between {interval_begin} and {interval_end} us')
                        intervals.append(dict(state='Waiting', beginUs=max(begin, interval_begin),
                                              endUs=min(end, readier['timeUs']), out=first, switchIn=second,
                                              readier=readier))
                        interval_begin = readier['timeUs']
                        state = 'Ready'
        intervals.append(dict(state=state, beginUs=max(begin, interval_begin), endUs=min(end, interval_end),
                              out=first if first['oldTid'] == thread else None,
                              switchIn=second if second['newTid'] == thread else first, readier=readier))

    intervals = sorted((row for row in intervals if row['endUs'] > row['beginUs']), key=lambda row: row['beginUs'])
    covered = []
    position = begin
    for interval in intervals:
        if interval['beginUs'] < position:
            raise ValueError('Overlapping target-thread states; trace ordering or identity is inconsistent')
        if interval['beginUs'] > position:
            covered.append(dict(state='Unknown', beginUs=position, endUs=interval['beginUs']))
        covered.append(interval)
        position = interval['endUs']
    if position < end:
        covered.append(dict(state='Unknown', beginUs=position, endUs=end))
    totals = dict.fromkeys(('Running', 'Ready', 'Waiting', 'Unknown'), 0.0)
    for interval in covered:
        interval['durationMs'] = (interval['endUs'] - interval['beginUs']) / 1000
        totals[interval['state']] += interval['durationMs']
        if interval['state'] == 'Ready':
            interval['allCpuOwners'] = aggregate_owners(running, [(interval['beginUs'], interval['endUs'])])
            interval['previousCpuOwners'] = aggregate_owners(running, [(interval['beginUs'], interval['endUs'])],
                                                            interval['out']['cpu'])
    for event in target_events:
        if begin <= event['timeUs'] <= end:
            target_priority.append(dict(timeUs=event['timeUs'],
                                        kind='switchIn' if event['newTid'] == thread else 'switchOut',
                                        priority=event['newPriority'] if event['newTid'] == thread else event['oldPriority'],
                                        cpu=event['cpu']))
    ready_windows = [(row['beginUs'], row['endUs']) for row in covered if row['state'] == 'Ready']
    cpu_coverage = [dict(cpu=cpu,
                         coveredMs=sum(overlap(begin, end, run['beginUs'], run['endUs']) / 1000
                                       for run in running if run['cpu'] == cpu)) for cpu in sorted(cpu_events)]
    if totals['Unknown']:
        warnings.append('Unobserved target state: export a wider interval containing prior and next target switches')
    for coverage in cpu_coverage:
        if abs(coverage['coveredMs'] - (end - begin) / 1000) > 0.000001:
            warnings.append(f'Incomplete CPU {coverage["cpu"]} running ownership in requested window')
    return dict(schema=1, threadId=thread, beginUs=begin, endUs=end, wallMs=(end-begin) / 1000,
                timestampResolutionUs=1, statesMs=totals, stateCoverageComplete=not totals['Unknown'],
                observedCpus=sorted(cpu_events), cpuCoverage=cpu_coverage,
                allCpuOwners=aggregate_owners(running, [(begin, end)]),
                allCpuOwnersDuringReady=aggregate_owners(running, ready_windows),
                readyIntervals=sorted((row for row in covered if row['state'] == 'Ready'),
                                      key=lambda row: -row['durationMs']),
                stateTimeline=covered, switchPriorities=target_priority,
                explicitPriorityChanges=[event for event in events['priority']
                                         if event['targetTid'] == thread and begin <= event['timeUs'] <= end],
                warnings=warnings,
                limits=['CPU ownership is scheduler residency including unseparated interrupt/DPC time.',
                        'CPU milliseconds sum across observed logical CPUs; they are not wall milliseconds.',
                        'An initial replacement thread does not own the entire subsequent Ready delay.',
                        'Target state is Unknown across thread lifetime boundaries, including equal-time boundaries.',
                        'Process lifetime is unknown when no lifecycle event was exported.',
                        'Observed CPU IDs do not establish the machine CPU count.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--events', type=Path, required=True)
    parser.add_argument('--thread', type=int, required=True)
    parser.add_argument('--begin', type=float, required=True, help='Interval start in trace-relative seconds')
    parser.add_argument('--end', type=float, required=True, help='Interval end in trace-relative seconds')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = analyze(read_events(args.events), args.thread, args.begin * 1000000, args.end * 1000000)
    result['eventsPath'] = str(args.events.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({key: result[key] for key in ('wallMs', 'statesMs', 'stateCoverageComplete', 'warnings')}, indent=2))


if __name__ == '__main__':
    main()
