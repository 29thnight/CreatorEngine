"""Audit ordinary-frame queue timing separately from diagnostic capture readbacks."""
import argparse
import json
import re
from pathlib import Path
import statistics
import numpy as np


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def distribution(values):
    values = sorted(values)
    return {"count": len(values), "median": statistics.median(values),
            "p95": values[int(np.ceil(.95 * len(values))) - 1],
            "minimum": values[0], "maximum": values[-1]}


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


def former_common_requests(graph):
    """Model the previous per-pass COMMON executor, not a historical GPU timing."""
    count = sum(int(r["initialState"] != 0) + int(r["finalState"] != 0)
                for r in graph["resources"] if r["used"])
    for p in graph["passes"]:
        if p["culled"]:
            continue
        states = {}
        phases = p["phases"] or [{"usages": p["usages"]}]
        for _ in range(p["repeatCount"]):
            for phase in phases:
                for usage in phase["usages"]:
                    resource, target = usage["resource"], usage["state"]
                    count += int(states.get(resource, 0) != target or target == 7)
                    states[resource] = target
        for usage in p["usages"]:
            resource = usage["resource"]
            count += int(states.get(resource, 0) != 0)
            states[resource] = 0
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path)
    parser.add_argument("normal", type=Path)
    parser.add_argument("--require-pooling", action="store_true")
    parser.add_argument("--require-barriers", action="store_true")
    args = parser.parse_args()
    capture_runs = read(args.captures / "runs.json")
    normal_runs = read(args.normal / "runs.json")
    assert len(capture_runs) == len(normal_runs) == 6
    assert all(not run.get("gpuValidation", False) for run in capture_runs + normal_runs)
    assert read(args.captures / "binary.json") == read(args.normal / "binary.json")
    baseline_dir = args.captures / "Forward-0/sample-0"
    baseline = read(baseline_dir / "manifest.json")
    baseline_inputs = inputs(baseline)
    references = {a["name"]: np.fromfile(baseline_dir / a["file"], dtype="<f4") for a in baseline["attachments"]}
    assert all(np.isfinite(a).all() for a in references.values())
    comparisons = []
    summaries = []
    for run in capture_runs:
        assert run["exitCode"] == 0 and run["samples"] == 8
        directory = Path(run["path"])
        records = []
        barrier_counts = []
        former_counts = []
        for index in range(run["samples"]):
            sample = directory / f"sample-{index}"
            manifest = read(sample / "manifest.json")
            assert inputs(manifest) == baseline_inputs, f"Input mismatch: {sample}"
            measurement = manifest["measurement"]
            if args.require_barriers and run["mode"] != 0:
                graph = manifest["compiledGraph"]
                # Pass snapshots omit the two separate queue boundary recordings.
                boundary = sum(int(r["initialState"] != 0) + int(r["finalState"] != 0)
                               for r in graph["resources"] if r["used"]) if run["mode"] == 2 else 0
                planned = boundary
                for p in graph["passes"]:
                    if p["culled"]:
                        continue
                    planned += len(p["barriers"])
                    planned += sum(len(phase["firstBarriers"]) +
                                   (p["repeatCount"] - 1) * len(phase["repeatBarriers"])
                                   for phase in p["phases"])
                assert manifest["graph"]["barriers"] == planned
                former = former_common_requests(graph)
                assert planned < former
                barrier_counts.append(planned)
                former_counts.append(former)
            assert measurement["gpuStatus"] == "measured" and measurement["queryOverflow"] == measurement["droppedSlices"] == 0
            assert measurement["computeSliceCount"] == (2 if run["mode"] == 2 else 0)
            for attachment in manifest["attachments"]:
                array = np.fromfile(sample / attachment["file"], dtype="<f4")
                reference = references[attachment["name"]]
                assert array.size == reference.size and np.isfinite(array).all()
                maximum = float(np.max(np.abs(array - reference)))
                assert maximum == 0, f"Pixel mismatch: {sample}/{attachment['name']}: {maximum}"
                comparisons.append({"order": run["order"], "mode": run["mode"], "sample": index,
                                    "attachment": attachment["name"], "maxAbs": maximum})
            records.append(measurement["cpuRecordMs"])
        summaries.append({"order": run["order"], "mode": run["mode"],
                          "queueBarrierCounts": sorted(set(barrier_counts)),
                          "modeledFormerCommonCounts": sorted(set(former_counts)),
                          "diagnosticCaptureCpuRecordMs": distribution(records)})
    normal_summary = []
    for run in normal_runs:
        assert run["exitCode"] == 0 and run["timingOnly"]
        directory = Path(run["path"])
        rows = read(directory / "normal-frames.json")
        assert len(rows) == 32 and len({row["gpu"]["frame"] for row in rows}) == 32
        for row in rows:
            gpu = row["gpu"]
            assert gpu["viewId"] == baseline["viewId"]
            assert gpu["sliceCount"] == sum(not p["name"].startswith("PBR.")
                                            for p in baseline["compiledGraph"]["passes"] if not p["culled"])
            assert gpu["clockValid"] and gpu["queueSpanMs"] > 0 and gpu["busyMs"] <= gpu["queueSpanMs"] + 1e-5
            assert gpu["droppedTotal"] == gpu["queryOverflowPasses"] == gpu["spanViolations"] == 0
            assert not any(p["name"].startswith("PBR.") for p in gpu["passes"])
            assert (row["display"]["width"], row["display"]["height"]) == (baseline["width"], baseline["height"])
        memory = [json.loads(line) for line in (directory / "memory-continuous.jsonl").read_text(encoding="utf-8-sig").splitlines()]
        assert memory and all(row["valid"] and row["budgetBytes"] > 0 for row in memory)
        for stream in {row["stream"] for row in memory}:
            series = [row for row in memory if row["stream"] == stream]
            assert series[0]["kind"] == "start" and series[-1]["kind"] == "end"
            assert max(b["elapsedMs"] - a["elapsedMs"] for a, b in zip(series, series[1:])) <= 1000
        window = [row for row in memory if rows[0]["utcMs"] <= row["utcMs"] <= rows[-1]["utcMs"]]
        assert len(window) >= 20
        log = (directory / "stdout.log").read_text(encoding="utf-8-sig", errors="replace")
        assert ("[rg8.compute] submissions=" in log) == (run["mode"] == 2)
        pooling = None
        barriers = None
        if args.require_barriers and run["mode"] != 0:
            barrier_rows = [tuple(map(int, row)) for row in re.findall(
                r"\[rg8.barriers\] total=(\d+) last=(\d+)", log)]
            assert barrier_rows
            total, last = max(barrier_rows)
            assert total >= last > 0
            barriers = {"totalPlannedAndRecorded": total, "lastGraph": last,
                        "scope": "includes submission boundaries and repeated phases"}
        if args.require_pooling and run["mode"] != 0:
            pool_rows = [tuple(map(int, row)) for row in re.findall(
                r"\[rg8.pool\] created=(\d+) reused=(\d+) leased=(\d+) cached=(\d+)", log)]
            batch_rows = [tuple(map(int, row)) for row in re.findall(
                r"\[rg8.live\] submissions=(\d+) batches=(\d+) profilerQueue=graphics", log)]
            assert pool_rows and batch_rows
            created, reused, leased, cached = max(pool_rows)
            submissions, batches = max(batch_rows)
            assert created > 0 and reused > created and leased == 0
            compute_batches = max([int(n) for n in re.findall(r"\[rg8.compute\] submissions=(\d+)", log)], default=0)
            assert batches == submissions * 3 + compute_batches * 2
            assert created + reused == batches
            pooling = {"createdPairs": created, "reusedPairs": reused, "leasedAtShutdown": leased,
                       "cachedAtShutdown": cached, "graphs": submissions, "batches": batches,
                       "computeBatches": compute_batches, "batchesPerGraph": batches / submissions}
        normal_summary.append({"order": run["order"], "mode": run["mode"],
                               "recordingPool": pooling,
                               "queueBarriers": barriers,
                               "gpuQueueSpanMs": distribution([row["gpu"]["queueSpanMs"] for row in rows]),
                               "gpuBusyMs": distribution([row["gpu"]["busyMs"] for row in rows]),
                               "normalWindowUsagePeakBytes": max(row["usedBytes"] for row in window),
                               "lifecycleSampledUsagePeakBytes": max(row["usedBytes"] for row in memory),
                               "memorySamples": len(memory), "normalWindowMemorySamples": len(window)})
    result = {"passed": True, "binary": read(args.normal / "binary.json"),
              "width": baseline["width"], "height": baseline["height"], "draws": len(baseline["draws"]),
              "captureComparisons": len(comparisons), "captureMaxAbs": 0,
              "normalFrames": 192, "normal": normal_summary, "diagnostic": summaries,
              "memoryScope": "DXGI sampled device-budget usage, not exact physical residency or per-resource committed bytes"}
    (args.normal / "adoption-measurements.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
