"""Read CEPROF snapshots (1/2/4) and finalized recordings (3/5).

Engine-frame duration, real-view render cost and presented cadence are distinct.
Only recorded, exact-key provenance can qualify render measurements. Old files
remain readable but cannot acquire provenance from current live settings.
Authored for later authorized execution; no acceptance result is checked in.
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import statistics
import struct
import zlib


def require(condition, message):
    if not condition:
        raise ValueError(message)


class Reader:
    def __init__(self, data):
        self.data = memoryview(data)
        self.pos = 0

    def take(self, size):
        require(0 <= size <= len(self.data) - self.pos, 'Truncated CEPROF field')
        result = self.data[self.pos:self.pos + size]
        self.pos += size
        return result

    def get(self, fmt):
        result = struct.unpack('<' + fmt, self.take(struct.calcsize('<' + fmt)))
        return result[0] if len(result) == 1 else result

    def string(self):
        return bytes(self.take(self.get('I'))).decode('utf-8')

    def count(self, minimum_size):
        count = self.get('I')
        require(count <= (len(self.data) - self.pos) // minimum_size, 'Impossible CEPROF count')
        return count

    def end(self):
        require(self.pos == len(self.data), 'Unexpected CEPROF trailing fields')


def stats(values):
    if not values:
        return None
    values = sorted(values)
    return dict(mean=statistics.mean(values), p50=statistics.median(values),
                p95=values[max(0, math.ceil(len(values) * .95) - 1)], max=max(values))


def event(reader, version):
    names = ('begin', 'end', 'marker', 'frame', 'slot', 'depth', 'flags',
             'queue', 'submission', 'view', 'reserved')
    value = dict(zip(names, reader.get('QQIIHHBBIHH')))
    value['cpu'] = reader.get('QQQ') if version == 2 else (0, 0, 0)
    require(value['end'] >= value['begin'] and value['flags'] <= 15, 'Malformed event')
    return value


def counter(reader, version):
    cid, value = reader.get('Hd')
    owner = reader.get('QQQ') if version == 2 else (0, 0, 0)
    require(cid > 0 and math.isfinite(value), 'Malformed counter')
    return cid, value, owner


def measurement(reader):
    keys = ('engineFrame', 'axis', 'queue', 'eventView', 'eventSubmission', 'marker',
            'submissionId', 'begin', 'end', 'frameKind', 'resolutionState', 'upscaler',
            'frameGenerator', 'realFrameId', 'viewId', 'sceneEpoch', 'publicationFrameId',
            'generatedOrdinal', 'renderWidth', 'renderHeight', 'displayWidth', 'displayHeight',
            'nativeGateActive', 'spatialProvenanceAvailable', 'spatialMode', 'deepDvcApplied')
    value = dict(zip(keys, reader.get('IBBHIIQQQBBBBQQQQIIIIIBBBB')))
    require(value['axis'] in (1, 2) and value['end'] >= value['begin'], 'Invalid measurement axis/time')
    require(value['frameKind'] <= 2 and value['resolutionState'] <= 4 and
            value['upscaler'] <= 3 and value['frameGenerator'] <= 3 and value['spatialMode'] <= 2 and
            all(value[key] in (0, 1) for key in ('nativeGateActive', 'spatialProvenanceAvailable', 'deepDvcApplied')),
            'Unknown measurement provenance vocabulary')
    if value['frameKind'] == 0:
        require(value['resolutionState'] == 0 and not value['spatialProvenanceAvailable'] and
                not value['nativeGateActive'], 'Invalid unknown provenance')
    else:
        require(all(value[key] > 0 for key in ('realFrameId', 'publicationFrameId', 'renderWidth',
                    'renderHeight', 'displayWidth', 'displayHeight')) and value['resolutionState'] != 0,
                'Missing measurement identities/extents')
        require((value['frameKind'] == 1 and value['generatedOrdinal'] == 0) or
                (value['frameKind'] == 2 and value['generatedOrdinal'] > 0 and value['frameGenerator'] != 0),
                'Frame kind/ordinal mismatch')
        same_extent = (value['renderWidth'], value['renderHeight']) == (value['displayWidth'], value['displayHeight'])
        if value['resolutionState'] == 2:
            require(value['upscaler'] != 0 and
                    (not value['spatialProvenanceAvailable'] or value['spatialMode'] != 1), 'Invalid reconstruction')
        elif value['resolutionState'] == 4:
            require(value['upscaler'] == 0 and value['spatialProvenanceAvailable'] and value['spatialMode'] == 1 and
                    value['renderWidth'] <= value['displayWidth'] and value['renderHeight'] <= value['displayHeight'],
                    'Invalid spatial scaling')
        else:
            require(value['upscaler'] == 0 and same_extent and
                    (not value['spatialProvenanceAvailable'] or value['spatialMode'] != 1), 'Invalid native extents')
    if value['axis'] == 2:
        require(value['marker'] != 0 and value['eventSubmission'] == (value['submissionId'] & 0xffffffff) and
                value['eventView'] == (value['viewId'] & 0xffff), 'Measurement/event key mismatch')
    return value


def snapshot(data):
    reader = Reader(data)
    require(bytes(reader.take(8)) == b'CEPROF\0\0', 'Not a CEPROF capture')
    version, count = reader.get('II')
    require(version in (1, 2, 4) and 4 <= count <= 64, 'Unsupported snapshot layout')
    chunks, ranges = {}, []
    for _ in range(count):
        kind, chunk_version, offset, size, checksum, reserved = reader.get('IIQQII')
        supported = {1: (1,), 2: (1,), 3: (1, 2), 4: (1, 2), 5: (1, 2), 6: (1,), 7: (1,)}
        require(kind in supported and chunk_version in supported[kind] and kind not in chunks and reserved == 0,
                'Unknown/duplicate snapshot chunk')
        require(offset >= 16 + 32 * count and offset + size <= len(data) and
                all(offset >= end or offset + size <= begin for begin, end in ranges), 'Invalid chunk range')
        payload = memoryview(data)[offset:offset + size]
        require(zlib.crc32(payload) == checksum, 'Chunk checksum mismatch')
        chunks[kind] = chunk_version, Reader(payload)
        ranges.append((offset, offset + size))
    require(all(key in chunks for key in (1, 2, 3, 4)) and (version != 4 or 7 in chunks), 'Missing required chunk')
    require(version == 4 or 7 not in chunks, 'Legacy capture cannot carry new provenance')
    env = chunks[1][1]
    hz, complete, unacked = env.get('QBI')
    require(hz > 0 and complete in (0, 1), 'Invalid capture environment')
    env.end()
    markers = []
    r = chunks[2][1]
    for _ in range(r.count(13)):
        kind, line = r.get('BI')
        markers.append(dict(name=r.string(), file=r.string(), line=line, kind=kind))
    r.end()
    threads = {}
    r = chunks[3][1]
    for _ in range(r.count(17)):
        slot, osid, kind, order = r.get('IIBI')
        require(slot not in threads, 'Duplicate thread slot')
        threads[slot] = r.string()
    r.end()
    descriptors = {}
    if 6 in chunks:
        r = chunks[6][1]
        for _ in range(r.count(14)):
            cid, category = r.get('HI')
            require(cid not in descriptors, 'Duplicate counter descriptor')
            descriptors[cid] = (r.string(), r.string(), category)
        r.end()
    frames = []
    r = chunks[4][1]
    for _ in range(r.count(32)):
        fid, begin, end, dropped, events = r.get('IQQQI')
        require(end >= begin and events <= (len(r.data) - r.pos) // (62 if chunks[4][0] == 2 else 38),
                'Invalid frame/event count')
        frames.append(dict(id=fid, begin=begin, end=end, dropped=dropped, counters=[], measurements=[],
                           events=[event(r, chunks[4][0]) for _ in range(events)]))
    r.end()
    dropped_counters = 0
    if 5 in chunks:
        r = chunks[5][1]
        dropped_counters, counter_frames = r.get('QI')
        require(counter_frames == len(frames), 'Counter/frame count mismatch')
        for frame in frames:
            require(r.get('I') == frame['id'], 'Counter/frame identity mismatch')
            frame['counters'] = [counter(r, chunks[5][0]) for _ in range(r.count(34 if chunks[5][0] == 2 else 10))]
        r.end()
    if 7 in chunks:
        r = chunks[7][1]
        require(r.get('I') == len(frames), 'Provenance/frame count mismatch')
        for frame in frames:
            require(r.get('I') == frame['id'], 'Provenance/frame identity mismatch')
            frame['measurements'] = [measurement(r) for _ in range(r.count(100))]
        r.end()
    return dict(version=version, hz=hz, complete=bool(complete), unacked=unacked, frames=frames,
                markers=markers, threads=threads, descriptors=descriptors, droppedCounters=dropped_counters,
                finalized=True, losses={})


def read_capture(path):
    require(path.stat().st_size <= 256 * 1024 * 1024, 'Capture exceeds this tool\'s 256 MiB limit; use ProfilerViewer')
    data = path.read_bytes()
    require(len(data) >= 16 and data[:8] == b'CEPROF\0\0', 'Not a CEPROF capture')
    version, count = struct.unpack_from('<II', data, 8)
    if version in (1, 2, 4):
        result = snapshot(data)
    else:
        require(version in (3, 5) and count == 0, 'Unsupported CEPROF recording version')
        r, sequence, metadata, frames, footer = Reader(data[16:]), 0, None, [], None
        while r.pos < len(r.data):
            header = r.take(32)
            magic, kind, serial, size, payload_crc, header_crc = struct.unpack('<IIQQII', header)
            require(magic == 0x4b435043 and serial == sequence and footer is None, 'Invalid recording sequence/tail')
            require(zlib.crc32(header[:28]) == header_crc and size <= 32 * 1024 * 1024, 'Invalid record header')
            payload = r.take(size)
            require(zlib.crc32(payload) == payload_crc, 'Record checksum mismatch')
            sequence += 1
            if kind == 1:
                new = snapshot(payload)
                require(not new['frames'] and new['version'] == (4 if version == 5 else 2), 'Invalid metadata record')
                if metadata is not None:
                    require(new['hz'] == metadata['hz'] and new['markers'][:len(metadata['markers'])] == metadata['markers'] and
                            all(new['threads'].get(k) == v for k, v in metadata['threads'].items()), 'Metadata identity changed')
                metadata = new
            elif kind == 2:
                require(metadata is not None, 'Frame preceded metadata')
                body = Reader(payload)
                fid, begin, end, dropped, events, counters = body.get('IQQQII')
                require(end >= begin and (not frames or fid > frames[-1]['id']) and
                        events * 62 + counters * 34 <= len(payload) - 36, 'Invalid recording frame')
                frame = dict(id=fid, begin=begin, end=end, dropped=dropped,
                             events=[event(body, 2) for _ in range(events)],
                             counters=[counter(body, 2) for _ in range(counters)], measurements=[])
                if version == 5:
                    frame['measurements'] = [measurement(body) for _ in range(body.count(100))]
                body.end()
                frames.append(frame)
            elif kind == 3:
                body = Reader(payload)
                complete, unacked = body.get('BI')
                names = ('writtenFrames', 'submittedFrames', 'firstTick', 'lastTick', 'droppedFrames', 'droppedEvents',
                         'droppedCounters', 'sourceDroppedCounters', 'sourceDroppedEvents', 'sourceDroppedFrameBoundaries',
                         'lateCpuEvents', 'lateGpuSpans')
                footer = dict(zip(names, body.get('Q' * 12)))
                body.end()
                require(complete in (0, 1) and footer['writtenFrames'] == len(frames) and
                        footer['submittedFrames'] == len(frames) + footer['droppedFrames'], 'Invalid recording footer')
            else:
                raise ValueError('Unknown recording record type')
        require(metadata is not None and footer is not None, 'Recording is not finalized; recovery is not acceptance')
        result = dict(metadata, version=version, frames=frames, complete=bool(complete), unacked=unacked,
                      droppedCounters=footer['droppedCounters'] + footer['sourceDroppedCounters'],
                      losses={k: v for k, v in footer.items() if k.startswith(('dropped', 'sourceDropped', 'late'))})
    result['sha256'] = hashlib.sha256(data).hexdigest()
    return result


def sample_key(value):
    return (value['engineFrame'], value['marker'], value['begin'], value['end'],
            value['queue'], value['eventSubmission'], value['eventView'])


def analyze(path):
    capture = read_capture(Path(path))
    frames, hz = capture['frames'], capture['hz']
    require(frames, 'Empty capture is not a measurement')
    events, counters = collections.defaultdict(list), collections.defaultdict(list)
    cpu_sessions, cpu_ticks, cpu_tasks, counter_owners = set(), set(), set(), set()
    boundary, gpu_events, matched_gpu, invalid_provenance = 0, 0, 0, 0
    measurements, groups = [], collections.defaultdict(lambda: {'cpu': [], 'gpu': [], 'realFrames': set()})
    for frame in frames:
        gpu_keys = collections.Counter(sample_key(m) for m in frame['measurements'] if m['axis'] == 2)
        used_keys = collections.Counter()
        for m in frame['measurements']:
            require(m['engineFrame'] == frame['id'], 'Stored provenance belongs to another engine frame')
            measurements.append(m)
            available = m['frameKind'] in (1, 2) and m['viewId'] > 0 and m['sceneEpoch'] > 0 and m['spatialProvenanceAvailable']
            invalid_provenance += not available
            key = tuple(m[k] for k in ('frameKind', 'viewId', 'sceneEpoch', 'renderWidth', 'renderHeight',
                                       'displayWidth', 'displayHeight', 'upscaler', 'frameGenerator', 'resolutionState',
                                       'spatialMode', 'deepDvcApplied'))
            groups[key]['cpu' if m['axis'] == 1 else 'gpu'].append((m['end'] - m['begin']) * 1000 / hz)
            groups[key]['realFrames'].add((m['realFrameId'], m['publicationFrameId']))
        for e in frame['events']:
            require(e['marker'] < len(capture['markers']) and e['slot'] in capture['threads'], 'Unknown marker/thread')
            owner = e['cpu']
            if owner[0]:
                cpu_sessions.add(owner[0])
                cpu_ticks.add(owner[:2])
                if owner[2]:
                    cpu_tasks.add(owner)
            if e['flags'] & 4:
                gpu_events += 1
                key = (frame['id'], e['marker'], e['begin'], e['end'], e['queue'], e['submission'], e['view'])
                used_keys[key] += 1
                matched_gpu += gpu_keys[key] == 1 and used_keys[key] == 1
            if e['flags'] & 8:
                continue
            if e['flags'] & 3:
                boundary += 1
                continue
            marker = capture['markers'][e['marker']]
            key = (capture['threads'][e['slot']], marker['name'], bool(e['flags'] & 4), e['view'], marker['file'])
            events[key].append((e['end'] - e['begin']) * 1000 / hz)
        invalid_provenance += sum(count for key, count in gpu_keys.items() if count != 1 or used_keys[key] != 1)
        for cid, value, owner in frame['counters']:
            counters[(cid, owner[0])].append((frame['id'], owner[1], value))
            if owner[0]:
                counter_owners.add(owner[:2])
    rows = [dict(thread=k[0], name=k[1], gpu=k[2], view=k[3], file=k[4], calls=len(v),
                 perEngineFrame=sum(v) / len(frames), **stats(v)) for k, v in events.items()]
    rows.sort(key=lambda row: row['perEngineFrame'], reverse=True)
    counter_rows = []
    for (cid, session), values in sorted(counters.items()):
        name, unit, category = capture['descriptors'].get(cid, (str(cid), '', 0))
        counter_rows.append(dict(name=name, unit=unit, category=category, session=session, samples=len(values),
                                 min=min(x[2] for x in values), max=max(x[2] for x in values), last=values[-1][2],
                                 lastTick=values[-1][1], lastFrame=values[-1][0]))
    clean = (capture['complete'] and capture['unacked'] == 0 and not any(capture['losses'].values()) and
             capture['droppedCounters'] == 0 and not any(f['dropped'] for f in frames))
    eligible = bool(clean and capture['version'] in (4, 5) and measurements and gpu_events and
                    matched_gpu == gpu_events and invalid_provenance == 0)
    real = eligible and all(m['frameKind'] == 1 for m in measurements)
    native = real and all(m['resolutionState'] == 1 and m['upscaler'] == m['frameGenerator'] == 0 and
                          m['spatialMode'] == 0 and not m['deepDvcApplied'] for m in measurements)
    group_rows = []
    for key, group in groups.items():
        names = ('frameKind', 'viewId', 'sceneEpoch', 'renderWidth', 'renderHeight', 'displayWidth', 'displayHeight',
                 'upscaler', 'frameGenerator', 'resolutionState', 'spatialMode', 'deepDvcApplied')
        group_rows.append(dict(zip(names, key), realFrameCount=len(group['realFrames']),
                               cpuRenderSubmitMs=stats(group['cpu']), cpuSamples=len(group['cpu']),
                               gpuPassMs=stats(group['gpu']), gpuPassSamples=len(group['gpu'])))
    return dict(path=str(path), sha256=capture['sha256'], formatVersion=capture['version'], frames=len(frames),
                frameMs=stats([(f['end'] - f['begin']) * 1000 / hz for f in frames]),
                frameMsAxis='engine-frame-boundaries-not-real-render-or-presented-fps',
                complete=capture['complete'], unacked=capture['unacked'], droppedEvents=sum(f['dropped'] for f in frames),
                excludedBoundaryEvents=boundary, markers=rows, droppedCounters=capture['droppedCounters'],
                recordingLosses=capture['losses'], counterTicks=len(counter_owners), counters=counter_rows,
                cpuOwnership=dict(sessions=len(cpu_sessions), ticks=len(cpu_ticks), tasks=len(cpu_tasks)),
                temporalPerformanceGateEligible=eligible, realFramePerformanceGateEligible=real,
                nativeRealPerformanceGateEligible=native,
                nativeQualityGateEligible=bool(native and all(m['nativeGateActive'] for m in measurements)),
                pixelAcceptance=False, presentationPacingAcceptance=False, latencyAcceptance=False,
                temporalProvenanceStatus='recorded-exact-key' if eligible else 'missing-incomplete-or-ambiguous',
                spatialProvenanceStatus='recorded-per-measurement' if eligible else 'unavailable-or-incomplete',
                gpuEvents=gpu_events, matchedGpuEvents=matched_gpu, invalidProvenanceSamples=invalid_provenance,
                renderMeasurementCount=len(measurements), renderGroups=group_rows,
                renderAxis='per-view-cpu-submission-and-gpu-pass-costs-not-display-or-input-to-photon')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--require-native-real', action='store_true')
    parser.add_argument('paths', nargs='+')
    args = parser.parse_args()
    for path in args.paths:
        result = analyze(path)
        Path(str(path) + '.summary.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(json.dumps(dict(result, markers=result['markers'][:35]), indent=2))
        require(not args.require_native_real or result['nativeRealPerformanceGateEligible'],
                'Capture does not establish lossless native-real per-frame performance provenance')


if __name__ == '__main__':
    main()
