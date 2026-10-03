"""Summarize complete batch bridge scopes from lossless Player captures."""
import json
from pathlib import Path
import sys

name = sys.argv[1] if len(sys.argv) > 1 else "ManagedProfileBridgeCostsClean"
root = Path(__file__).resolve().parents[2] / "Build/Obj/Phase19T2QueryBench" / name
receipt = json.loads((root / "result.json").read_text(encoding="utf-8-sig"))
if not receipt.get("bridgeCostsRequired"):
    raise SystemExit("Bridge coverage gate was not required")

fields = ("validate", "prepare", "sdk", "translate", "commit", "residual")
rows = []
for count in (16, 64):
    captures = []
    for run in receipt["receipts"]:
        if run["profile"] != "on":
            continue
        capture = run["capture"]
        if any(capture[key] for key in ("unacked", "dropped", "droppedCounters", "hierarchyViolations")):
            raise SystemExit("Incomplete or lossy capture")
        cost = next(row for row in capture["bridgeCosts"] if row["requests"] == count)
        captures.append({"evidence": run["evidence"], **cost})
    if len(captures) != 2:
        raise SystemExit("Expected two on captures")
    samples = sum(row["samples"] for row in captures)
    total = sum(row["totalMeanUs"] * row["samples"] for row in captures) / samples
    stages = {}
    for field in fields:
        mean = sum(row[field + "MeanUs"] * row["samples"] for row in captures) / samples
        stages[field] = {"meanUs": mean, "sharePercent": 100 * mean / total}
    rows.append({"requests": count, "samples": samples, "totalMeanUs": total,
                 "stages": stages, "captures": captures})

receipt["bridgeCostDiagnostic"] = {
    "rows": rows,
    "scope": "Profiler-on complete bridge calls; pooled warmup, timed and parity calls with exactly 16 or 64 executed requests.",
    "limits": [
        "Preparation includes scratch/result allocation, request conversion, reserve and answer allocation.",
        "Translation includes translated-output allocation, body/shape resolution and registry conversion.",
        "SDK is inclusive Physics.QueryBatch: structure update, query execution and their profiler overhead.",
        "Residual includes cleanup, control flow and scope overhead; it is not an allocator-only measurement.",
        "No causal attribution of CPU-cycle variance and no profiler-off speedup inferred from stage shares."
    ],
    "performanceAccepted": False
}
(root / "result.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps(receipt["bridgeCostDiagnostic"], ensure_ascii=False, indent=2))
