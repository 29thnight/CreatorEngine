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
COMPLETION_PATH_AXIS = "observed-latest-predecessor-completion-not-cpu-critical-path"
DEPENDENCY_REASONS = {"same-target", "cpu-producer"}
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
    "graphCompleteToOwnerReturn": "Graph callback teardown to owner return; includes observer/notification work.",
    "lastNodeCompleteToGraphComplete": "Last native completion gate to graph callback teardown; includes native drain.",
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
    if trace["schemaVersion"] == 1:
        result = {name: unsigned(policy.get(name), f"policy.{name}")
                  for name in ("ownerSpinUs", "workerSpinUs", "frameSpinUs", "spinWorkers")}
    else:
        if set(policy) != {"ownerWaitMode"} or policy.get("ownerWaitMode") != "blocking":
            raise ValueError("schema 2 policy must contain only ownerWaitMode='blocking'")
        if "ownerWait" in trace or "workerSpin" in trace:
            raise ValueError("schema 2 must not contain removed ownerWait/workerSpin sections")
        result = dict(ownerWaitMode="blocking")
    result["schemaVersion"] = trace["schemaVersion"]
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
                topologyAvailableTraces=0, topologyUnavailableTraces=0, topologyTruncatedTraces=0,
                invalidTopologyTraces=0, lastCompletionUnavailableTraces=0,
                dispatcherHandoffs=0, nativeTasks=0, _samples=defaultdict(list),
                _units=defaultdict(list), _nodeRows=[], _unitRows=[])


def read_id_list(value, label):
    if not isinstance(value, list):
        raise ValueError(f"{label} must be an array")
    ids = [unsigned(identity, label) for identity in value]
    if len(set(ids)) != len(ids):
        raise ValueError(f"{label} contains duplicate identities")
    return ids


def analyze_completion_path(item, group, result, entry, valid_nodes, valid_units, unit_errors):
    """완료시각으로 관측한 선행 경로다. 실제 CPU 실행량이나 ETW Ready 경로를 추정하지 않는다."""
    if item["trace"]["schemaVersion"] == 1:
        return
    trace = item["trace"]
    path = entry["completionPath"]
    observation = dict(status="unavailable", lastCompletedNodes=None, lastCompleteNs=None)
    entry["completionObservation"] = observation
    try:
        jobs = trace.get("jobs")
        if not isinstance(jobs, dict):
            raise ValueError("schema 2 jobs must be an object")
        available = boolean(jobs.get("available"), "jobs.available")
        topology = boolean(jobs.get("topologyAvailable"), "jobs.topologyAvailable")
        truncated = boolean(jobs.get("topologyTruncated"), "jobs.topologyTruncated")
        last_ids = read_id_list(jobs.get("lastCompletedNodes"), "jobs.lastCompletedNodes")
        if topology and (truncated or trace["unitTimingsTruncated"] or trace["jobTimingsTruncated"]):
            raise ValueError("available topology must not have truncated nodes, units, or topology")
        group["topologyAvailableTraces" if topology else "topologyUnavailableTraces"] += 1
        group["topologyTruncatedTraces"] += truncated
        path.update(topologyAvailable=topology, topologyTruncated=truncated)
        status = jobs.get("topologyStatus")
        if isinstance(status, str):
            path["reportedTopologyStatus"] = status
        units = trace.get("units")
        if not isinstance(units, list):
            raise ValueError("schema 2 units must be an array")
        for index, unit in enumerate(units):
            if not isinstance(unit, dict) or unsigned(unit.get("unit"), "unit.unit") != index:
                raise ValueError("schema 2 unit IDs must match their sequential array positions")
            if not topology and "node" in unit:
                raise ValueError("unit.node must be absent when topology is unavailable")
        if not available:
            if topology or last_ids:
                raise ValueError("unavailable jobs cannot claim topology or last completed nodes")
            observation["status"] = "unavailable-for-this-path"
            group["lastCompletionUnavailableTraces"] += 1
            path["status"] = "topology-truncated" if truncated else "unavailable-for-this-path"
            return
        if entry.get("jobsStatus") == "invalid":
            raise ValueError("valid graph lifetime is required to verify last completion")
        task_count = unsigned(jobs.get("nativeTasks"), "jobs.nativeTasks")
        if any(identity >= task_count for identity in last_ids):
            raise ValueError("jobs.lastCompletedNodes has an identity outside nativeTasks")
        if len(valid_nodes) != task_count:
            if trace["jobTimingsTruncated"] and not topology:
                observation["status"] = "node-timings-truncated"
                group["lastCompletionUnavailableTraces"] += 1
                path["status"] = "topology-truncated" if truncated else "node-timings-truncated"
                return
            raise ValueError("complete valid native node timings are required to verify last completion")
        completed = unsigned(jobs.get("completedNs"), "jobs.completedNs", positive=True)
        latest = max((row["times"]["completeNs"] for row in valid_nodes.values()), default=None)
        expected_last = sorted(identity for identity, row in valid_nodes.items()
                               if row["times"]["completeNs"] == latest)
        if sorted(last_ids) != expected_last:
            raise ValueError("jobs.lastCompletedNodes must list every node tied at the maximum completeNs")
        observation.update(status="available" if task_count else "empty-graph",
                           lastCompletedNodes=expected_last, lastCompleteNs=latest)
        if latest is not None:
            observation["lastNodeCompleteToGraphCompleteNs"] = completed - latest
            add_metric(group, "lastNodeCompleteToGraphComplete", completed - latest)
        if not topology:
            for row in valid_nodes.values():
                if "dependencies" in row["source"] or "units" in row["source"]:
                    raise ValueError("node topology fields must be absent when topology is unavailable")
            path["status"] = "topology-truncated" if truncated else "unavailable-for-this-path"
            return
        if not trace["dependencyGraph"] or not trace["batchSubmit"]:
            raise ValueError("native batched dependency graph is required for available topology")
        if task_count > 4096 or len(units) > 4096:
            raise ValueError("available topology exceeds the node/unit diagnostic cap")
        dependencies = {}
        previous_targets = {}
        assigned_units = {}
        edge_count = 0
        for identity in range(task_count):
            row = valid_nodes[identity]
            node = row["source"]
            target = unsigned(node.get("target"), f"node[{identity}].target")
            wave = unsigned(node.get("wave"), f"node[{identity}].wave")
            raw_edges = node.get("dependencies")
            if not isinstance(raw_edges, list):
                raise ValueError(f"node[{identity}].dependencies must be an array")
            edges = {}
            for edge in raw_edges:
                if not isinstance(edge, dict):
                    raise ValueError(f"node[{identity}] dependency must be an object")
                predecessor = unsigned(edge.get("node"), f"node[{identity}].dependency.node")
                if predecessor >= identity or predecessor in edges:
                    raise ValueError(f"node[{identity}] dependencies must be unique strictly earlier IDs")
                reasons = edge.get("reasons")
                if not isinstance(reasons, list) or not reasons or any(
                        not isinstance(reason, str) or reason not in DEPENDENCY_REASONS for reason in reasons):
                    raise ValueError(f"node[{identity}] dependency has an empty or unsupported reason")
                if len(set(reasons)) != len(reasons):
                    raise ValueError(f"node[{identity}] dependency has duplicate reasons")
                producer = valid_nodes[predecessor]
                if "same-target" in reasons and producer["source"].get("target") != target:
                    raise ValueError(f"node[{identity}] same-target dependency crosses targets")
                if producer["times"]["completeNs"] > row["times"]["readyNs"]:
                    raise ValueError(f"node[{identity}] became ready before a predecessor completed")
                edges[predecessor] = reasons
            if list(edges) != sorted(edges):
                raise ValueError(f"node[{identity}] dependencies must be sorted by node ID")
            previous = previous_targets.get(target)
            if previous is not None and "same-target" not in edges.get(previous, []):
                raise ValueError(f"node[{identity}] lacks its preceding same-target dependency")
            previous_targets[target] = identity
            dependencies[identity] = edges
            edge_count += len(edges)
            if edge_count > 16384:
                raise ValueError("available topology exceeds the dependency edge diagnostic cap")
            unit_ids = read_id_list(node.get("units"), f"node[{identity}].units")
            for unit_id in unit_ids:
                if unit_id >= len(units) or unit_id in assigned_units:
                    raise ValueError(f"node[{identity}] unit mapping is duplicate or outside the unit array")
                unit = units[unit_id]
                if unsigned(unit.get("node"), f"unit[{unit_id}].node") != identity:
                    raise ValueError(f"unit[{unit_id}] reverse node mapping does not match")
                if any(unsigned(unit.get(key), f"unit[{unit_id}].{key}") != value
                       for key, value in (("target", target), ("wave", wave))):
                    raise ValueError(f"unit[{unit_id}] target/wave differs from its mapped node")
                assigned_units[unit_id] = identity
        if len(assigned_units) != len(units):
            raise ValueError("every unit must map to exactly one native node")
        if unit_errors:
            raise ValueError("invalid unit timings prevent complete observed path reconstruction")
        if not task_count:
            path.update(status="empty-graph", nodeIds=[], steps=[], lastCompletedNodes=[])
            return
        # 동률 전체를 공개하되 대표 경로만 가장 작은 ID로 결정한다. 최장 CPU 경로라고 부르지 않는다.
        selected = min(expected_last)
        identity = selected
        steps = []
        ties = []
        while True:
            row = valid_nodes[identity]
            predecessors = dependencies[identity]
            predecessor_end = max((valid_nodes[key]["times"]["completeNs"] for key in predecessors), default=None)
            candidates = sorted(key for key in predecessors
                                if valid_nodes[key]["times"]["completeNs"] == predecessor_end)
            predecessor = min(candidates) if candidates else None
            if len(candidates) > 1:
                ties.append(dict(node=identity, predecessors=candidates))
            unit_rows = []
            for unit_id in row["source"]["units"]:
                unit = units[unit_id]
                measured = valid_units.get(unit_id)
                unit_rows.append(dict(unit=unit_id, passId=unit.get("pass"), name=unit.get("name"),
                    slice=unit.get("slice"), sliceCount=unit.get("sliceCount"),
                    recordBodyWallNs=measured["recordBodyWallNs"] if measured is not None else None,
                    callbackTotalNs=measured["callbackTotalNs"] if measured is not None else None,
                    callbackInvocations=measured["callbackInvocations"] if measured is not None else None))
            steps.append(dict(node=identity, target=row["source"]["target"], wave=row["source"]["wave"],
                selectedPredecessor=predecessor, latestPredecessorCandidates=candidates,
                selectedPredecessorReasons=predecessors[predecessor] if predecessor is not None else [],
                readyAfterLatestPredecessorNs=(row["times"]["readyNs"] - predecessor_end
                                               if predecessor_end is not None else None),
                timestampsNs=row["times"], durationsNs=row["durations"], skipped=row["skipped"],
                executeEnkiThread=row["source"].get("executeEnkiThread"),
                executeOsThread=row["source"].get("executeOsThread"), units=unit_rows))
            if predecessor is None:
                break
            identity = predecessor
        steps.reverse()
        path.update(status="available", lastCompletedNodes=expected_last, selectedLastNode=selected,
                    nodeIds=[step["node"] for step in steps], steps=steps,
                    hasTies=len(expected_last) > 1 or bool(ties), predecessorTies=ties,
                    tieSelection="smallest node ID; all local tied candidates are reported")
    except ValueError as error:
        group["invalidTopologyTraces"] += 1
        path.update(status="invalid", nodeIds=None, steps=None)
        add_warning(result, item, str(error))


def analyze_trace(item, group, result):
    trace = item["trace"]
    entry = dict(session=item["session"], path=item["path"], viewId=item["viewId"],
                 frameId=trace["frameId"], dispatchBeginNs=trace["dispatchBeginNs"],
                 traceSchemaVersion=trace["schemaVersion"],
                 completionPath=dict(status="unsupported-schema-1" if trace["schemaVersion"] == 1 else "unavailable",
                                     axis=COMPLETION_PATH_AXIS, nodeIds=None, steps=None))
    enabled = boolean(trace.get("timingEnabled"), "timingEnabled")
    if not enabled:
        group["timingDisabled"] += 1
        entry["status"] = "timing-disabled"
        if trace["schemaVersion"] == 2:
            entry["completionPath"]["status"] = "timing-disabled"
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
    if trace["schemaVersion"] == 1:
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
    valid_nodes = {}
    valid_units = {}
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
                    valid_nodes[identity] = dict(source=node, times=times, durations=durations, skipped=skipped)
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
        if trace["schemaVersion"] == 2:
            entry["completionPath"]["status"] = "invalid-unit-timings"
        return entry
    if not isinstance(units, list):
        add_warning(result, item, "units must be an array")
        group["invalidUnits"] += 1
        if trace["schemaVersion"] == 2:
            entry["completionPath"]["status"] = "invalid-unit-timings"
        return entry
    unit_errors_before = group["invalidUnits"] + group["invalidCallbackTimings"]
    identity_fields = ("pass", "target", "wave", "slice", "sliceCount")
    unit_counts = Counter(tuple(unit[key] for key in identity_fields) for unit in units
                          if isinstance(unit, dict) and all(type(unit.get(key)) is int for key in identity_fields))
    for unit_index, unit in enumerate(units):
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
            if trace["schemaVersion"] == 2 and isinstance(jobs, dict) and jobs.get("topologyAvailable") is True:
                unit_node = unsigned(unit.get("node"), "unit.node")
                mapped = valid_nodes.get(unit_node)
                if mapped is None:
                    raise ValueError("unit node mapping has no valid native node timing")
                containing_node = (mapped["times"]["startNs"], mapped["times"]["bodyEndNs"], mapped["skipped"])
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
            if trace["schemaVersion"] == 2:
                row["unit"] = unit.get("unit")
                row["node"] = unit.get("node")
            add_metric(group, "unitRecordBody", wall)
            group["_unitRows"].append(row)
            group["_units"][(identity[0], name)].append(row)
            valid_units[unit_index] = row
        except ValueError as error:
            group["invalidUnits"] += 1
            add_warning(result, item, str(error))
    unit_errors = group["invalidUnits"] + group["invalidCallbackTimings"] != unit_errors_before
    analyze_completion_path(item, group, result, entry, valid_nodes, valid_units, unit_errors)
    return entry


def analyze(documents, top=10):
    """documents는 (session 식별자, JSON 값)의 반복자다. CLI는 각 파일을 별도 session으로 둔다."""
    result = dict(schemaVersion=2, acceptedTraceSchemas=[1, 2], axis=AXIS, clock=CLOCK, durationUnit="nanoseconds",
                  percentileMethod="linear interpolation on sorted durations",
                  sessionRule="Each input file is a separate session; repeated queries within it are deduplicated.",
                  unitsAxis=UNIT_AXIS, completionPathAxis=COMPLETION_PATH_AXIS,
                  metricDescriptions=METRIC_DESCRIPTIONS,
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
                if type(trace.get("schemaVersion")) is not int or trace.get("schemaVersion") not in (1, 2):
                    raise ValueError("unsupported schedulingTrace.schemaVersion")
                if trace.get("clock") != CLOCK or trace.get("axis") != AXIS:
                    raise ValueError("unsupported recording clock or timing axis")
                frame = unsigned(trace.get("frameId"), "frameId")
                dispatch = unsigned(trace.get("dispatchBeginNs"), "dispatchBeginNs")
                enabled = boolean(trace.get("timingEnabled"), "timingEnabled")
                policy = read_policy(trace)
                key = (str(session), frame, dispatch, view)
                fingerprint_data = {key: value for key, value in trace.items()
                                    if trace["schemaVersion"] != 1 or key != "workerSpin"}
                if not enabled and trace["schemaVersion"] == 1:
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
        legacy_spin_count = len(item["snapshots"]) if item["trace"]["schemaVersion"] == 1 else 0
        if item["conflict"]:
            result["counts"]["invalidTraces"] += 1
            group["invalidTraces"] += 1
            invalid_sessions[item["session"]] += 1
            result["counts"]["quarantinedSpinSnapshots"] += legacy_spin_count
            result["traces"].append(dict(session=item["session"], path=item["path"],
                                         status="conflicting-duplicate", frameId=item["trace"]["frameId"]))
            continue
        error_fields = ("invalidNodes", "invalidUnits", "invalidCallbackTimings", "invalidOwnerWaits",
                        "invalidJobSections", "invalidTopologyTraces")
        errors_before = sum(group[name] for name in error_fields)
        try:
            entry = analyze_trace(item, group, result)
            if sum(group[name] for name in error_fields) != errors_before:
                result["counts"]["invalidTraces"] += 1
                group["invalidTraces"] += 1
                invalid_sessions[item["session"]] += 1
                result["counts"]["quarantinedSpinSnapshots"] += legacy_spin_count
                entry["status"] = "partial-invalid"
            elif item["trace"]["schemaVersion"] == 1:
                for snapshot in item["snapshots"]:
                    spin = read_worker_snapshot(snapshot, item, item["policy"], result)
                    if spin is not None:
                        spin_snapshots.append(spin)
            result["traces"].append(entry)
        except ValueError as error:
            result["counts"]["invalidTraces"] += 1
            group["invalidTraces"] += 1
            invalid_sessions[item["session"]] += 1
            result["counts"]["quarantinedSpinSnapshots"] += legacy_spin_count
            add_warning(result, item, str(error))
            result["traces"].append(dict(session=item["session"], path=item["path"],
                                         status="invalid", frameId=item["trace"]["frameId"]))
    for group in groups.values():
        group["sessionHasInvalidTraces"] = invalid_sessions[group["session"]] != 0
        group["coverageComplete"] = bool(group["traces"]) and not group["sessionHasInvalidTraces"] and not any(
            group[name] for name in (
            "timingDisabled", "jobTimingUnavailable", "truncatedJobTraces", "truncatedUnitTraces",
            "invalidNodes", "unavailableUnits", "invalidUnits", "callbackTimingUnavailableUnits",
            "ownerWaitUnavailable", "invalidOwnerWaits", "invalidCallbackTimings", "invalidJobSections",
            "invalidTopologyTraces"))
        group["completionPathCoverageComplete"] = (
            group["policy"]["schemaVersion"] == 2 and group["coverageComplete"] and not any(
                group[name] for name in ("topologyUnavailableTraces", "topologyTruncatedTraces",
                                        "lastCompletionUnavailableTraces")))
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
        policy_label = (f"Schema={policy['schemaVersion']} DAG={policy['dependencyGraph']} "
                        f"native={policy['batchSubmit']}")
        if policy["schemaVersion"] == 1:
            policy_label += (f" ownerSpin={policy['ownerSpinUs']}us workerSpin={policy['workerSpinUs']}us"
                             f" frameSpin={policy['frameSpinUs']}us spinWorkers={policy['spinWorkers']}")
        else:
            policy_label += " ownerWait=blocking"
        lines += ["", f"Session: {group['session']}  view={group['viewId'] or 'unknown'}",
                  policy_label,
                  f"Timed traces={group['traces']} disabled={group['timingDisabled']} invalid={group['invalidTraces']} "
                  f"job timing unavailable={group['jobTimingUnavailable']} "
                  f"truncated jobs/units={group['truncatedJobTraces']}/{group['truncatedUnitTraces']}"]
        if policy["schemaVersion"] == 2:
            lines.append(f"Topology: available={group['topologyAvailableTraces']} "
                         f"unavailable={group['topologyUnavailableTraces']} "
                         f"truncated={group['topologyTruncatedTraces']} invalid={group['invalidTopologyTraces']}")
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
    paths = [entry for entry in result["traces"] if entry.get("traceSchemaVersion") == 2]
    if paths:
        lines += ["", "Observed completion paths: last node, then latest-completing predecessors.",
                  "A representative tied path is selected by smallest node ID; JSON reports all local ties.",
                  "This is not an exclusive CPU-time critical path, ETW Ready time, or a proven OS wake delay."]
        for entry in paths[:top]:
            path = entry["completionPath"]
            prefix = f"  {entry['session']} frame={entry['frameId']} view={entry['viewId'] or 'unknown'}"
            if path["status"] == "available":
                node_ids = path["nodeIds"][:top]
                suffix = f" ({len(path['nodeIds'])} nodes total)" if len(node_ids) < len(path["nodeIds"]) else ""
                lines.append(f"{prefix}: last={path['lastCompletedNodes']} path={node_ids}{suffix} "
                             f"ties={path['hasTies']}")
            else:
                lines.append(f"{prefix}: {path['status']}; no complete path inferred")
    if any(group["policy"]["schemaVersion"] == 1 for group in result["policies"]):
        lines += ["", "Legacy schema 1 worker spin: one cumulative observation per frame, never a sum across views.",
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
