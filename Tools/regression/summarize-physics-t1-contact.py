"""Summarize a completed, fresh T1 contact ABBA evidence directory."""
from pathlib import Path
import hashlib
import json
import statistics
import sys

repo = Path(__file__).resolve().parents[2]
root = Path(sys.argv[1]).resolve()
records = json.loads((root / "result.json").read_text(encoding="utf-8-sig"))
assert len(records) == 96
errors = list(root.glob("*.err"))
assert len(errors) == 96
for error_file in errors:
    assert error_file.read_text(encoding="utf-8-sig").strip() == "[profiler] shutdown abandoned=0 retained=0 foreign=0"
rows = []
for backend in ["cpu", "gpu"]:
    for workload, active in [("free", 4096), ("contact", 1024), ("contact", 4096)]:
        for profile in [False, True]:
            row = dict(backend=backend, workload=workload, active=active, profile=profile)
            for side in ["serial", "wake"]:
                values = [r["data"] for r in records if r["side"] == side and
                          r["data"]["backend"] == backend and r["data"]["active"] == active and
                          r["data"]["workload"] == workload and r["data"]["profile"] == profile]
                assert len(values) == 4
                for value in values:
                    assert value["samples"] == 240
                    if workload == "contact":
                        assert value["contactTicks"] == 240 and value["minContacts"] >= active
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
            print(backend, workload, active, "on" if profile else "off",
                  round(row["meanUsChangePercent"], 2), row["meanUsStable"],
                  round(row["p99UsChangePercent"], 2), row["p99UsStable"])

sources = ["Engine/Physics/PhysicsScene.cpp", "Engine/SceneRuntime/ScenePhysicsSimulation.cpp",
           "Tools/regression/physics_t1_benchmark.cpp", "Tools/regression/verify-physics-t1-contact.ps1",
           "Tools/regression/prepare-physics-t1-wake-benchmark.py",
           "Tools/regression/build-physics-t1-benchmark.ps1"]
result = dict(status="contact_large_workload_evaluated", productionChanged=False,
              performanceAccepted=False, root=str(root), runs=96, samplesPerSideState=4,
              warmTicks=60, measuredTicks=240, workers=records[0]["data"]["workersActual"],
              summary=rows, sources={s: hashlib.sha256((repo / s).read_bytes()).hexdigest() for s in sources},
              limitations=["Independent dynamic boxes sliding on one static floor; no dynamic body stacks",
                           "Module wall times, not complete Player or Editor frame performance",
                           "CV above 10 percent excluded separately for mean and p99"])
result["binaries"] = {s: hashlib.sha256((repo / f"Build/Obj/Phase19T1Bench/{s}/physics-t1-bench.exe").read_bytes()).hexdigest()
                      for s in ["serial", "wake"]}
(repo / "Build/Obj/Phase19T1Contact/result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print("T1_CONTACT_EVIDENCE_OK")
