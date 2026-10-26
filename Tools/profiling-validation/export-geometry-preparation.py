"""Export the fixed-load SceneHost comparison to the existing PC timeline schema."""
import argparse
import bisect
import collections
import csv
import hashlib
import importlib.util
import json
import statistics
from pathlib import Path


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def save(path, value):
    with path.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, separators=(",", ":"))


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def stats(values):
    values = sorted(values)
    return dict(count=len(values), mean=statistics.mean(values), p50=statistics.median(values),
                p95=values[min(len(values) - 1, int(len(values) * .95))]) if values else None


class StateIntervals:
    def __init__(self, states):
        self.states = states
        self.starts = [row[0] for row in states]
        self.prefix = [[0.] for _ in range(5)]
        for _, duration, state, _ in states:
            for kind, prefix in enumerate(self.prefix):
                prefix.append(prefix[-1] + (duration if state == kind else 0.))

    def integral(self, time):
        index = bisect.bisect_right(self.starts, time) - 1
        if index < 0:
            return [0.] * 5
        start, duration, state, _ = self.states[index]
        partial = min(duration, max(0., time - start))
        return [self.prefix[kind][index] + (partial if state == kind else 0.) for kind in range(5)]


def intersection(begin, end, states):
    left, right = states.integral(begin), states.integral(end)
    result = [max(0., b - a) for a, b in zip(left, right)]
    return dict(unknownMs=result[0], runningMs=result[1], readyMs=result[2], waitingMs=result[3],
                terminatedMs=result[4])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--title", default="기하 준비 연속 인덱스 · Debug 전후 ETW")
    parser.add_argument("--change", default="dense-geometry-preparation-index")
    options = parser.parse_args()
    root = options.root.resolve()
    repo = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location("reader", repo / "Tools/regression/summarize-material-profile.py")
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    timeline = root / "timeline"
    timeline.mkdir(exist_ok=True)
    entries, results = [], []
    for receipt in load(root / "runs.json"):
        assert receipt["passed"], receipt["name"]
        name = receipt["name"]
        directory = root / name
        samples = load(directory / "frames.json")
        started = load(directory / "started.json")
        hz = started["hz"]
        origin = samples[0]["beginQpc"]
        stamp = lambda tick: (int(tick) - origin) * 1000 / hz
        end = stamp(samples[-1]["endQpc"])
        recording = name.endswith("-on")
        capture = reader.read_capture(directory / "native.ceprof") if recording else None
        if capture:
            assert capture["complete"] and not capture["unacked"] and not any(capture["losses"].values())
            assert capture["hz"] == hz
            save(directory / "native.json", capture)
        target_ids = set(started["threadIds"])
        lanes = {}
        markers = [marker["name"] for marker in capture["markers"]] if capture else []
        main_tid = None
        seen_scopes = set()
        duplicate_scopes = 0
        if capture:
            for slot, title in capture["threads"].items():
                tid = capture["threadIds"][slot]
                target_ids.add(tid)
                lanes[str(tid)] = dict(tid=tid, names=[title], scopes=[], states=[], maxDuration=0)
                if title == "[RenderThread]":
                    main_tid = tid
            for frame in capture["frames"]:
                for event in frame["events"]:
                    if event["flags"] & 4:
                        continue
                    scope_key = (event["slot"], event["marker"], event["begin"], event["end"], event["depth"], tuple(event["cpu"]))
                    if scope_key in seen_scopes:
                        duplicate_scopes += 1
                        continue
                    seen_scopes.add(scope_key)
                    tid = capture["threadIds"][event["slot"]]
                    lane = lanes[str(tid)]
                    lane["scopes"].append([stamp(event["begin"]), (event["end"] - event["begin"]) * 1000 / hz,
                                           event["marker"], event["depth"], event["flags"],
                                           event["cpu"][1], event["cpu"][2]])
        else:
            # Process thread zero is captured at the warm gate, before it can exit.
            main_tid = started["threadIds"][0]
            lanes[str(main_tid)] = dict(tid=main_tid, names=["[RenderThread] · recording OFF"],
                                        scopes=[], states=[], maxDuration=0)
        assert main_tid
        if capture:
            assert main_tid == started["threadIds"][0], "Main thread snapshot order differs from capture"
        transitions = collections.defaultdict(list)
        raw = []
        with (directory / "kernel.csv").open(encoding="utf-8-sig") as stream:
            for row in csv.reader(stream):
                kind, tick, cpu, incoming, outgoing, old_state, reason = row
                tick, cpu, incoming, outgoing, old_state, reason = map(int, row[1:])
                if incoming not in target_ids and (kind != "S" or outgoing not in target_ids):
                    continue
                raw.append([kind, str(tick), cpu, incoming, outgoing, old_state, reason])
                time = stamp(tick)
                if kind == "S":
                    if outgoing in target_ids:
                        state = 2 if old_state in (1, 3, 7) else 3 if old_state == 5 else 4 if old_state == 4 else 0
                        transitions[outgoing].append((time, 0, state, -1))
                    if incoming in target_ids:
                        transitions[incoming].append((time, 1, 1, cpu))
                elif incoming in target_ids:
                    transitions[incoming].append((time, 2, 2, -1))
        save(directory / "kernel-events.json", dict(schema=1, qpcHz=hz,
             fields=["kind", "qpcTick", "cpu", "incomingOrReadyTid", "outgoingTid", "oldThreadState", "waitReason"],
             events=raw, source="kernel.etl", sourceSha256=digest(directory / "kernel.etl")))
        for tid in target_ids:
            lane = lanes.setdefault(str(tid), dict(tid=tid, names=[], scopes=[], states=[], maxDuration=0))
            cursor, state, cpu = 0., 0, -1
            for time, kind, new_state, new_cpu in sorted(transitions[tid]):
                if time > end:
                    break
                if kind == 2 and state == 1:
                    continue
                if time > cursor:
                    lane["states"].append([cursor, time - cursor, state, cpu])
                cursor = max(0., time)
                state, cpu = new_state, new_cpu
            if cursor < end:
                lane["states"].append([cursor, end - cursor, state, cpu])
            lane["scopes"].sort()
            lane["maxDuration"] = max((scope[1] for scope in lane["scopes"]), default=0)
        main_states = StateIntervals(lanes[str(main_tid)]["states"])
        frame_states = []
        for frame in samples:
            frame_states.append(dict(step=frame["step"],
                frame=intersection(stamp(frame["beginQpc"]), stamp(frame["endQpc"]), main_states),
                seal=intersection(stamp(frame["beginQpc"]), stamp(frame.get("sealEndQpc", frame["beginQpc"] + round(frame["sealMs"] * hz / 1000))), main_states),
                prepare=intersection(stamp(frame["prepareBeginQpc"]), stamp(frame["prepareEndQpc"]),
                                     main_states)))
        metrics = {field: stats([frame[field] for frame in samples]) for field in
                   ("sealMs", "prepareMs", "declareMs", "submitWaitMs", "frameMs")}
        scopes = collections.defaultdict(list)
        scope_cpu = collections.defaultdict(list)
        for scope in lanes[str(main_tid)]["scopes"]:
            if scope[0] >= 0 and scope[0] + scope[1] <= end:
                title = markers[scope[2]]
                scopes[title].append(scope[1])
                scope_cpu[title].append(intersection(scope[0], scope[0] + scope[1], main_states))
        frame_totals = collections.defaultdict(lambda: [0.] * len(samples))
        frame_running = collections.defaultdict(lambda: [0.] * len(samples))
        starts = [stamp(frame["beginQpc"]) for frame in samples]
        ends = [stamp(frame["endQpc"]) for frame in samples]
        for scope in lanes[str(main_tid)]["scopes"]:
            frame_index = bisect.bisect_right(starts, scope[0]) - 1
            if frame_index >= 0 and scope[0] + scope[1] <= ends[frame_index]:
                title = markers[scope[2]]
                frame_totals[title][frame_index] += scope[1]
                frame_running[title][frame_index] += intersection(
                    scope[0], scope[0] + scope[1], main_states)["runningMs"]
        scope_metrics = {name: dict(wallMs=stats(values), **{
            state: stats([item[state] for item in scope_cpu[name]])
            for state in ("runningMs", "readyMs", "waitingMs", "unknownMs")}) for name, values in scopes.items()}
        if capture:
            bookkeeping = [max(0., a - b) for a, b in zip(
                frame_totals["MaterialSceneGeometryPreparation"], frame_totals["MaterialMeshGpuPrepare"])]
            metrics["geometryBookkeepingMs"] = stats(bookkeeping)
        source = str((directory / "native.ceprof").relative_to(root)) if capture else str((directory / "frames.json").relative_to(root))
        identity = capture["sha256"] if capture else None
        data = dict(id=name, variant=name, mode="etw", begin=0, end=end,
                    windows=[dict(name="on" if recording else "off", begin=0, end=end)], markers=markers,
                    lanes=lanes, measurements=[[stamp(frame["beginQpc"]),
                        stamp(frame["endQpc"]) - stamp(frame["beginQpc"]), frame["step"] + 10008, 941] for frame in samples],
                    dpc={}, dpcAvailable=False, nativeSha256=identity, source=source, hz=hz,
                    sourceLabel="원본 CEPROF" if capture else "프레임 QPC JSON · 기록 OFF",
                    frameBoundarySourceSha256=digest(directory / "frames.json"),
                    nativeEvents=sum(len(lane["scopes"]) for lane in lanes.values()), duplicateScopesRemoved=duplicate_scopes,
                    partialScopeCount=sum(bool(scope[4] & 3) for lane in lanes.values() for scope in lane["scopes"]),
                    etwAvailable=True, etwSource=str((directory / "kernel.etl").relative_to(root)),
                    alignment=dict(qpcAnchor=origin, utcOffsetMs=0), losses=capture["losses"] if capture else {},
                    rhiIdentities=[], recordingOn=recording)
        data["frameBoundaries"] = [row[:3] + [0] for row in data["measurements"]]
        if capture:
            identities = []
            for frame in capture["frames"]:
                for sample in frame["measurements"]:
                    if sample["axis"] == 4:
                        identities.append([stamp(sample["begin"]), stamp(sample["end"]), sample["marker"],
                            capture["threadIds"][sample["slot"]], str(sample["recordingId"]), str(sample["frameId"]),
                            str(sample["viewId"]), str(sample["deviceIncarnation"]), sample["backendGeneration"],
                            str(sample["queueId"]), sample["queueKind"], sample["targetIndex"],
                            str(sample["sourceEngineFrame"]) if sample["sourceEngineFrameAvailable"] else None])
            data["rhiIdentities"] = sorted(identities)
        gpu = dict(id=name, events=[], queues={}, markers=[], maxDuration=0,
                   summary=dict(sourceSha256=identity, exactProvenanceMatches=0, events=0,
                                available=False, reason="이 하네스는 GPU timestamp를 수집하지 않습니다. GPU 유휴 시간을 뜻하지 않습니다."))
        save(timeline / (name + ".json"), data)
        save(timeline / (name + "-gpu.json"), gpu)
        build, case, repeat, record = name.split("-")
        label = f'{"변경 전" if build == "before" else "변경 후"} · {"동일 1종" if case == "same" else "64종"} · {repeat.upper()} · 기록 {record.upper()}'
        entries.append(dict(id=name, variant=name, mode="etw", label=label, events=data["nativeEvents"],
                            cpu=name + ".json", gpu=name + "-gpu.json"))
        all_thread_frame_scopes = {}
        if capture:
            selected = {"GeometryFeedbackReadbackCreate", "MaterialSceneCameraVisibilityTask", "MaterialSceneCameraJoin", "MaterialClusterRequestSort", "MeshClusterRequests", "MeshPrepareStaticCache", "MeshPrepareOwner", "MeshPrepareTransformLookup", "MaterialSceneDrawChunks", "MaterialSceneChunkRecordUpload", "MaterialSceneInputSeal", "GeometryScaleFixtureSeal", "MaterialSceneSealGeometryIdentities", "MaterialSceneSealPinValidation", "MaterialMeshPlanForScene"}
            for title in selected:
                all_thread_frame_scopes[title] = [dict(count=0, wallMs=0., runningMs=0., readyMs=0., waitingMs=0., unknownMs=0.) for _ in samples]
            for lane in lanes.values():
                intervals = StateIntervals(lane["states"])
                for scope in lane["scopes"]:
                    title = markers[scope[2]]
                    if title not in selected:
                        continue
                    index = bisect.bisect_right(starts, scope[0]) - 1
                    if index < 0 or scope[0] + scope[1] > ends[index]:
                        continue
                    row = all_thread_frame_scopes[title][index]
                    row["count"] += 1
                    row["wallMs"] += scope[1]
                    states = intersection(scope[0], scope[0] + scope[1], intervals)
                    for field in ("runningMs", "readyMs", "waitingMs", "unknownMs"):
                        row[field] += states[field]
        binding_tasks = []
        for lane in lanes.values():
            for scope in lane["scopes"]:
                if markers[scope[2]] == "MaterialSceneDrawBindingsTask" and scope[0] >= 0 and scope[0] + scope[1] <= end:
                    binding_tasks.append(dict(tid=lane["tid"], begin=scope[0], durationMs=scope[1]))
        result = dict(name=name, frames=len(samples), metrics=metrics, scopes=scope_metrics, mainTid=main_tid,
                      frameStates=frame_states, allThreadFrameScopes=all_thread_frame_scopes, bindingTasks=binding_tasks, bindingTaskWallMs=stats([item["durationMs"] for item in binding_tasks]),
                      scopeFrameTotals={key: stats(value) for key, value in frame_totals.items()},
                      scopeFrameRunningTotals={key: stats(value) for key, value in frame_running.items()}, fullCapture=bool(capture), etwTargetEvents=len(raw))
        save(directory / "analysis.json", result)
        results.append(result)
        print(name, "prepare p50", metrics["prepareMs"]["p50"], flush=True)
    comparisons = []
    for case in ("same", "distinct"):
        if not any(row["name"].startswith("before-" + case + "-") for row in results):
            continue
        for recording in ("on", "off"):
            select = lambda build: [row for row in results if row["name"].startswith(build + "-" + case + "-")
                                     and row["name"].endswith("-" + recording)]
            before, after = select("before"), select("after")
            comparison = dict(case=case, recording=recording, trialsPerBuild=2, order="ABBA", metrics={})
            fields = ["sealMs", "prepareMs", "declareMs", "submitWaitMs", "frameMs"]
            if recording == "on":
                fields.append("geometryBookkeepingMs")
            for field in fields:
                old = statistics.mean(row["metrics"][field]["p50"] for row in before)
                new = statistics.mean(row["metrics"][field]["p50"] for row in after)
                comparison["metrics"][field] = dict(beforeTrialMedianMean=old, afterTrialMedianMean=new,
                                                   deltaMs=new - old, deltaPercent=(new / old - 1) * 100)
            comparisons.append(comparison)
    save(root / "analysis.json", dict(schema=1, runs=results, comparisons=comparisons,
         scope="Debug native product SceneHost; 1024 admitted instances; one visible instance; four recording workers",
         notDisplayFps=True, synchronousGpuReadbackPerFrame=True, otherCachePoliciesUnchanged=True, change=options.change))
    save(timeline / "viewer.json", dict(schema=1, title=options.title, report="../index.html", datasets=entries))


if __name__ == "__main__":
    main()
