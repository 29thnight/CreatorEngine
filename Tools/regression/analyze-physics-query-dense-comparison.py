"""Fresh profiler-off before/after comparison with unchanged CV acceptance."""
import hashlib
import json
from pathlib import Path
import statistics
import sys

repo = Path(__file__).resolve().parents[2]
root = repo / "Build/Obj/Phase19T2QueryBench" / (sys.argv[1] if len(sys.argv) > 1 else "ManagedDenseComparison")
p = root / "result.json"
r = json.loads(p.read_text(encoding="utf-8-sig"))
expected = [("before","off"),("after","off"),("after","off"),("before","off")]
if not r.get("offOnly"):
    expected += [("after","on"),("after","on")]
assert [(x["revision"], x["profile"]) for x in r["receipts"]] == expected
rows = [dict(row, revision=run["revision"]) for run in r["receipts"] if run["profile"] == "off" for row in run["rows"]]
groups = {}
for revision in ("before", "after"):
    for count in (16,64):
        for mode in ("scalar", "batch"):
            group = {"revision": revision, "requests": count, "mode": mode}
            for metric in ("meanUs", "p99Us"):
                values = [row[metric] for row in rows if (row["revision"], row["requests"], row["mode"]) == (revision,count,mode)]
                assert len(values) == 8
                cv = statistics.stdev(values) / statistics.mean(values) * 100
                group[metric] = {"median": statistics.median(values), "cvPercent": cv, "stable": cv <= 10, "values": values}
            groups[revision,count,mode] = group
comparisons = []
for count in (16,64):
    result = {"requests": count}
    for metric in ("meanUs", "p99Us"):
        before = groups["before",count,"batch"][metric]
        after = groups["after",count,"batch"][metric]
        controlBefore = groups["before",count,"scalar"][metric]
        controlAfter = groups["after",count,"scalar"][metric]
        result[metric] = {"batchChangePercent": (after["median"]/before["median"]-1)*100,
                          "comparisonStable": before["stable"] and after["stable"],
                          "scalarControlChangePercent": (controlAfter["median"]/controlBefore["median"]-1)*100,
                          "scalarControlStable": controlBefore["stable"] and controlAfter["stable"]}
    comparisons.append(result)
r["freshOffGroups"] = list(groups.values())
r["freshOffComparisons"] = comparisons
r["stabilityPolicy"] = "CV <= 10 percent per metric; retain all eight blocks per revision/count/mode; no trimming"
r["performanceAccepted"] = False
r["analyzerSha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
r["stageRuntimeHashes"] = {}
for revision in ("before", "after"):
    stage = Path(r[revision + "Stage"])
    binaries = list(stage.rglob("Player.runtime.dll"))
    assert len(binaries) == 1
    r["stageRuntimeHashes"][revision] = {"path": str(binaries[0]), "sha256": hashlib.sha256(binaries[0].read_bytes()).hexdigest()}
p.write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding="utf-8")
after = root / "After"
after.mkdir(exist_ok=True)
afterReceipt = json.loads((root / "after-result.json").read_text(encoding="utf-8-sig"))
if r.get("offOnly"):
    afterReceipt["status"] = "managed_query_off_validated"
    afterReceipt["bridgeCostsRequired"] = False
    afterReceipt["scope"] = "Off-only subset; no new on captures or on/off overhead comparison"
(after / "result.json").write_text(json.dumps(afterReceipt,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(comparisons,indent=2))
