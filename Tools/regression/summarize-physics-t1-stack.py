"""Summarize a completed, fresh T1 stack worker-budget ABBA evidence directory."""
from pathlib import Path
import hashlib
import json
import statistics
import sys

repo = Path(__file__).resolve().parents[2]
root = Path(sys.argv[1]).resolve()
records = json.loads((root / "result.json").read_text(encoding="utf-8-sig"))
assert len(records) >= 16
backends = sorted({r["data"]["backend"] for r in records})
active_counts = sorted({r["data"]["active"] for r in records})
worker_budgets = sorted({r["data"]["workersRequested"] for r in records}, key=lambda w: w or 256)
assert len(records) == len(backends) * len(active_counts) * len(worker_budgets) * 16
errors = list(root.glob("*.err"))
assert len(errors) == len(records)
for error_file in errors:
    assert error_file.read_text(encoding="utf-8-sig").strip() == "[profiler] shutdown abandoned=0 retained=0 foreign=0"
rows = []
for backend in backends:
    for active in active_counts:
        workload = "stack"
        for workers in worker_budgets:
            for profile in [False, True]:
                row = dict(backend=backend, workload=workload, active=active, profile=profile, workersRequested=workers)
                for side in ["serial", "wake"]:
                    values = [r["data"] for r in records if r["side"] == side and
                              r["data"]["backend"] == backend and r["data"]["active"] == active and
                              r["data"]["workload"] == workload and r["data"]["profile"] == profile and r["data"]["workersRequested"] == workers]
                    assert len(values) == 4
                    for value in values:
                        assert value["samples"] == 240
                        if workload == "stack":
                            assert value["contactTicks"] == value["dynamicContactTicks"] == 240 and value["minContacts"] >= active
                            assert value["minDynamicPairs"] >= active // 2 and value["minMeanHeight"] >= 1.5
                        if profile:
                            costs = value["profileCosts"]
                            assert costs["unmatchedTasks"] == costs["hierarchyViolations"] == 0
                            assert costs["sdkTasks"] == costs["workerTasks"] + costs["inlineTasks"]
                            for name in ["Physics.FetchWait", "Physics.FetchResults", "Physics.DispatcherDrain",
                                         "Physics.SnapshotStatistics", "Physics.RenderPrepare", "Physics.RenderMerge"]:
                                assert costs["inclusivePerTick"][name]["ticks"] == 240
                    row[side] = {}
                    for metric in ["meanUs", "p99Us"]:
                        samples = [v[metric] for v in values]
                        row[side][metric] = statistics.median(samples)
                        row[side][metric + "Cv"] = statistics.stdev(samples) / statistics.mean(samples)
                for metric in ["meanUs", "p99Us"]:
                    row[metric + "ChangePercent"] = (row["wake"][metric] / row["serial"][metric] - 1) * 100
                    row[metric + "Stable"] = max(row[s][metric + "Cv"] for s in ["serial", "wake"]) <= .1
                rows.append(row)
                print(backend, workload, active, workers, "on" if profile else "off",
                      round(row["meanUsChangePercent"], 2), row["meanUsStable"],
                      round(row["p99UsChangePercent"], 2), row["p99UsStable"])

sources = ["Engine/Physics/PhysicsScene.cpp", "Engine/SceneRuntime/ScenePhysicsSimulation.cpp",
           "Tools/regression/physics_t1_benchmark.cpp", "Tools/regression/verify-physics-t1-stack.ps1",
           "Tools/regression/prepare-physics-t1-wake-benchmark.py",
           "Tools/regression/build-physics-t1-benchmark.ps1"]
result = dict(status="stack_worker_budget_evaluated", productionChanged=False,
              performanceAccepted=False, root=str(root), runs=len(records), samplesPerSideState=4,
              warmTicks=60, measuredTicks=240, workerBudgets=worker_budgets,
              summary=rows, sources={s: hashlib.sha256((repo / s).read_bytes()).hexdigest() for s in sources},
              limitations=["Four-box stacks with rotation locked and Z translation locked, X/Y free; not unconstrained stack stability or product frame performance",
                           "Module wall times, not complete Player or Editor frame performance",
                           "CV above 10 percent excluded separately for mean and p99"])
result["binaries"] = {s: hashlib.sha256((repo / f"Build/Obj/Phase19T1Bench/{s}/physics-t1-bench.exe").read_bytes()).hexdigest()
                      for s in ["serial", "wake"]}
(Path(sys.argv[2]) if len(sys.argv) > 2 else repo / "Build/Obj/Phase19T1Stack/result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print("T1_STACK_EVIDENCE_OK")
