"""Validate publication-policy ABBA runs and summarize owner wall observations."""
from pathlib import Path
import hashlib
import json
import statistics
import sys

repo = Path(__file__).resolve().parents[2]
records = []
roots = [Path(v).resolve() for v in sys.argv[1:4]]
for root, count in zip(roots, [96, 64, 8]):
    values = json.loads((root / "result.json").read_text(encoding="utf-8-sig"))
    assert len(values) == count
    for value in values:
        d = value["data"]
        assert d["samples"] == 240
        if d["workload"] == "small":
            assert d["bodies"] == d["active"] == 16
        if d["profile"]:
            c = d["profileCosts"]
            assert c["unmatchedTasks"] == c["unmatchedCompletions"] == c["hierarchyViolations"] == 0
            assert c["completionTasks"] == c["sdkTasks"] == c["workerTasks"] + c["inlineTasks"]
            for name in ["Physics.FetchWait", "Physics.FetchResults", "Physics.DispatcherDrain",
                         "Physics.SnapshotStatistics", "Physics.RenderPrepare", "Physics.RenderMerge"]:
                assert c["inclusivePerTick"][name]["ticks"] == 240
    records.extend(values)

rows = []
for root in roots:
    rs = json.loads((root / "result.json").read_text(encoding="utf-8-sig"))
    keys = sorted({(v["data"]["backend"], v["data"]["active"], v["data"]["workersRequested"], v["data"]["profile"]) for v in rs})
    for backend, active, workers, recording in keys:
        row = dict(root=str(root), backend=backend, active=active, workersRequested=workers,
                   profile=recording, workload=rs[0]["data"]["workload"])
        for side in ["serial", "profile"]:
            ds = [v["data"] for v in rs if v["side"] == side and v["data"]["backend"] == backend and
                  v["data"]["active"] == active and v["data"]["workersRequested"] == workers and v["data"]["profile"] == recording]
            assert len(ds) == 4
            row[side] = {}
            for metric in ["meanUs", "p99Us"]:
                samples = [d[metric] for d in ds]
                row[side][metric] = statistics.median(samples)
                row[side][metric + "Cv"] = statistics.stdev(samples) / statistics.mean(samples)
        for metric in ["meanUs", "p99Us"]:
            row[metric + "ChangePercent"] = (row["profile"][metric] / row["serial"][metric] - 1) * 100
            row[metric + "Stable"] = max(row[s][metric + "Cv"] for s in ["serial", "profile"]) <= .1
        rows.append(row)

result = dict(status="publication_policy_applied_module_validated", productionChanged=True,
              moduleEvidenceAccepted=True, fullProductPerformanceAccepted=False, runs=168,
              profileRuns=sum(v["data"]["profile"] for v in records), roots=list(map(str, roots)), summary=rows)
result["policy"] = "Release work queue mutex for publication only while recording and SDK work remains; active batch stays counted until publication finishes."
result["limitations"] = ["Standalone owner wall time, not complete product frame performance",
                           "CV above 10 percent excluded per metric; stable CV is not statistical significance",
                           "GPU off16 p99 +8.66 percent first, -4.76 percent repeat; no persistent regression conclusion",
                           "Benchmark capture budget512MiB only; product profiler budget unchanged"]
for key, prefix, expected in [("nativeT1", "Phase19T1Profile", [8161,8161,3530,8161]),
                              ("nativeT0", "Phase19T0", [173,173,170,173])]:
    result[key] = {}
    for config, checks in zip(["Debug","Release","Shipping","ASan"], expected):
        path = repo / f"Build/Obj/{prefix}/{config}/result.jsonl"
        value = json.loads(path.read_text(encoding="utf-8-sig"))
        assert value["checks"] == checks and value["gpu_verified"]
        result[key][config] = dict(path=str(path), result=value)
result["player"] = {}
for label in ["DT1Profile", "DT1Profile-shear", "RT1Profile", "ST1Profile"]:
    path = repo / f"Build/Obj/P19B2/{label}/result.json"
    value = json.loads(path.read_text(encoding="utf-8-sig"))["result"]
    assert value["immutable"] and value["result"] == "PHYSICS_B2_PLAYER_OK"
    if "shear" in label:
        assert value["exitCode"] == 3
    else:
        assert value["exitCode"] == 0 and value["completedGameDisplay"]
        assert sum(p["passed"] for p in value["probes"]) == 27 and sum(p["failed"] for p in value["probes"]) == 0
    result["player"][label] = dict(path=str(path), result=value)
if len(sys.argv) > 4:
    editor = Path(sys.argv[4]).resolve()
    value = json.loads((editor / "result.json").read_text(encoding="utf-8-sig"))
    assert value["result"] == "PHYSICS_B2_EDITOR_OK" and value["restoredEntities"] == 6 and value["playCycles"] == 3
    result["editor"] = dict(path=str(editor / "result.json"), result=value)
isolation = repo / "Build/Obj/P19B2/t1-profile-isolation.log"
text = isolation.read_text(encoding="utf-8-sig")
assert "Player Shipping 격리 통과" in text
result["shippingIsolation"] = str(isolation)
result["sources"] = {path.relative_to(repo).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in
                     [repo/"Engine/Physics/PhysicsScene.cpp", repo/"Engine/SceneRuntime/ScenePhysicsSimulation.cpp",
                      repo/"Tools/regression/physics_t1_benchmark.cpp", repo/"Tools/regression/prepare-physics-t1-profile-benchmark.py",
                      repo/"Tools/regression/verify-physics-t1-profile-small.ps1", Path(__file__).resolve()]}
(repo/"Build/Obj/Phase19T1Profile/result.json").write_text(json.dumps(result,indent=2),encoding="utf-8")
print("T1_PUBLICATION_FINAL_EVIDENCE_OK")
