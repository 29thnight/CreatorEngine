"""Compare recorded RG8 whole render-thread frames in both collection orders."""
import argparse
import importlib.util
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    reader_path = Path(__file__).with_name("summarize-material-profile.py")
    spec = importlib.util.spec_from_file_location("rg8_profile_reader", reader_path)
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    audit_spec = importlib.util.spec_from_file_location("rg8_interval_audit", Path(__file__).with_name("audit-rg8-adoption.py"))
    audit = importlib.util.module_from_spec(audit_spec)
    audit_spec.loader.exec_module(audit)
    runs = {}
    for path in sorted(args.root.glob("*-*/whole-frame.ceprof")):
        report = reader.analyze(path)
        if not report["nativeRealPerformanceGateEligible"]:
            raise ValueError(f"Not a lossless native-real recording: {path}")
        scopes = [row for row in report["markers"] if row["name"] == "RenderThreadFrame" and not row["gpu"]]
        if len(scopes) != 1 or scopes[0]["calls"] < 32:
            raise ValueError(f"Missing unambiguous whole render-thread frames: {path}")
        path.with_suffix(".summary.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        joined = json.loads(path.with_name("normal-samples.json").read_text(encoding="utf-8-sig"))
        if len(joined) != 32:
            raise ValueError(f"Missing chronological normal-frame cohort: {path}")
        for sample in joined:
            if sample["execution"]:
                audit.verify_schedule(sample["execution"], sample["execution"]["mode"])
            audit.verify_timing(sample["timing"], sample["execution"])
        executions = [sample["execution"] for sample in joined if sample["execution"]]
        runs[path.parent.name] = dict(
            sha256=report["sha256"], frames=report["frames"], wholeRenderThread=scopes[0],
            renderGroups=report["renderGroups"],
            gpuQueueSpanMs=reader.stats([sample["timing"]["queueSpanMilliseconds"] for sample in joined]),
            measuredOverlapMs=reader.stats([sample["timing"]["measuredOverlapMilliseconds"] for sample in joined]),
            positiveOverlapFrames=sum(sample["timing"]["measuredOverlapMilliseconds"] > 0 for sample in joined),
            cpuFullGraphMs=reader.stats([sample["cpu"]["renderOnceMilliseconds"] for sample in joined]),
            cpuScheduleMs=reader.stats([value["execution"]["scheduleMilliseconds"] for value in executions]),
            cpuDependencyMs=reader.stats([value["schedule"]["dependencyMilliseconds"] for value in executions]),
            cpuSimulationMs=reader.stats([value["schedule"]["simulationMilliseconds"] for value in executions]),
            computeFrames=sum(value["execution"]["computeBatches"] > 0 for value in executions),
            insufficientGainFrames=sum(value["fallbackReason"] == "insufficient-gain" for value in executions))
    comparisons = []
    for order in ("Forward", "Reverse"):
        off = runs[f"{order}-0"]["wholeRenderThread"]
        for mode in (1, 2):
            on = runs[f"{order}-{mode}"]["wholeRenderThread"]
            comparisons.append(dict(order=order, mode=mode,
                                    p50Percent=(on["p50"] / off["p50"] - 1) * 100,
                                    p95Percent=(on["p95"] / off["p95"] - 1) * 100))
    result = dict(axis="complete-RenderThreadFrame-cpu-wall-time", runs=runs, comparisons=comparisons,
                  presentationOrDisplayFps=False)
    (args.root / "whole-frame-comparison.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(comparisons, indent=2))


if __name__ == "__main__":
    main()
