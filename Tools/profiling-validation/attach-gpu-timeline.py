"""Attach existing calibrated CEPROF GPU spans to a generated CPU/ETW timeline.

Run from the repository: python Tools/profiling-validation/attach-gpu-timeline.py
    --root Artifacts/remeasure-10-20261019
The input timeline retains every CPU scope. GPU records join provenance by their
exact event key, never by proximity to the CPU frame or the collection frame.
"""
import argparse
import base64
import collections
import gzip
import importlib.util
import json
import re
import statistics
from pathlib import Path


def union_ms(intervals):
    merged = []
    for begin, end in sorted(intervals):
        if merged and begin <= merged[-1][1]:
            merged[-1][1] = max(end, merged[-1][1])
        else:
            merged.append([begin, end])
    return sum(end - begin for begin, end in merged)


def stats(values):
    values = sorted(values)
    if not values:
        return None
    return dict(count=len(values), mean=statistics.mean(values),
                p50=statistics.median(values), p95=values[min(len(values)-1, int(len(values)*.95))])


def extract(reader, capture_path, cpu_data, capture=None):
    capture = capture if capture is not None else reader.read_capture(capture_path)
    if not capture['complete'] or capture['unacked'] or any(capture['losses'].values()):
        raise ValueError(f'Capture is incomplete or lossy: {capture_path}')
    if capture['sha256'] != cpu_data['nativeSha256'] or capture['hz'] != cpu_data['hz']:
        raise ValueError('CPU and GPU source capture identities differ')
    origin = cpu_data['alignment']['qpcAnchor']
    offset = cpu_data['alignment']['utcOffsetMs']
    hz = capture['hz']
    stamp = lambda tick: (tick-origin)*1000/hz+offset
    events, seen, queues = [], set(), {}
    duplicates = zero = 0
    windows = cpu_data['windows']
    if cpu_data['mode'] == 'baseline':
        foreground = json.loads((capture_path.parent/'baseline-foreground-history.json').read_text('utf-8-sig'))
        window = dict(begin=stamp(foreground['beginQpc']), end=stamp(foreground['endQpc']))
    else:
        window = next(w for w in windows if w['name'] == 'on')
    groups = collections.defaultdict(list)
    passes = collections.defaultdict(list)
    for frame in capture['frames']:
        metadata = {}
        for measurement in frame['measurements']:
            if measurement['axis'] != 2:
                continue
            key = reader.sample_key(measurement)
            if key in metadata:
                raise ValueError('Duplicate GPU provenance key')
            metadata[key] = measurement
        used = set()
        for event in frame['events']:
            if not event['flags'] & 4:
                continue
            key = (frame['id'], event['marker'], event['begin'], event['end'],
                   event['queue'], event['submission'], event['view'])
            measurement = metadata.get(key)
            if measurement is None:
                raise ValueError('GPU span has no exact provenance measurement')
            if event['flags'] & 3:
                raise ValueError('Partial GPU timestamps cannot establish a complete pass')
            if any(measurement[field] > (1 << 53)-1 for field in ['viewId', 'realFrameId']):
                raise ValueError('CPU viewer frame/view identity exceeds JavaScript integer precision')
            used.add(key)
            if key in seen:
                raise ValueError('Duplicate GPU span identity')
            seen.add(key)
            begin, end = stamp(event['begin']), stamp(event['end'])
            duration = (event['end']-event['begin'])*1000/hz
            if duration == 0:
                zero += 1
            # Strings retain full uint64 identities even in a JavaScript viewer.
            record = [begin, duration, event['marker'], event['queue'],
                      measurement['viewId'], str(measurement['submissionId']), frame['id'],
                      str(measurement['realFrameId']), measurement['frameKind'],
                      measurement['generatedOrdinal'], str(event['begin']), str(event['end'])]
            events.append(record)
            queues[str(event['queue'])] = capture['threads'].get(event['slot'], '[GPU]')
            group = (measurement['viewId'], str(measurement['submissionId']), event['queue'])
            groups[group].append((begin, end))
        if used != set(metadata):
            raise ValueError('GPU provenance records without spans')
    events.sort(key=lambda event: (event[0], event[3], event[4], event[2]))
    submissions = []
    accepted = set()
    for (view, submission, queue), spans in groups.items():
        if min(a for a,b in spans) < window['begin'] or max(b for a,b in spans) > window['end']:
            continue
        accepted.add((view, submission, queue))
        submissions.append(dict(view=view, submission=submission, queue=queue,
                                spanMs=max(b for a,b in spans)-min(a for a,b in spans),
                                unionMs=union_ms(spans), passSumMs=sum(b-a for a,b in spans)))
    for event in events:
        if (event[4], event[5], event[3]) not in accepted:
            continue
        name = re.sub(r' \[gpu scope \d+\]$', '', capture['markers'][event[2]]['name'])
        passes[(event[4], name)].append(event[1])
    summary = dict(events=len(events), exactProvenanceMatches=len(seen),
                   duplicatesRemoved=duplicates, zeroDurationSpans=zero,
                   sourceSha256=capture['sha256'], sourceLosses=capture['losses'],
                   alignedTo='existing CPU QPC anchor; GPU begin/end already calibrated by backend',
                   window=window, queues=queues,
                   views={str(view):dict(submissions=sum(x['view']==view for x in submissions),
                                         spanMs=stats([x['spanMs'] for x in submissions if x['view']==view]),
                                         unionMs=stats([x['unionMs'] for x in submissions if x['view']==view]))
                          for view in sorted({x['view'] for x in submissions})},
                   passMs=[dict(view=view, name=name, **stats(values))
                           for (view,name),values in sorted(passes.items())])
    return dict(events=events, queues=queues, maxDuration=max((e[1] for e in events), default=0),
                summary=summary, clock='QPC-aligned GPU timestamp; not GPU utilization'), summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--data-only', action='store_true', help='Write JSON only; reuse the common viewer')
    args = parser.parse_args()
    root = args.root.resolve()
    repo = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('ceprof_reader', repo/'Tools/regression/summarize-material-profile.py')
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    manifest = json.loads((root/'timeline/manifest.json').read_text('utf-8-sig'))
    tags, summaries = [], {}
    for item in manifest:
        data = json.loads((root/'timeline'/(item['id']+'.json')).read_text('utf-8-sig'))
        gpu, summary = extract(reader, root/item['variant']/(item['mode']+'.ceprof'), data)
        (root/'timeline'/(item['id']+'-gpu.json')).write_text(json.dumps(gpu, separators=(',',':')), 'utf-8')
        if args.data_only:
            summaries[item['id']] = summary
            continue
        payload = base64.b64encode(gzip.compress(json.dumps(gpu, separators=(',',':')).encode(), mtime=0)).decode()
        tags.append(f'<script type="application/gzip" id="gpu-blob-{item["id"]}">{payload}</script>')
        summaries[item['id']] = summary
        print(item['id'], summary['events'], 'exact GPU spans', flush=True)
    (root/'gpu-metrics.json').write_text(json.dumps(summaries, indent=2), 'utf-8')
    if args.data_only:
        return
    source = root/'timeline.src.html'
    html = source.read_text('utf-8')
    if 'id="gpuTimeline"' in html:
        raise ValueError('GPU integration already attached; regenerate CPU source before reattaching')
    fragment = (Path(__file__).with_name('gpu-timeline.html')).read_text('utf-8')
    script = (Path(__file__).with_name('gpu-timeline.js')).read_text('utf-8')
    html = html.replace('<div class="controls"><label class="check"><input id="allThreads"',
                        fragment+'<div class="controls"><label class="check"><input id="allThreads"', 1)
    old = 'D=value;for(const z of defs)'
    if old not in html:
        raise ValueError('Unsupported CPU timeline loader; expected integration anchor missing')
    html = html.replace(old, 'D=value;await attachGpuData(D);if(v!==version)return;for(const z of defs)', 1)
    html = html.replace('drawPhases();drawThreads();window.CETimelineRange',
                        'drawPhases();drawGpuTimeline();drawThreads();window.CETimelineRange', 1)
    html = html.replace('CPU 시간축 ms`;', 'CPU·GPU QPC 시간축 ms · GPU ${D.gpu.events.length.toLocaleString()} spans`;', 1)
    html = html.replace('<script>const MANIFEST=', '\n'.join(tags)+'<script>const MANIFEST=', 1)
    index = html.rfind('</script>')
    html = html[:index]+'\n'+script+'\n'+html[index:]
    html = html.replace('GPU 큐 실행·완료, 화면 표시 FPS, Ready 지연의 원인 프로세스는 이 그림만으로 확정하지 않는다.',
                        'GPU 패스 시간은 백엔드 타임스탬프이며 화면 표시 FPS나 GPU 사용률을 뜻하지 않는다.')
    html = html.replace('GPU 스코프는 별도 시간영역이므로 이 CPU 그림에 더하지 않는다.',
                        'GPU는 별도 큐 레인에 표시하고 CPU 스코프나 ETW Running과 합산하지 않는다.')
    (root/'timeline-gpu.src.html').write_text(html, 'utf-8')
    (root/'gpu-verdict.json').write_text(json.dumps(dict(passed=True, datasets={k:dict(
        events=v['events'], matched=v['exactProvenanceMatches'], duplicates=v['duplicatesRemoved'])
        for k,v in summaries.items()}, newEngineInstrumentation=False, newCapture=False), indent=2), 'utf-8')


if __name__ == '__main__':
    main()
