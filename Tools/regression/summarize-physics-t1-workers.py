"""Summarize repeated worker measurements without treating noisy runs as wins."""
import argparse
import json
import statistics
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("inputs", nargs="+", type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
groups = {}
records = []
for path in args.inputs:
    rows = json.loads(path.read_text(encoding="utf-8-sig"))
    for row in rows:
        data = row["data"]
        if data["samples"] != 240:
            raise ValueError("Incomplete measurement window")
        if data["profile"]:
            costs = data["profileCosts"]
            for key in ("unmatchedTasks", "unmatchedCompletions", "hierarchyViolations"):
                if costs[key]:
                    raise ValueError(f"Invalid task correlation: {key}")
        key = (data["workload"], data["backend"], data["active"], data["profile"], data["workersRequested"])
        groups.setdefault(key, []).append(data)
        records.append(row)

summary = []
for key, rows in sorted(groups.items()):
    means = [row["meanUs"] for row in rows]
    cv = statistics.pstdev(means) / statistics.mean(means) * 100
    summary.append(dict(zip(("workload", "backend", "active", "profile", "workersRequested"), key)) |
                   dict(runs=len(rows), meanMedianUs=statistics.median(means),
                        p99MedianUs=statistics.median(row["p99Us"] for row in rows),
                        meanCvPercent=cv, stable=cv <= 10,
                        workersActual=rows[0]["workersActual"]))
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(dict(result="T1_WORKER_MEASUREMENTS_ACCEPTED", runs=len(records),
                                     inputs=[str(path) for path in args.inputs], groups=summary,
                                     scope="Standalone 240-tick workload; profiler-on costs are not product latency"),
                                indent=2) + "\n", encoding="utf-8")
print(f"T1_WORKER_SUMMARY_OK runs={len(records)} groups={len(summary)}")
