#!/usr/bin/env python3
"""Measure warmed production graph allocation requests and enforce retained-storage budgets.

Optional --baseline-ref overlays only Graph production files from that Git
revision. Both executables use the same frozen fixture and other engine inputs,
are compiled before timing starts, and run in alternating order. Results describe
the portable CPU compiler/planner, not native GPU frame time or Windows Debug STL.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil
import statistics
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
GRAPH = Path("Engine/RenderEngine/Render/Graph")
PROBE = Path("Tools/regression/render_graph_allocation_probe.cpp")


def mirror(source, destination, replacements):
    destination.mkdir()
    for child in source.iterdir():
        selected = {path.relative_to(child.name): data for path, data in replacements.items()
                    if path.parts[0] == child.name}
        target = destination / child.name
        if Path(".") in selected:
            target.write_bytes(selected[Path(".")])
        elif selected:
            mirror(child, target, selected)
        else:
            target.symlink_to(child, target_is_directory=child.is_dir())


def source_identity(files):
    digest = hashlib.sha256()
    graph_hashes = {}
    for path, data in sorted(files.items()):
        value = hashlib.sha256(data).hexdigest()
        digest.update(path.as_posix().encode() + b"\0" + value.encode() + b"\n")
        if GRAPH in path.parents or path == PROBE or "fixtures" in path.parts:
            graph_hashes[path.as_posix()] = value
    return {"frozen_files": len(files), "frozen_inputs_sha256": digest.hexdigest(),
            "graph_and_fixture_sha256": graph_hashes}


def summarize(runs, enforce):
    names = [row["name"] for row in runs[0]["measurements"]]
    summary = []
    for name in names:
        rows = [next(row for row in run["measurements"] if row["name"] == name) for run in runs]
        counts = []
        for row in rows:
            sample = {key: row[key] / row["iterations"] for key in
                      ("standard_calls", "standard_bytes", "mimalloc_calls", "mimalloc_bytes")}
            counts.append(sample)
            # Queue submission intentionally allocates one independently owned
            # completion lease. Its lifetime ends only after native collection.
            allowed = 1 if name.startswith("queue_submit_") else 0
            if enforce and (sample["standard_calls"] > allowed or sample["mimalloc_calls"] != 0):
                raise RuntimeError(f"warm allocation budget exceeded for {name}: {sample}")
        if any(value != counts[0] for value in counts):
            raise RuntimeError(f"warm allocation counts changed between repetitions: {name}")
        timings = [row["cpu_nanoseconds"] / row["iterations"] for row in rows]
        summary.append({"name": name, "per_iteration": counts[0],
                        "cpu_nanoseconds_per_iteration_median": statistics.median(timings),
                        "cpu_nanoseconds_per_iteration_min": min(timings),
                        "cpu_nanoseconds_per_iteration_max": max(timings),
                        "timing_samples": timings})
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--baseline-ref")
    parser.add_argument("--repetitions", type=int, default=9)
    parser.add_argument("--iterations", type=int, default=512)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--allow-warm-allocations", action="store_true")
    parser.add_argument("--retain-binaries", type=Path,
                        help="copy the exact measured executables here for later runs without compilation")
    args = parser.parse_args()
    if args.repetitions < 1 or not 1 <= args.iterations <= 100000:
        parser.error("positive repetitions and 1..100000 iterations are required")
    root = args.source_root.resolve()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("a GCC/Clang C++23 compiler is required")
    frozen = {}
    for directory in ("Engine/RenderEngine", "Engine/Utility_Framework", "Tools/regression"):
        for path in (root / directory).rglob("*"):
            if path.is_file() and path.suffix in {".h", ".hpp", ".inl", ".cpp", ".py"}:
                frozen[path.relative_to(root)] = path.read_bytes()
    versions = {"current": dict(frozen)}
    baseline_ref = None
    if args.baseline_ref:
        baseline_ref = subprocess.check_output(["git", "rev-parse", args.baseline_ref], cwd=root, text=True).strip()
        baseline = dict(frozen)
        paths = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", baseline_ref, str(GRAPH)],
                                        cwd=root, text=True).splitlines()
        for name in paths:
            path = Path(name)
            if path.suffix in {".h", ".hpp", ".inl", ".cpp"}:
                baseline[path] = subprocess.check_output(["git", "show", f"{baseline_ref}:{name}"], cwd=root)
        versions["baseline"] = baseline
    build = runpy.run_path(str(root / "Tools/regression/verify-render-graph-program.py"))["compile_probe"]
    reports = {}
    with tempfile.TemporaryDirectory(prefix="render-graph-allocations-") as temporary:
        executables = {}
        for label, files in versions.items():
            source = Path(temporary) / label
            mirror(root, source, files)
            executable = Path(temporary) / f"probe-{label}"
            build(source, compiler, executable, PROBE, sanitize=args.sanitize)
            executables[label] = executable
            reports[label] = {"source": source_identity(files), "runs": [],
                              "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest()}
            if args.retain_binaries:
                args.retain_binaries.mkdir(parents=True, exist_ok=True)
                shutil.copy2(executable, args.retain_binaries / f"probe-{label}")
        environment = os.environ.copy()
        environment["CE_GRAPH_ALLOCATION_ITERATIONS"] = str(args.iterations)
        labels = list(executables)
        for repetition in range(args.repetitions):
            for label in labels if repetition % 2 == 0 else reversed(labels):
                result = subprocess.run([str(executables[label])], cwd=root, env=environment,
                                        capture_output=True, text=True)
                if result.returncode:
                    sys.stderr.write(result.stdout + result.stderr)
                    raise SystemExit(result.returncode)
                reports[label]["runs"].append(json.loads(result.stdout))
    for label, report in reports.items():
        report["summary"] = summarize(report["runs"], label == "current" and not args.allow_warm_allocations)
    print(json.dumps({"status": "passed", "sanitized": args.sanitize, "baseline_ref": baseline_ref,
                      "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
                      "repetitions": args.repetitions, "iterations_per_repetition": args.iterations,
                      "timing_order": "both binaries compiled before alternating current/baseline process runs",
                      "source_changed_after_snapshot": [str(path) for path, data in frozen.items()
                                                         if (root / path).read_bytes() != data],
                      "scope": "production CPU graph; successful C++ new and mimalloc requests; one producer; native mock allocations excluded from SubmitQueues",
                      "limits": "not native rendering, GPU frame time, real parallel jobs, Windows Debug STL, libc-internal allocations, or allocator retained capacity",
                      "versions": reports}, indent=2))


if __name__ == "__main__":
    main()
