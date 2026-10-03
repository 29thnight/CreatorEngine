"""Validate and decompose translation costs from preserved Player spans."""
import csv
import hashlib
import json
from pathlib import Path
import statistics
import sys

repo = Path(__file__).resolve().parents[2]
name = sys.argv[1] if len(sys.argv) > 1 else "ManagedProfileHitCostsClean"
root = repo / "Build/Obj/Phase19T2QueryBench" / name / "SdkTail"
receipt = json.loads((root / "result.json").read_text(encoding="utf-8"))
rows = []
for capture in receipt["captures"]:
    for row in csv.DictReader(open(capture["raw"], encoding="utf-8")):
        row = {key: float(value) for key, value in row.items()}
        assert row["initialized"] == 1 and row["lookups"] == row["encodes"] and row["lookups"] > 0
        assert abs(row["translate"] - sum(row[k] for k in ("initialize", "lookup", "encode", "translateResidual"))) < 1e-6
        assert row["translateResidual"] >= -1e-6
        rows.append(row)

results = []
for count in (16, 64):
    group = [r for r in rows if r["requests"] == count]
    assert len(group) >= 5280
    threshold = next(r["p99Us"] for r in receipt["rows"] if r["requests"] == count)
    tail = [r for r in group if r["total"] >= threshold]
    def summary(values):
        return {k: statistics.mean(r[k] for r in values) for k in
                ("total", "translate", "initialize", "lookup", "encode", "translateResidual", "lookups")}
    results.append({"requests": count, "samples": len(group), "mean": summary(group), "tailCount": len(tail), "tailMean": summary(tail)})
result = {"status": "translation_cost_decomposition_validated", "rows": results,
          "source": str(root / "result.json"), "analyzerSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "performanceAccepted": False,
          "limits": ["Profiler-on pooled warmup/timed/parity calls.",
                     "Per-hit lookup and encode markers add measurement overhead; not directly comparable to earlier captures.",
                     "Initialize includes allocation and value initialization; encode includes registry registration and hit construction.",
                     "Residual includes iteration, summaries and marker gaps; tails do not establish causal allocator/cache/descheduling attribution."]}
(root / "hit-result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps(result, ensure_ascii=False, indent=2))
