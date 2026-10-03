"""Reanalyze preserved captures without rerunning or changing the product."""
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
name = sys.argv[1] if len(sys.argv) > 1 else "ManagedProfileBridgeCostsClean"
source = repo / "Build/Obj/Phase19T2QueryBench" / name / "result.json"
receipt = json.loads(source.read_text(encoding="utf-8-sig"))
out = source.parent / "SdkTail"
out.mkdir(exist_ok=True)
exe = repo / "Build/Obj/Phase19T2QueryBench/CaptureProbe/capture-probe.exe"
fields = ("validate", "prepare", "update", "ray", "overlap", "sdkResidual", "translate", "commit", "residual")
all_rows = []
captures = []
for index, run in enumerate(receipt["receipts"]):
    if run["profile"] != "on":
        continue
    capture = Path(run["evidence"]) / "query.ceprof"
    raw = out / f"capture-{index}.csv"
    probe = subprocess.run([str(exe), str(capture), str(raw)], capture_output=True, text=True, check=True)
    gate = json.loads(probe.stdout)
    assert gate["complete"] and not any(gate[k] for k in ("unacked", "dropped", "droppedCounters", "hierarchyViolations"))
    rows = list(csv.DictReader(raw.open(encoding="utf-8")))
    for row in rows:
        row.update({k: float(v) for k, v in row.items()})
        assert all(math.isfinite(v) and v >= 0 for v in row.values())
        assert abs(sum(row[k] for k in fields) - row["total"]) < 1e-6
        row["captureIndex"] = index
    all_rows.extend(rows)
    captures.append({"path": str(capture), "sha256": hashlib.sha256(capture.read_bytes()).hexdigest(), "gate": gate, "raw": str(raw)})
assert len(captures) == 2

def summarize(rows):
    return {k: statistics.mean(r[k] for r in rows) for k in ("total", "sdk", *fields)}

results = []
for count in (16, 64):
    rows = [r for r in all_rows if r["requests"] == count]
    assert len(rows) >= 5280
    ordered = sorted(rows, key=lambda r: r["total"])
    p99 = ordered[math.ceil(len(rows) * .99) - 1]["total"]
    median = statistics.median(r["total"] for r in rows)
    tail = [r for r in rows if r["total"] >= p99]
    above3 = [r for r in rows if r["total"] > median * 3]
    results.append({"requests": count, "samples": len(rows), "medianUs": median, "p99Us": p99,
                    "maxUs": ordered[-1]["total"], "mean": summarize(rows),
                    "tailCount": len(tail), "tailMean": summarize(tail),
                    "above3MedianCount": len(above3), "largestCalls": ordered[-10:][::-1]})
result = {"status": "sdk_tail_capture_analysis_validated", "sourceReceipt": str(source), "captures": captures,
          "rows": results, "performanceAccepted": False,
          "scope": "Profiler-on complete calls pooled across warmup/timed/parity; p99 nearest rank; tails retained, not removed.",
          "limits": ["Scope wall time includes profiler overhead and possible descheduling.",
                     "SDK residual includes query dispatch/control and gaps between child markers.",
                     "Native capture spans are not paired to managed rawUs by query ID; no claim that their tails are the same calls.",
                     "Stage contributions describe observed tails, not causal CPU/cache/allocator attribution."]}
result["analyzerHashes"] = {str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
    (Path(__file__), repo / "Tools/regression/physics_managed_query_capture_probe.cpp", exe)}
(out / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
for row in results:
    print(json.dumps({k: v for k, v in row.items() if k != "largestCalls"}, ensure_ascii=False))
