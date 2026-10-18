"""기록 작업의 JSON/NDJSON 스냅샷을 분석한다. 실제 실행이나 ETW 수집은 수행하지 않는다."""

import argparse
import json
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path


AXIS = "cpu-recording-wall-not-cpu-residency-or-gpu"
CLOCK = "steady-clock-nanoseconds"
UNIT_AXIS = "record-body-including-repeat-phase-barriers"
SPIN_AXIS = "frame-cumulative-snapshot-inflight-exits-may-follow"
UINT64_MAX = (1 << 64) - 1
NODE_STAMPS = (
    "registeredNs", "readyNs", "publishBeginNs", "publishEndNs", "startNs", "bodyEndNs", "completeNs"
)
SPIN_COUNTERS = (
    "attempts", "taskHits", "budgetExhaustions", "scopeExits", "shutdownExits",
    "wallNs", "reservedBudgetNs", "maxActiveSpinners"
)
METRIC_DESCRIPTIONS = {
    "dispatchToReturn": "Recording dispatch/join wall interval; excludes other frame phases.",
    "waitBeginToReturn": "Owner final wait call and return; not measured OS Waiting time.",
    "submittedToDispatcher": "Graph submission to dispatcher callback; not pure OS wake latency.",
    "registeredToReady": "Dependency/admission delay after node registration; includes upstream work.",
    "readyToPublishBegin": "Ready notification to publication call begin.",
    "publishCall": "Publication call bracket; can overlap or contain task execution.",
    "publishBeginToStart": "Publication begin to execution; not exact queue residence or pure OS wake.",
    "nodeBody": "Executed native node callback wall time; can include preemption.",
    "skippedNodeBody": "Scheduler body of a skipped dependency; no user callback executed.",
    "bodyEndToComplete": "Body return to native completion notification.",
    "nodeCompleteToOwnerReturn": "Includes remaining independent jobs and owner resumption.",
    "graphCompleteToOwnerReturn": "Final graph completion to owner return; not pure OS wake latency.",
    "unitRecordBody": "One pass/slice RecordPassBody wall interval, including repeated-phase barriers.",
    "unitCallbackTotal": "Sum of actual callback wall intervals in one unit; not an individual-callback percentile.",
    "ownerSpinWall": "Owner bounded-spin wall interval, including possible OS preemption.",
}


def unsigned(value, label, positive=False):
    if type(value) is not int or not (1 if positive else 0) <= value <= UINT64_MAX:
        raise ValueError(f"{label} must be a {'positive' if positive else 'nonnegative'} uint64 integer")
    return value


def boolean(value, label):
    if type(value) is not bool:
        raise ValueError(f"{label} must be a boolean")
    return value


def distribution(values):
    if not values:
        return None
    ordered = sorted(values)

    def percentile(fraction):
        position = (len(ordered) - 1) * fraction
        lower = math.floor(position)
        upper = math.ceil(position)
        return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)

    return dict(count=len(ordered), min=ordered[0], p50=percentile(.5), p95=percentile(.95),
                max=ordered[-1], mean=sum(ordered) / len(ordered), total=sum(ordered))


def parse_documents(text, source="<memory>"):
    """한 파일 안의 문서는 같은 session이다. 파일 사이에는 frameId를 합치지 않는다."""
    text = text.lstrip("\ufeff")
    try:
        return [(source, json.loads(text))]
    except json.JSONDecodeError:
        documents = []
        for line_number, line in enumerate(text.splitlines(), 1):
            if not line.strip():
                continue
            try:
                documents.append((source, json.loads(line)))
            except json.JSONDecodeError as error:
                raise ValueError(f"{source}:{line_number}: invalid JSON/NDJSON: {error.msg}") from error
        if not documents:
            raise ValueError(f"{source}: no JSON documents")
        return documents


def find_traces(value, path="$", view=None):
    if isinstance(value, list):
        for index, item in enumerate(value):
            yield from find_traces(item, f"{path}[{index}]", view)
    elif isinstance(value, dict):
        provenance = value.get("temporalProvenance")
        if isinstance(provenance, dict) and provenance.get("viewId") is not None:
            view = str(provenance["viewId"])
        if value.get("viewId") is not None:
            view = str(value["viewId"])
        if "timingEnabled" in value and "schemaVersion" in value and (
                "dispatchBeginNs" in value or value.get("clock") == CLOCK or value.get("axis") == AXIS):
            yield path, view, value
            return
        for name, item in value.items():
            item_path = f"{path}.{name}"
            if name == "schedulingTrace":
                yield item_path, view, item
            else:
                yield from find_traces(item, item_path, view)
    elif isinstance(value, str) and "schedulingTrace" in value and value.lstrip().startswith(("{", "[")):
        try:
            decoded = json.loads(value)
        except json.JSONDecodeError:
            return
        yield from find_traces(decoded, f"{path}.decodedJson", view)


def read_policy(trace):
    policy = trace.get("policy")
    if not isinstance(policy, dict):
        raise ValueError("policy must be an object")
    result = {name: unsigned(policy.get(name), f"policy.{name}")
              for name in ("ownerSpinUs", "workerSpinUs", "frameSpinUs", "spinWorkers")}
    result["dependencyGraph"] = boolean(trace.get("dependencyGraph"), "dependencyGraph")
    result["batchSubmit"] = boolean(trace.get("batchSubmit"), "batchSubmit")
    return result


def add_metric(group, name, value):
    group["_samples"][name].append(value)


def add_warning(result, item, message):
    result["warnings"].append(dict(session=item["session"], path=item["path"], message=message))


def read_worker_snapshot(trace, item, policy, result):
    spin = trace.get("workerSpin")
    if not isinstance(spin, dict):
        add_warning(result, item, "workerSpin snapshot unavailable")
        return None
    if spin.get("scopeAccepted") is False:
        result["counts"]["unacceptedSpinScopeSnapshots"] += 1
        return None
    try:
        accepted = boolean(spin.get("scopeAccepted"), "workerSpin.scopeAccepted")
        if not accepted:
            return None
        if spin.get("axis") != SPIN_AXIS:
            raise ValueError("unsupported workerSpin axis")
        if any(policy[name] == 0 for name in ("workerSpinUs", "frameSpinUs", "spinWorkers")):
            raise ValueError("accepted workerSpin scope has a disabled policy")
        frame = unsigned(spin.get("frameId"), "workerSpin.frameId")
        if frame != unsigned(trace.get("frameId"), "frameId"):
            raise ValueError("workerSpin.frameId differs from the recording frame")
        stats = {name: unsigned(spin.get(name), f"workerSpin.{name}") for name in SPIN_COUNTERS}
        stats["activeSpinners"] = unsigned(spin.get("activeSpinners"), "workerSpin.activeSpinners")
        if stats["activeSpinners"] > policy["spinWorkers"]:
            raise ValueError("workerSpin active count exceeds its worker policy")
        if stats["reservedBudgetNs"] > policy["frameSpinUs"] * 1000:
            raise ValueError("workerSpin reserved budget exceeds its frame policy")
        if stats["maxActiveSpinners"] > policy["spinWorkers"]:
            raise ValueError("workerSpin maximum active count exceeds its worker policy")
    except ValueError as error:
        add_warning(result, item, str(error))
        result["counts"]["invalidSpinSnapshots"] += 1
        return None
    # 개별 relaxed counter는 하나의 원자적 스냅샷이 아니다. attempts와 exit 합의 등식은 강제하지 않는다.
    return dict(session=item["session"], frameId=frame, viewId=item["viewId"],
                observedReturnNs=trace.get("returnNs", 0),
                policy={key: policy[key] for key in ("workerSpinUs", "frameSpinUs", "spinWorkers")},
                statistics=stats)


def finish_worker_frames(snapshots, result):
    frames = defaultdict(list)
    for snapshot in snapshots:
        frames[(snapshot["session"], snapshot["frameId"])].append(snapshot)
    for (session, frame), observations in sorted(frames.items()):
        # Scene/Game에 공통인 누계 중 실제 관측 하나를 고른다. 각 필드의 최댓값을 조립하지 않는다.
        policies = {json.dumps(row["policy"], sort_keys=True) for row in observations}
        candidates = [row for row in observations if all(
            all(row["statistics"][key] >= older["statistics"][key] for key in SPIN_COUNTERS)
            for older in observations)]
        ordered = sorted(
            (row for row in observations if type(row["observedReturnNs"]) is int and row["observedReturnNs"] > 0),
            key=lambda row: row["observedReturnNs"])
        regressed = any(
            newer["observedReturnNs"] > older["observedReturnNs"] and any(
                newer["statistics"][key] < older["statistics"][key] for key in SPIN_COUNTERS)
            for older, newer in zip(ordered, ordered[1:]))
        summary = dict(session=session, frameId=frame, snapshotCount=len(observations),
                       views=sorted({row["viewId"] for row in observations if row["viewId"] is not None}),
                       frameFinal=False, countersAreNonTransactional=True)
        if not candidates or len(policies) != 1 or regressed:
            summary.update(status="ambiguous-cumulative-snapshots", statistics=None)
            result["counts"]["ambiguousSpinFrames"] += 1
            result["warnings"].append(dict(session=session, path=f"workerSpin.frame[{frame}]",
                message="No single cumulative snapshot dominates the others; possible mixed process runs or "
                        "inconsistent snapshots. No view totals were summed."))
        else:
            selected = max(candidates, key=lambda row: (
                row["observedReturnNs"] if type(row["observedReturnNs"]) is int else 0,
                -row["statistics"]["activeSpinners"]))
            active = selected["statistics"]["activeSpinners"]
            summary.update(status="inflight-snapshot" if active else "quiescent-snapshot",
                           statistics=selected["statistics"], policy=selected["policy"],
                           observedReturnNs=selected["observedReturnNs"], snapshotInFlight=active != 0)
        result["workerSpinFrames"].append(summary)


def new_group(session, view, policy):
    return dict(session=session, viewId=view, policy=policy, traces=0, timingDisabled=0, invalidTraces=0,
                jobTimingUnavailable=0, truncatedJobTraces=0, truncatedUnitTraces=0,
                executedNodes=0, skippedNodes=0, invalidNodes=0, unavailableUnits=0, invalidUnits=0,
                callbackTimingUnavailableUnits=0, invalidCallbackTimings=0, callbackInvocations=0,
                ownerWaitUnavailable=0, invalidOwnerWaits=0, invalidJobSections=0,
                dispatcherHandoffs=0, nativeTasks=0, _samples=defaultdict(list),
                _units=defaultdict(list), _nodeRows=[], _unitRows=[])


def analyze_trace(item, group, result):
    trace = item["trace"]
    entry = dict(session=item["session"], path=item["path"], viewId=item["viewId"],
                 frameId=trace["frameId"], dispatchBeginNs=trace["dispatchBeginNs"])
    enabled = boolean(trace.get("timingEnabled"), "timingEnabled")
    if not enabled:
        group["timingDisabled"] += 1
        entry["status"] = "timing-disabled"
        return entry
    begin = unsigned(trace.get("dispatchBeginNs"), "dispatchBeginNs", positive=True)
    returned = unsigned(trace.get("returnNs"), "returnNs", positive=True)
    wait_begin = unsigned(trace.get("waitBeginNs"), "waitBeginNs")
    if returned < begin or (wait_begin and not begin <= wait_begin <= returned):
        raise ValueError("recording dispatch/wait/return timestamps are reversed")
    unit_truncated = boolean(trace.get("unitTimingsTruncated"), "unitTimingsTruncated")
    job_truncated = boolean(trace.get("jobTimingsTruncated"), "jobTimingsTruncated")
    group["traces"] += 1
    group["truncatedUnitTraces"] += unit_truncated
    group["truncatedJobTraces"] += job_truncated
    add_metric(group, "dispatchToReturn", returned - begin)
    if wait_begin:
        add_metric(group, "waitBeginToReturn", returned - wait_begin)
    entry.update(status="available", unitTimingsTruncated=unit_truncated,
                 jobTimingsTruncated=job_truncated, returnNs=returned)
    wait = trace.get("ownerWait")
    if not isinstance(wait, dict) or wait.get("available") is False or "available" not in wait:
        group["ownerWaitUnavailable"] += 1
    else:
        try:
            boolean(wait.get("available"), "ownerWait.available")
            if not wait_begin:
                raise ValueError("ownerWait is available without an observed wait interval")
            elapsed = unsigned(wait.get("spinElapsedNs"), "ownerWait.spinElapsedNs")
            boolean(wait.get("spinAttempted"), "ownerWait.spinAttempted")
            boolean(wait.get("completedDuringSpin"), "ownerWait.completedDuringSpin")
            boolean(wait.get("blockingPathEntered"), "ownerWait.blockingPathEntered")
            if elapsed > returned - wait_begin:
                raise ValueError("ownerWait spin wall is outside its measured wait interval")
            add_metric(group, "ownerSpinWall", elapsed)
        except ValueError as error:
            group["invalidOwnerWaits"] += 1
            add_warning(result, item, str(error))

    jobs = trace.get("jobs")
    node_intervals = {}
    if isinstance(jobs, dict) and type(jobs.get("available")) is not bool:
        group["jobTimingUnavailable"] += 1
        group["invalidJobSections"] += 1
        entry["jobsStatus"] = "invalid"
        add_warning(result, item, "jobs.available must be a boolean")
    elif not isinstance(jobs, dict) or jobs.get("available") is not True:
        group["jobTimingUnavailable"] += 1
        entry["jobsStatus"] = "truncated-unavailable" if job_truncated else "unavailable-for-this-path"
    else:
        try:
            submitted = unsigned(jobs.get("submittedNs"), "jobs.submittedNs", positive=True)
            completed = unsigned(jobs.get("completedNs"), "jobs.completedNs", positive=True)
            task_count = unsigned(jobs.get("nativeTasks"), "jobs.nativeTasks")
            handoffs = unsigned(jobs.get("dispatcherHandoffs"), "jobs.dispatcherHandoffs")
            dispatcher = unsigned(jobs.get("dispatcherBeginNs"), "jobs.dispatcherBeginNs",
                                  positive=task_count != 0)
            nodes = jobs.get("nodes")
            if not isinstance(nodes, list):
                raise ValueError("jobs.nodes must be an array")
            if not begin <= submitted <= completed <= returned:
                raise ValueError("jobs submission/completion is outside the recording interval")
            if task_count and not submitted <= dispatcher <= completed:
                raise ValueError("jobs dispatcher timestamp is outside graph lifetime")
            if len(nodes) > task_count or (len(nodes) != task_count and not job_truncated):
                raise ValueError("jobs node count differs from nativeTasks without declared truncation")
            group["nativeTasks"] += task_count
            group["dispatcherHandoffs"] += handoffs
            add_metric(group, "graphCompleteToOwnerReturn", returned - completed)
            if task_count:
                add_metric(group, "submittedToDispatcher", dispatcher - submitted)
            entry["jobsStatus"] = "available-truncated" if job_truncated else "available"
            node_counts = Counter(node["node"] for node in nodes
                                  if isinstance(node, dict) and type(node.get("node")) is int)
            for node in nodes:
                try:
                    if not isinstance(node, dict):
                        raise ValueError("job node must be an object")
                    identity = unsigned(node.get("node"), "node.node")
                    if identity >= task_count or node_counts[identity] != 1:
                        raise ValueError("job node identity is duplicate or outside nativeTasks")
                    times = {key: unsigned(node.get(key), f"node[{identity}].{key}", positive=True)
                             for key in NODE_STAMPS}
                    registered, ready, publish = times["registeredNs"], times["readyNs"], times["publishBeginNs"]
                    start, body_end, complete = times["startNs"], times["bodyEndNs"], times["completeNs"]
                    if not submitted <= registered <= ready <= publish <= start <= body_end <= complete <= completed:
                        raise ValueError(f"node[{identity}] timestamps violate causal order")
                    if not publish <= times["publishEndNs"] <= completed:
                        raise ValueError(f"node[{identity}] publication bracket is invalid")
                    skipped = boolean(node.get("skipped"), f"node[{identity}].skipped")
                    durations = dict(registeredToReady=ready - registered, readyToPublishBegin=publish - ready,
                                     publishCall=times["publishEndNs"] - publish,
                                     publishBeginToStart=start - publish, bodyEndToComplete=complete - body_end,
                                     nodeCompleteToOwnerReturn=returned - complete)
                    durations["skippedNodeBody" if skipped else "nodeBody"] = body_end - start
                    for name, duration in durations.items():
                        add_metric(group, name, duration)
                    group["skippedNodes" if skipped else "executedNodes"] += 1
                    group["_nodeRows"].append(dict(frameId=entry["frameId"], viewId=entry["viewId"],
                        node=identity, target=node.get("target"), wave=node.get("wave"), skipped=skipped,
                        startNs=start, bodyEndNs=body_end, durationsNs=durations,
                        executionOverlapsPublication=start < times["publishEndNs"],
                        publishEnkiThread=node.get("publishEnkiThread"),
                        executeEnkiThread=node.get("executeEnkiThread"),
                        publishOsThread=node.get("publishOsThread"), executeOsThread=node.get("executeOsThread"),
                        dispatcherRoute=node.get("dispatcherRoute")))
                    if type(node.get("target")) is int and type(node.get("wave")) is int:
                        node_intervals[(node["target"], node["wave"])] = (start, body_end, skipped)
                except ValueError as error:
                    group["invalidNodes"] += 1
                    add_warning(result, item, str(error))
        except ValueError as error:
            group["jobTimingUnavailable"] += 1
            group["invalidJobSections"] += 1
            entry["jobsStatus"] = "invalid"
            add_warning(result, item, str(error))

    units = trace.get("units")
    if trace.get("unitsAxis") != UNIT_AXIS:
        add_warning(result, item, "unit timing axis is unavailable or unsupported")
        group["invalidUnits"] += 1
        return entry
    if not isinstance(units, list):
        add_warning(result, item, "units must be an array")
        group["invalidUnits"] += 1
        return entry
    identity_fields = ("pass", "target", "wave", "slice", "sliceCount")
    unit_counts = Counter(tuple(unit[key] for key in identity_fields) for unit in units
                          if isinstance(unit, dict) and all(type(unit.get(key)) is int for key in identity_fields))
    for unit in units:
        try:
            if not isinstance(unit, dict):
                raise ValueError("unit timing must be an object")
            stamps = {key: unsigned(unit.get(key), f"unit.{key}") for key in ("beginNs", "endNs")}
            if not stamps["beginNs"] and not stamps["endNs"]:
                group["unavailableUnits"] += 1
                continue
            if not stamps["beginNs"] or not stamps["endNs"]:
                raise ValueError("unit timing has only one observed endpoint")
            if not begin <= stamps["beginNs"] <= stamps["endNs"] <= returned:
                raise ValueError("unit callback is reversed or outside the recording interval")
            identity = tuple(unsigned(unit.get(key), f"unit.{key}")
                             for key in identity_fields)
            if unit_counts[identity] != 1 or identity[4] == 0 or identity[3] >= identity[4]:
                raise ValueError("unit identity is duplicate or its slice is out of range")
            containing_node = node_intervals.get((identity[1], identity[2]))
            if containing_node is not None:
                node_start, node_end, skipped = containing_node
                if skipped or not node_start <= stamps["beginNs"] <= stamps["endNs"] <= node_end:
                    raise ValueError("unit record body is outside its native node or the node was skipped")
            name = unit.get("name")
            if not isinstance(name, str):
                raise ValueError("unit.name must be a string")
            work = unsigned(unit.get("estimatedWork"), "unit.estimatedWork")
            wall = stamps["endNs"] - stamps["beginNs"]
            callback_total = None
            callback_count = None
            if "callbackNs" not in unit or "callbackInvocations" not in unit:
                group["callbackTimingUnavailableUnits"] += 1
            else:
                try:
                    callback_total = unsigned(unit["callbackNs"], "unit.callbackNs")
                    callback_count = unsigned(unit["callbackInvocations"], "unit.callbackInvocations")
                    if callback_total > wall or (callback_count == 0 and callback_total != 0):
                        raise ValueError("unit callback sum exceeds its containing body or has no invocation")
                    add_metric(group, "unitCallbackTotal", callback_total)
                    group["callbackInvocations"] += callback_count
                except ValueError as error:
                    callback_total = callback_count = None
                    group["callbackTimingUnavailableUnits"] += 1
                    group["invalidCallbackTimings"] += 1
                    add_warning(result, item, str(error))
            row = dict(frameId=entry["frameId"], viewId=entry["viewId"], name=name, passId=identity[0],
                       target=identity[1], wave=identity[2], slice=identity[3], sliceCount=identity[4],
                       beginNs=stamps["beginNs"], endNs=stamps["endNs"], recordBodyWallNs=wall, estimatedWork=work,
                       callbackTotalNs=callback_total, callbackInvocations=callback_count,
                       bodyWallPerEstimatedWorkNs=wall / work if work else None)
            add_metric(group, "unitRecordBody", wall)
            group["_unitRows"].append(row)
            group["_units"][(identity[0], name)].append(row)
        except ValueError as error:
            group["invalidUnits"] += 1
            add_warning(result, item, str(error))
    return entry


def analyze(documents, top=10):
    """documents는 (session 식별자, JSON 값)의 반복자다. CLI는 각 파일을 별도 session으로 둔다."""
    result = dict(schemaVersion=1, axis=AXIS, clock=CLOCK, durationUnit="nanoseconds",
                  percentileMethod="linear interpolation on sorted durations",
                  sessionRule="Each input file is a separate session; repeated queries within it are deduplicated.",
                  unitsAxis=UNIT_AXIS, metricDescriptions=METRIC_DESCRIPTIONS,
                  policies=[], traces=[], workerSpinFrames=[], warnings=[],
                  counts=dict(documents=0, foundTraces=0, uniqueTraces=0, duplicateTraces=0,
                              conflictingTraces=0, invalidTraces=0,
                              unacceptedSpinScopeSnapshots=0, invalidSpinSnapshots=0,
                              ambiguousSpinFrames=0, quarantinedSpinSnapshots=0))
    unique = {}
    spin_snapshots = []
    groups = {}
    invalid_sessions = Counter()
    for session, document in documents:
        result["counts"]["documents"] += 1
        for path, view, trace in find_traces(document):
            result["counts"]["foundTraces"] += 1
            item = dict(session=str(session), path=path, viewId=view, trace=trace, conflict=False)
            try:
                if not isinstance(trace, dict):
                    raise ValueError("schedulingTrace must be an object")
                if trace.get("schemaVersion") != 1 or type(trace.get("schemaVersion")) is not int:
                    raise ValueError("unsupported schedulingTrace.schemaVersion")
                if trace.get("clock") != CLOCK or trace.get("axis") != AXIS:
                    raise ValueError("unsupported recording clock or timing axis")
                frame = unsigned(trace.get("frameId"), "frameId")
                dispatch = unsigned(trace.get("dispatchBeginNs"), "dispatchBeginNs")
                enabled = boolean(trace.get("timingEnabled"), "timingEnabled")
                policy = read_policy(trace)
                key = (str(session), frame, dispatch, view)
                fingerprint_data = {key: value for key, value in trace.items() if key != "workerSpin"}
                if not enabled:
                    fingerprint_data.pop("ownerWait", None)
                fingerprint = json.dumps(fingerprint_data, sort_keys=True, separators=(",", ":"))
                if key in unique:
                    result["counts"]["duplicateTraces"] += 1
                    unique[key]["snapshots"].append(trace)
                    if fingerprint != unique[key]["fingerprint"]:
                        if not unique[key]["conflict"]:
                            result["counts"]["conflictingTraces"] += 1
                            add_warning(result, item, "Conflicting payloads for one frame/view/dispatch; "
                                        "latency samples for this identity are excluded.")
                        unique[key]["conflict"] = True
                    continue
                item.update(policy=policy, fingerprint=fingerprint, snapshots=[trace])
                unique[key] = item
            except ValueError as error:
                result["counts"]["invalidTraces"] += 1
                invalid_sessions[item["session"]] += 1
                add_warning(result, item, str(error))
    result["counts"]["uniqueTraces"] = len(unique)
    for item in unique.values():
        policy_key = (item["session"], item["viewId"], json.dumps(item["policy"], sort_keys=True))
        group = groups.setdefault(policy_key, new_group(item["session"], item["viewId"], item["policy"]))
        if item["conflict"]:
            result["counts"]["invalidTraces"] += 1
            group["invalidTraces"] += 1
            invalid_sessions[item["session"]] += 1
            result["counts"]["quarantinedSpinSnapshots"] += len(item["snapshots"])
            result["traces"].append(dict(session=item["session"], path=item["path"],
                                         status="conflicting-duplicate", frameId=item["trace"]["frameId"]))
            continue
        error_fields = ("invalidNodes", "invalidUnits", "invalidCallbackTimings", "invalidOwnerWaits",
                        "invalidJobSections")
        errors_before = sum(group[name] for name in error_fields)
        try:
            entry = analyze_trace(item, group, result)
            if sum(group[name] for name in error_fields) != errors_before:
                result["counts"]["invalidTraces"] += 1
                group["invalidTraces"] += 1
                invalid_sessions[item["session"]] += 1
                result["counts"]["quarantinedSpinSnapshots"] += len(item["snapshots"])
                entry["status"] = "partial-invalid"
            else:
                for snapshot in item["snapshots"]:
                    spin = read_worker_snapshot(snapshot, item, item["policy"], result)
                    if spin is not None:
                        spin_snapshots.append(spin)
            result["traces"].append(entry)
        except ValueError as error:
            result["counts"]["invalidTraces"] += 1
            group["invalidTraces"] += 1
            invalid_sessions[item["session"]] += 1
            result["counts"]["quarantinedSpinSnapshots"] += len(item["snapshots"])
            add_warning(result, item, str(error))
            result["traces"].append(dict(session=item["session"], path=item["path"],
                                         status="invalid", frameId=item["trace"]["frameId"]))
    for group in groups.values():
        group["sessionHasInvalidTraces"] = invalid_sessions[group["session"]] != 0
        group["coverageComplete"] = bool(group["traces"]) and not group["sessionHasInvalidTraces"] and not any(
            group[name] for name in (
            "timingDisabled", "jobTimingUnavailable", "truncatedJobTraces", "truncatedUnitTraces",
            "invalidNodes", "unavailableUnits", "invalidUnits", "callbackTimingUnavailableUnits",
            "ownerWaitUnavailable", "invalidOwnerWaits", "invalidCallbackTimings", "invalidJobSections"))
        group["metricsNs"] = {name: distribution(values) for name, values in group.pop("_samples").items()}
        group["units"] = [
            dict(passId=identity[0], name=identity[1],
                 recordBodyWallNs=distribution([row["recordBodyWallNs"] for row in rows]),
                 callbackTotalNs=distribution([row["callbackTotalNs"] for row in rows
                                               if row["callbackTotalNs"] is not None]),
                 callbackInvocations=distribution([row["callbackInvocations"] for row in rows
                                                   if row["callbackInvocations"] is not None]),
                 estimatedWork=distribution([row["estimatedWork"] for row in rows]),
                 bodyWallPerEstimatedWorkNs=distribution(
                     [row["bodyWallPerEstimatedWorkNs"] for row in rows
                      if row["bodyWallPerEstimatedWorkNs"] is not None]))
            for identity, rows in group.pop("_units").items()]
        group["units"].sort(key=lambda row: row["recordBodyWallNs"]["p95"], reverse=True)
        node_rows = [row for row in group.pop("_nodeRows") if not row["skipped"]]
        group["slowestNodes"] = sorted(node_rows, key=lambda row: row["durationsNs"]["nodeBody"], reverse=True)[:top]
        unit_rows = group.pop("_unitRows")
        group["slowestUnits"] = sorted(unit_rows, key=lambda row: row["recordBodyWallNs"], reverse=True)[:top]
        group["largestCallbackTotals"] = sorted(
            (row for row in unit_rows if row["callbackTotalNs"] is not None),
            key=lambda row: row["callbackTotalNs"], reverse=True)[:top]
        result["policies"].append(group)
    finish_worker_frames(spin_snapshots, result)
    if not result["counts"]["foundTraces"]:
        result["warnings"].append(dict(session="", path="$", message="No schedulingTrace objects found. "
            "Enable CREATOR_RECORDING_JOB_TRACE=1 before starting the measured process to collect timing stages."))
    return result


def format_text(result, top=10):
    counts = result["counts"]
    lines = [
        "Recording scheduling analysis: CPU wall intervals",
        f"Traces: found={counts['foundTraces']} unique={counts['uniqueTraces']} "
        f"duplicates={counts['duplicateTraces']} invalid={counts['invalidTraces']}",
        "Each input file is a separate session; views/policies are reported separately.",
        "Publication-to-start includes enqueue/scheduling work; it is not pure OS wake latency.",
        "The steady clock is not automatically aligned to ETW/QPC timestamps.",
    ]
    for group in result["policies"]:
        policy = group["policy"]
        lines += ["", f"Session: {group['session']}  view={group['viewId'] or 'unknown'}",
                  f"DAG={policy['dependencyGraph']} native={policy['batchSubmit']} "
                  f"ownerSpin={policy['ownerSpinUs']}us workerSpin={policy['workerSpinUs']}us "
                  f"frameSpin={policy['frameSpinUs']}us spinWorkers={policy['spinWorkers']}",
                  f"Timed traces={group['traces']} disabled={group['timingDisabled']} invalid={group['invalidTraces']} "
                  f"job timing unavailable={group['jobTimingUnavailable']} "
                  f"truncated jobs/units={group['truncatedJobTraces']}/{group['truncatedUnitTraces']}"]
        for name, stats in group["metricsNs"].items():
            lines.append(f"  {name}: n={stats['count']} p50={stats['p50'] / 1000:.3f}us "
                         f"p95={stats['p95'] / 1000:.3f}us max={stats['max'] / 1000:.3f}us")
        if group["units"]:
            lines.append("  Pass/slice record-body wall (includes repeated-phase barriers; work is a static hint):")
            for row in group["units"][:top]:
                stats = row["recordBodyWallNs"]
                lines.append(f"    pass={row['passId']} {row['name']!r}: n={stats['count']} "
                             f"p50={stats['p50'] / 1000:.3f}us p95={stats['p95'] / 1000:.3f}us")
                callbacks = row["callbackTotalNs"]
                if callbacks is not None:
                    invocations = row["callbackInvocations"]["total"]
                    lines.append(f"      actual callback sum={callbacks['total'] / 1000:.3f}us "
                                 f"invocations={invocations}; per-unit sums, not individual callback percentiles")
    lines += ["", "Worker spin: one cumulative observation per frame, never a sum across views.",
              "active>0 is in flight; active=0 does not establish the final frame total.",
              "taskHits counts any shared-queue job found, not a proven recording wake avoided."]
    for frame in result["workerSpinFrames"]:
        prefix = f"  {frame['session']} frame={frame['frameId']} {frame['status']}"
        stats = frame["statistics"]
        if stats:
            lines.append(f"{prefix}: attempts={stats['attempts']} hits={stats['taskHits']} "
                         f"reserved={stats['reservedBudgetNs'] / 1000:.3f}us "
                         f"observedWall={stats['wallNs'] / 1000:.3f}us active={stats['activeSpinners']}")
        else:
            lines.append(prefix)
    if result["warnings"]:
        lines.append("")
        lines.extend(f"Warning [{row['session']} {row['path']}]: {row['message']}" for row in result["warnings"])
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path, help="JSON/NDJSON snapshots; each file is a separate session")
    parser.add_argument("--json", action="store_true", help="write the complete summary as JSON to stdout")
    parser.add_argument("--top", type=int, default=10, help="number of longest nodes/units shown (default: 10)")
    args = parser.parse_args(argv)
    if args.top < 0:
        parser.error("--top must be nonnegative")
    documents = []
    try:
        seen_paths = set()
        for path in args.files:
            session = str(path.resolve())
            if session in seen_paths:
                continue
            seen_paths.add(session)
            documents.extend(parse_documents(path.read_text(encoding="utf-8-sig"), session))
        result = analyze(documents, top=args.top)
    except (OSError, ValueError) as error:
        print(f"RECORDING_JOBS_ANALYSIS_FAILED {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2) if args.json else format_text(result, args.top))
    return 1 if any(result["counts"][name] for name in (
        "invalidTraces", "invalidSpinSnapshots", "ambiguousSpinFrames")) else 0


if __name__ == "__main__":
    raise SystemExit(main())
