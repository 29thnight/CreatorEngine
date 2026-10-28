"""Summarize paired Player captures without discarding low-cost script markers."""
import argparse
import importlib.util
import json
import statistics
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path)
parser.add_argument("--max-p95-increase-us", type=float)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location("profile_reader", Path(__file__).with_name("summarize-material-profile.py"))
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)
rows = []
for capture in sorted(args.root.glob("*-*/frames.ceprof")):
    report = reader.analyze(capture)
    if not report["nativeRealPerformanceGateEligible"] or report["frames"] < 32:
        raise ValueError(f"Incomplete native-real capture: {capture}")
    markers = {}
    for name in ("Script.FixedTick", "RenderThreadFrame"):
        matches = [row for row in report["markers"] if row["name"] == name and not row["gpu"]]
        if len(matches) != 1:
            raise ValueError(f"Expected exactly one {name} marker: {capture}")
        markers[name] = matches[0]
    for name in ("ScriptProbe.Status", "ScriptProbe.Read", "ScriptProbe.Velocity", "ScriptProbe.Empty"):
        matches = [row for row in report["markers"] if row["name"] == name and not row["gpu"]]
        if len(matches) > 1:
            raise ValueError(f"Ambiguous {name} marker: {capture}")
        if matches:
            markers[name] = matches[0]
    rows.append({"run": capture.parent.name, "sha256": report["sha256"], "frames": report["frames"],
                 "frameMsAxis": report["frameMsAxis"], "frameMs": report["frameMs"], "markers": markers})
if len(rows) != 8:
    raise ValueError("Expected eight paired and reversed captures")
if args.max_p95_increase_us is not None:
    native = [row["markers"]["Script.FixedTick"]["p95"] for row in rows if row["run"].endswith("-native")]
    legacy = [row["markers"]["Script.FixedTick"]["p95"] for row in rows if row["run"].endswith("-legacy")]
    increase = (statistics.median(native) - statistics.median(legacy)) * 1000.0
    if increase > args.max_p95_increase_us:
        raise ValueError(f"Fixed-tick p95 increase {increase:.2f}us exceeds {args.max_p95_increase_us:.2f}us")
print(json.dumps(rows, indent=2))
