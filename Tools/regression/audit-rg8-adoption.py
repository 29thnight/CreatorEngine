"""Strict RG8 evidence audit. This script does not establish performance adoption.

Required workload-evidence.json (schemaVersion=2, binary=head/exe/runtime) has
workloads with kind positive-overlap or negative-fallback, stdout path, and frames
containing backendGeneration/frameId/viewId/submissionId/captureGeneration.
Each workload also references runBinary, validationBinary, captureBinary (exact
head/exe/runtime binary.json files), nativeEvidence (execution-result.json),
validationEvidence (GPU-validation result.json), and pixelComparison (exact-output
comparison.json). Paths are relative to the evidence manifest. Native execution
coverage alone is not calibrated GPU overlap evidence. Missing evidence fails.
"""
import argparse
import json
import math
import re
import statistics
from pathlib import Path
import numpy as np


def check(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def distribution(values):
    values = sorted(values)
    check(values and all(math.isfinite(v) for v in values), "Missing/nonfinite distribution")
    return {"count": len(values), "median": statistics.median(values),
            "p95": values[math.ceil(.95 * len(values)) - 1],
            "minimum": values[0], "maximum": values[-1]}


def same_binary(actual, expected):
    check(all(actual.get(k) and actual[k] == expected[k] for k in ("head", "exe", "runtime")),
          "Evidence must identify the exact source and binaries")


def inputs(manifest):
    draws = []
    for draw in manifest["draws"]:
        row = {k: draw.get(k) for k in ("route", "modelId", "meshId", "modelGeneration", "world", "pose")}
        row["material"] = {k: draw.get("lattice", {}).get(k)
                           for k in ("features", "coverage", "uniformBytes", "textures")}
        draws.append(row)
    return {k: manifest.get(k) for k in ("width", "height", "camera", "lights", "captureMode",
                                        "sampleIndex", "historyPolicy", "ibl", "viewFlags")} | {
        "draws": sorted(draws, key=lambda row: json.dumps(row, sort_keys=True)),
        "skybox": Path(manifest["skyBoxPath"]).name}


def identity(row):
    return tuple(row[k] for k in ("backendGeneration", "frameId", "viewId", "submissionId"))


def log_rows(log, tag):
    return [json.loads(line.split(f"[rg8.{tag}] ", 1)[1]) for line in log.splitlines()
            if f"[rg8.{tag}] {{" in line]


def unique_rows(rows):
    result = {}
    for row in rows:
        key = identity(row)
        # Repeated collection of one token must not count as extra evidence.
        check(key not in result, f"Duplicate submission evidence: {key}")
        result[key] = row
    return result


def verify_schedule(diagnostics, mode, graph=None):
    check(diagnostics["schemaVersion"] == 2, "Stale schedule evidence schema")
    execution, schedule = diagnostics["execution"], diagnostics["schedule"]
    batches, waits = diagnostics["batches"], diagnostics["waits"]
    check(execution["completed"] and execution["submissionAttempted"] and not execution["recoveryRequired"],
          "Incomplete/failed queue execution")
    check(batches and execution["plannedBatches"] == execution["submittedBatches"] == len(batches),
          "Actual batch accounting mismatch")
    check(all(b["submitted"] and b["queue"] in (0, 1) for b in batches), "Unsubmitted/unknown queue batch")
    compute = sum(b["queue"] == 1 for b in batches)
    check(compute == execution["plannedComputeBatches"] == execution["computeBatches"], "Compute batch mismatch")
    check(schedule["usesCompute"] == (compute > 0), "Schedule/execution placement mismatch")
    check(mode == 2 or compute == 0, "Compute submitted outside requested mode 2")
    check(not schedule["predictionCalibrated"] and (compute == 0 or schedule["predictionComplete"]),
          "Compute placement requires complete costs; model calibration remains unproven")
    check(execution["plannedWaits"] == execution["submittedWaits"] == len(waits), "Wait accounting mismatch")
    check(execution["plannedBarriers"] == sum(b["barriers"] for b in batches), "Barrier accounting mismatch")
    check(execution["prologueBarriers"] == batches[0]["barriers"] and
          execution["epilogueBarriers"] == batches[-1]["barriers"], "Boundary barrier mismatch")
    entries = schedule["entries"]
    passes = [p for b in batches for p in b["passes"]]
    check(passes == [e["pass"] for e in entries] and len(passes) == len(set(passes)), "Pass order/identity mismatch")
    assignment = {p: (i, b["queue"]) for i, b in enumerate(batches) for p in b["passes"]}
    check(all(assignment[e["pass"]][1] == e["queue"] for e in entries), "Pass queue mismatch")
    wait_pairs = set()
    for wait in waits:
        producer, consumer = wait["producerBatch"], wait["consumerBatch"]
        check(wait["submitted"] and 0 <= producer < consumer < len(batches), "Invalid/unsubmitted wait")
        check(batches[producer]["queue"] != batches[consumer]["queue"], "Same-queue wait in cross-queue contract")
        check((producer, consumer) not in wait_pairs, "Duplicate queue wait")
        wait_pairs.add((producer, consumer))
    for edge in schedule["waits"]:
        producer, consumer = assignment[edge["producer"]], assignment[edge["consumer"]]
        check(producer[0] < consumer[0] and producer[1] != consumer[1] and
              (producer[0], consumer[0]) in wait_pairs, "Missing planned dependency wait")
    if graph is not None:
        check(set(passes) == {p["authoredIndex"] for p in graph["passes"] if not p["culled"]},
              "Executed passes differ from compiled graph")
    costs = [execution[k] for k in ("scheduleMilliseconds", "recordingMilliseconds", "submissionMilliseconds")]
    check(all(math.isfinite(v) and v >= 0 for v in costs) and
          math.isfinite(execution["totalMilliseconds"]) and
          execution["totalMilliseconds"] + 1e-5 >= sum(costs), "Invalid CPU measurement scopes")
    return execution


def interval_union(intervals):
    result = []
    for begin, end in sorted(intervals):
        check(end >= begin > 0, "Invalid calibrated GPU interval")
        if result and begin <= result[-1][1]:
            result[-1] = (result[-1][0], max(end, result[-1][1]))
        else:
            result.append((begin, end))
    return result


def verify_timing(timing, diagnostics=None):
    check(timing["schemaVersion"] == 2 and timing["clockValid"] and timing["cpuTicksPerSecond"] > 0,
          "Calibrated queue clocks missing")
    check(timing["queryOverflow"] == timing["droppedSlices"] == 0, "Dropped/overflowed timing slices")
    check(all(math.isfinite(timing[k]) and timing[k] >= 0 for k in
              ("queueSpanMilliseconds", "busyMilliseconds", "measuredOverlapMilliseconds", "overlapClockErrorMilliseconds")),
          "Nonfinite/negative GPU timing")
    slices = timing["slices"]
    check(slices and len(slices) == timing["sliceCount"] and
          sum(s["queue"] == 1 for s in slices) == timing["computeSliceCount"], "Raw slice accounting mismatch")
    check(all(s["queue"] in (0, 1) for s in slices), "Unknown queue interval")
    check(timing["graphicsCalibrationSamples"] > 0 and
          (timing["computeSliceCount"] == 0 or timing["computeCalibrationSamples"] > 0), "Uncalibrated queue")
    if diagnostics is not None:
        entries = {(e["pass"], e["queue"]) for e in diagnostics["schedule"]["entries"]}
        check({(s["passIndex"], s["queue"]) for s in slices} == entries,
              "Measured pass identities/queues differ from actual schedule")
    lanes = [interval_union((s["beginCpuTick"], s["endCpuTick"]) for s in slices if s["queue"] == q)
             for q in (0, 1)]
    overlap = sum(max(0, min(g[1], c[1]) - max(g[0], c[0])) for g in lanes[0] for c in lanes[1])
    to_ms = 1000 / timing["cpuTicksPerSecond"]
    check(math.isclose(overlap * to_ms, timing["measuredOverlapMilliseconds"], rel_tol=1e-8, abs_tol=1e-8),
          "Overlap is not calibrated interval intersection")
    union = interval_union((s["beginCpuTick"], s["endCpuTick"]) for s in slices)
    span, busy = (union[-1][1] - union[0][0]) * to_ms, sum(e - b for b, e in union) * to_ms
    check(span > 0 and busy <= span + 1e-5 and timing["overlapClockErrorMilliseconds"] >= 0,
          "Invalid GPU span/clock uncertainty")
    # Queue-relative single-queue durations may differ by calibration slope rounding.
    check(math.isclose(span, timing["queueSpanMilliseconds"], rel_tol=.001, abs_tol=.001) and
          math.isclose(busy, timing["busyMilliseconds"], rel_tol=.001, abs_tol=.001), "GPU union/span mismatch")
    check(min(s["beginCpuTick"] for s in slices) >= timing["cpuSubmitTick"], "GPU interval precedes submission")
    return overlap * to_ms


def verify_native(path, binary):
    native = read(path)
    same_binary(native["binary"], binary)
    check(native["schemaVersion"] == 2 and native["passed"] and native["fullRegression"] and
          native["nativeQueueExecutionTested"] and native["frameRetirementTested"] and
          native["positiveOverlapExecutionTested"] and native["negativeFallbackExecutionTested"] and
          native["declarationOrderTested"] and native["readReadOwnershipTested"] and
          native["reorderedFailureTested"] and native["exitCode"] == 0,
          "Current full native overlap-path, retirement and fallback coverage required")
    return native


def verify_validation(result):
    validation = result["validation"] if "validation" in result else result
    check(validation["layerEnabled"] and validation["mode"] == "gpu" and
          validation["problems"] == validation["droppedMessages"] == 0, "Missing/failed GPU validation")


def verify_workloads(path, binary):
    evidence = read(path)
    check(evidence["schemaVersion"] == 2, "Stale real-workload evidence")
    same_binary(evidence["binary"], binary)
    kinds = set()
    summaries = []
    for workload in evidence["workloads"]:
        kind = workload["kind"]
        check(kind in ("positive-overlap", "negative-fallback") and kind not in kinds, "Duplicate/unknown workload")
        kinds.add(kind)
        resolve = lambda key: path.parent / workload[key]
        same_binary(read(resolve("runBinary")), binary)
        same_binary(read(resolve("validationBinary")), binary)
        same_binary(read(resolve("captureBinary")), binary)
        verify_native(resolve("nativeEvidence"), binary)
        validation = read(resolve("validationEvidence"))
        verify_validation(validation)
        comparison = read(resolve("pixelComparison"))
        check(comparison["passed"] and comparison["attachments"] and
              all(a["nonfinite"] == 0 and a["maxAbs"] == 0 for a in comparison["attachments"]),
              "Real workload requires exact pixel/output equivalence")
        log = resolve("stdout").read_text(encoding="utf-8-sig", errors="replace")
        executions, timings = unique_rows(log_rows(log, "execution")), unique_rows(log_rows(log, "timing"))
        check(workload["frames"] and len({identity(f) for f in workload["frames"]}) == len(workload["frames"]),
              "Workload requires distinct measured frames")
        for diagnostics in executions.values():
            verify_schedule(diagnostics, diagnostics["mode"])
        overlaps = []
        for frame in workload["frames"]:
            key = identity(frame)
            diagnostics, timing = executions[key], timings[key]
            check(diagnostics["mode"] == timing["mode"] == 2 and
                  diagnostics["captureGeneration"] == timing["captureGeneration"] == frame["captureGeneration"] == 0,
                  "Workload proof must use ordinary mode-2 submissions")
            execution = verify_schedule(diagnostics, 2)
            check(diagnostics["schedule"]["predictionComplete"], "Warm complete-cost workload required; missing history is not negative-fallback proof")
            overlap = verify_timing(timing, diagnostics)
            if kind == "positive-overlap":
                check(execution["computeBatches"] > 0 and overlap > timing["overlapClockErrorMilliseconds"],
                      "Positive real workload has no overlap above measured clock drift")
            else:
                check(execution["computeBatches"] == timing["computeSliceCount"] == overlap == 0,
                      "Negative workload did not select graphics-only fallback")
            overlaps.append(overlap)
        verify_counters(log, list(executions.values()))
        summaries.append({"kind": kind, "measuredOverlapMilliseconds": distribution(overlaps)})
    check(kinds == {"positive-overlap", "negative-fallback"}, "Both real positive-overlap and negative-fallback workloads required")
    return summaries


def verify_counters(log, executions):
    executions = [e for e in executions if e["mode"] != 0]
    pools = [tuple(map(int, r)) for r in re.findall(r"\[rg8.pool\] created=(\d+) reused=(\d+) leased=(\d+) cached=(\d+)", log)]
    totals = [tuple(map(int, r)) for r in re.findall(r"\[rg8.live\] submissions=(\d+) batches=(\d+) profilerQueue=graphics", log)]
    barriers = [tuple(map(int, r)) for r in re.findall(r"\[rg8.barriers\] total=(\d+) last=(\d+)", log)]
    check(executions and pools and totals and barriers, "Missing schedule/pool/shutdown accounting")
    check(all(leased == cached == 0 for created, reused, leased, cached in pools),
          "Shutdown must release all recording leases and cached storage")
    created, reused = sum(p[0] for p in pools), sum(p[1] for p in pools)
    batch_count = sum(e["execution"]["submittedBatches"] for e in executions)
    check(created > 0 and reused > 0 and created + reused == batch_count == sum(t[1] for t in totals),
          "Pool leases do not match actual submitted batch list")
    check(len(executions) == sum(t[0] for t in totals) and
          sum(e["execution"]["plannedBarriers"] for e in executions) == sum(b[0] for b in barriers),
          "Aggregate graph/barrier counters do not match per-submission evidence")
    compute = sum(e["execution"]["computeBatches"] for e in executions)
    check(compute == sum(map(int, re.findall(r"\[rg8.compute\] submissions=(\d+)", log))), "Compute counter mismatch")
    schedules = [tuple(map(int, r)) for r in re.findall(
        r"\[rg8.schedule\] placement=overlap computeSubmittedFrames=(\d+) measuredOverlapSubmissions=(\d+) predictedGainNs=\d+ predictionCalibrated=false", log)]
    check(schedules and sum(r[0] for r in schedules) == sum(e["execution"]["computeBatches"] > 0 for e in executions),
          "Compute-submission counter is not actual placement")
    return {"createdPairs": created, "reusedPairs": reused, "graphs": len(executions), "batches": batch_count,
            "computeBatches": compute, "plannedBarriers": sum(b[0] for b in barriers)}


def load_run(directory, mode):
    log_path = directory / "stdout.log"
    if not log_path.exists():
        log_path = directory / "editor.stdout.log"
    log = log_path.read_text(encoding="utf-8-sig", errors="replace")
    executions = unique_rows(log_rows(log, "execution"))
    timings = unique_rows(log_rows(log, "timing"))
    cpus = unique_rows(log_rows(log, "cpu"))
    check(timings and cpus, "Missing opt-in per-submission timing/CPU evidence")
    check(all(row["mode"] == mode for row in list(executions.values()) + list(timings.values()) + list(cpus.values())),
          "Requested mode differs from actual execution evidence")
    for row in executions.values():
        verify_schedule(row, mode)
    for key, timing in timings.items():
        check(mode == 0 or key in executions, "Timing has no matching actual schedule")
        verify_timing(timing, executions.get(key))
    pool = verify_counters(log, list(executions.values())) if mode else None
    check(mode != 0 or not executions, "Mode 0 unexpectedly used queue executor")
    return executions, timings, cpus, pool


def verify_capture(manifest, mode):
    measurement = manifest["measurement"]
    check(measurement["gpuStatus"] == "measured" and measurement["clockValid"] and
          measurement["queryOverflow"] == measurement["droppedSlices"] == 0, "Invalid capture timing")
    check(measurement["cpuRecordSubmitMs"] >= measurement["cpuSubmitMs"] >= 0, "Incomplete CPU scope")
    diagnostics = manifest["compiledGraph"]["queueExecution"]
    if mode:
        execution = verify_schedule(diagnostics, mode, manifest["compiledGraph"])
        check(manifest["graph"]["barriers"] == execution["plannedBarriers"], "Capture barrier count mismatch")
        check((measurement["computeSliceCount"] > 0) == (execution["computeBatches"] > 0),
              "Capture slices differ from actual queue placement")
    else:
        check(measurement["computeSliceCount"] == 0, "Legacy path emitted compute slices")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path, nargs="?")
    parser.add_argument("normal", type=Path, nargs="?")
    parser.add_argument("--native-evidence", type=Path)
    parser.add_argument("--validation-evidence", type=Path)
    parser.add_argument("--workload-evidence", type=Path)
    parser.add_argument("--verify-workloads", action="store_true")
    parser.add_argument("--verify-live-evidence", type=Path)
    parser.add_argument("--mode", type=int, choices=(0, 1, 2))
    parser.add_argument("--binary", type=Path)
    # Compatibility switches no longer weaken the default mandatory accounting.
    parser.add_argument("--require-pooling", action="store_true")
    parser.add_argument("--require-barriers", action="store_true")
    args = parser.parse_args()
    if args.verify_workloads:
        check(args.workload_evidence and args.binary, "Workload manifest and binary identity required")
        print(json.dumps(verify_workloads(args.workload_evidence, read(args.binary)), indent=2))
        return
    if args.verify_live_evidence:
        check(args.mode is not None, "Live mode required")
        executions, timings, cpus, pool = load_run(args.verify_live_evidence, args.mode)
        result = read(args.verify_live_evidence / "result.json")
        verify_validation(result)
        for capture in result["captures"]:
            verify_capture(read(Path(capture["path"]) / "manifest.json"), args.mode)
        print(json.dumps({"scheduleEvidencePassed": True, "pool": pool, "adoptionEstablished": False}))
        return
    check(args.captures and args.normal and args.native_evidence and args.validation_evidence and args.workload_evidence,
          "Strict audit requires captures, normal, native, validation, and real workload evidence")
    binary = read(args.normal / "binary.json")
    same_binary(read(args.captures / "binary.json"), binary)
    verify_native(args.native_evidence / "execution-result.json", binary)
    same_binary(read(args.validation_evidence / "binary.json"), binary)
    validation_runs = read(args.validation_evidence / "runs.json")
    check({run["mode"] for run in validation_runs} == {1, 2}, "GPU validation required for owned graphics and compute-requested modes")
    for run in validation_runs:
        check(run["gpuValidation"] and run["exitCode"] == 0, "Failed validation process")
        verify_validation(read(Path(run["path"]) / "validation.json"))
    workloads = verify_workloads(args.workload_evidence, binary)
    capture_runs, normal_runs = read(args.captures / "runs.json"), read(args.normal / "runs.json")
    expected = {(order, mode) for order in ("Forward", "Reverse") for mode in (0, 1, 2)}
    check(len(capture_runs) == len(normal_runs) == 6 and
          {(r["order"], r["mode"]) for r in capture_runs} == expected and
          {(r["order"], r["mode"]) for r in normal_runs} == expected, "Incomplete forward/reverse mode matrix")
    check(all(not r["gpuValidation"] for r in capture_runs + normal_runs), "Validation runs cannot establish timing")
    baseline_dir = args.captures / "Forward-0/sample-0"
    baseline = read(baseline_dir / "manifest.json")
    references = {a["name"]: (a, np.fromfile(baseline_dir / a["file"], dtype="<f4")) for a in baseline["attachments"]}
    check(references and all(np.isfinite(a).all() for _, a in references.values()), "Invalid reference pixels")
    comparisons, diagnostic = 0, []
    for run in capture_runs:
        check(run["exitCode"] == 0 and run["samples"] >= 4, "Missing successful diagnostic captures")
        _, capture_timings, _, _ = load_run(Path(run["path"]), run["mode"])
        costs = []
        for index in range(run["samples"]):
            sample = Path(run["path"]) / f"sample-{index}"
            manifest = read(sample / "manifest.json")
            check(inputs(manifest) == inputs(baseline), f"Input mismatch: {sample}")
            verify_capture(manifest, run["mode"])
            measured = [t for t in capture_timings.values() if t["frameId"] == manifest["frameId"] and
                        t["viewId"] == manifest["viewId"] and t["captureGeneration"] > 0]
            check(len(measured) == 1 and measured[0]["computeSliceCount"] == manifest["measurement"]["computeSliceCount"],
                  "Capture does not match its canonical submission timing")
            check({a["name"] for a in manifest["attachments"]} == references.keys(), "Missing/extra attachment")
            for attachment in manifest["attachments"]:
                description, reference = references[attachment["name"]]
                check(all(attachment[k] == description[k] for k in ("channels", "width", "height", "encoding")), "Attachment format mismatch")
                array = np.fromfile(sample / attachment["file"], dtype="<f4")
                check(array.size == reference.size and np.isfinite(array).all() and np.array_equal(array, reference),
                      f"Pixel mismatch: {sample}/{attachment['name']}")
                comparisons += 1
            costs.append(manifest["measurement"]["cpuRecordSubmitMs"])
        diagnostic.append({"order": run["order"], "mode": run["mode"], "captureCpuRecordSubmitMs": distribution(costs)})
    normal, metrics = [], {}
    for run in normal_runs:
        check(run["exitCode"] == 0 and run["timingOnly"], "Failed ordinary-frame run")
        directory = Path(run["path"])
        rows = read(directory / "normal-frames.json")
        check(len(rows) == 32 and len({(r["gpu"]["frame"], r["gpu"]["submission"]) for r in rows}) == 32, "Expected 32 unique normal submissions")
        executions, timings, cpus, pool = load_run(directory, run["mode"])
        cpu_costs, schedule_costs, overlap_costs = [], [], []
        for row in rows:
            gpu = row["gpu"]
            matching = [key for key, t in timings.items() if t["frameId"] == gpu["frame"] and t["viewId"] == gpu["viewId"] and t["submissionId"] == gpu["submission"]]
            check(len(matching) == 1, "Normal sample lacks unique submission evidence")
            key = matching[0]
            check(timings[key]["captureGeneration"] == 0 and key in cpus, "Capture/CPU mismatch in normal sample")
            check(gpu["viewId"] == baseline["viewId"] and gpu["clockValid"] and gpu["queueSpanMs"] > 0 and
                  gpu["busyMs"] <= gpu["queueSpanMs"] + 1e-5 and
                  gpu["droppedTotal"] == gpu["queryOverflowPasses"] == gpu["spanViolations"] == 0 and
                  not any(p["name"].startswith("PBR.") for p in gpu["passes"]), "Invalid ordinary GPU measurement")
            check((row["display"]["width"], row["display"]["height"]) == (baseline["width"], baseline["height"]), "Normal dimensions differ")
            cpu = cpus[key]
            check(math.isfinite(cpu["totalMilliseconds"]) and cpu["totalMilliseconds"] >= cpu["submissionMilliseconds"] >= 0, "Invalid full record/submit CPU timing")
            cpu_costs.append(cpu["totalMilliseconds"])
            schedule_costs.append(executions[key]["execution"]["scheduleMilliseconds"] if run["mode"] else 0)
            overlap_costs.append(timings[key]["measuredOverlapMilliseconds"])
        memory = [json.loads(line) for line in (directory / "memory-continuous.jsonl").read_text(encoding="utf-8-sig").splitlines()]
        check(memory and all(r["valid"] and r["budgetBytes"] > 0 for r in memory), "Invalid memory sampling")
        for stream in {r["stream"] for r in memory}:
            series = [r for r in memory if r["stream"] == stream]
            check(len(series) >= 2 and series[0]["kind"] == "start" and series[-1]["kind"] == "end" and
                  max(b["elapsedMs"] - a["elapsedMs"] for a, b in zip(series, series[1:])) <= 1000, "Incomplete memory lifecycle samples")
        window = [r for r in memory if rows[0]["utcMs"] <= r["utcMs"] <= rows[-1]["utcMs"]]
        check(len(window) >= 20, "Insufficient memory window samples")
        summary = {"order": run["order"], "mode": run["mode"], "recordingPool": pool,
                   "gpuQueueSpanMs": distribution([r["gpu"]["queueSpanMs"] for r in rows]),
                   "gpuBusyMs": distribution([r["gpu"]["busyMs"] for r in rows]),
                   "cpuRecordSubmitMs": distribution(cpu_costs), "cpuScheduleMs": distribution(schedule_costs),
                   "measuredOverlapMs": distribution(overlap_costs),
                   "normalWindowUsagePeakBytes": max(r["usedBytes"] for r in window),
                   "lifecycleSampledUsagePeakBytes": max(r["usedBytes"] for r in memory)}
        normal.append(summary)
        metrics[(run["order"], run["mode"])] = summary
    differences = []
    for order in ("Forward", "Reverse"):
        for before, after, purpose in ((0, 1, "owned-executor-overhead"), (1, 2, "compute-placement-delta")):
            differences.append({"order": order, "comparison": f"{before}-to-{after}", "purpose": purpose,
                "medianGpuDeltaMs": metrics[(order, after)]["gpuQueueSpanMs"]["median"] - metrics[(order, before)]["gpuQueueSpanMs"]["median"],
                "medianCpuDeltaMs": metrics[(order, after)]["cpuRecordSubmitMs"]["median"] - metrics[(order, before)]["cpuRecordSubmitMs"]["median"]})
    result = {"passed": True, "evidenceValid": True, "schemaVersion": 2, "phaseComplete": False,
              "adoptionEstablished": False, "performanceValidated": False,
              "performanceClaim": "none; process-separated distributions and uncalibrated cost constants do not establish speedup",
              "binary": binary, "captureComparisons": comparisons, "captureMaxAbs": 0, "normalFrames": 192,
              "realWorkloads": workloads, "normal": normal, "diagnostic": diagnostic, "comparisons": differences,
              "cpuMeasurementCaveat": "Full CPU scope includes opt-in evidence serialization; executor phase timers exclude adapter log serialization",
              "memoryScope": "DXGI sampled budget usage, not exact residency or retained-resource peak"}
    (args.normal / "adoption-measurements.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
