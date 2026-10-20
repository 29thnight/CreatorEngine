#!/usr/bin/env python3
"""Verify recording DAG ordering/draining with production scheduler sources.

Requires a local enkiTS checkout matching v1.12 / 0289cf6ffce86697e1f46cb4eeca1c946bffec71.
This runner does not download dependencies. Native GPU command lists and frame
performance are outside its scope; CE_CONTRACT_STANDALONE and CE_SHIPPING are set.
The deliberate wave-join mutation must fail the existing early-progress check.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--enkits", required=True, type=Path)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    dependency = args.enkits.resolve()
    if (dependency / "src/TaskScheduler.cpp").is_file():
        dependency /= "src"
    if not compiler or not all((dependency / name).is_file() for name in ("TaskScheduler.h", "TaskScheduler.cpp")):
        parser.error("a C++23 GCC/Clang compiler and a local enkiTS source checkout are required")
    probe = ROOT / "Tools/regression/recording_task_graph_probe.cpp"
    inputs = [probe, ROOT / "Engine/RenderEngine/RHI/RHICommandRecordingJobs.h",
              ROOT / "Engine/RenderEngine/RHI/RHIRecordingTaskGraph.h",
              ROOT / "Engine/Utility_Framework/JobScheduler.cpp",
              ROOT / "Engine/Utility_Framework/JobScheduler.h",
              ROOT / "Engine/Utility_Framework/ThreadPool.cpp",
              ROOT / "Engine/Utility_Framework/ThreadPool.h",
              dependency / "TaskScheduler.h", dependency / "TaskScheduler.cpp"]

    def source_hashes():
        return {str(path.relative_to(ROOT) if path.is_relative_to(ROOT) else path):
                hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}

    before = source_hashes()
    rows = []
    with tempfile.TemporaryDirectory(prefix="recording-task-graph-") as directory:
        target = Path(directory)
        include = target / "include/enkiTS"
        include.mkdir(parents=True)
        shutil.copyfile(dependency / "TaskScheduler.h", include / "TaskScheduler.h")
        for mutant in (False, True):
            executable = target / ("wave-join-mutant" if mutant else "recording-task-graph")
            command = [compiler, "-std=c++23", "-O2", "-pthread", "-DCE_CONTRACT_STANDALONE=1", "-DCE_SHIPPING=1",
                       "-I", str(ROOT / "Engine/RenderEngine"), "-I", str(ROOT / "Engine/Utility_Framework"),
                       "-I", str(include.parent), str(probe),
                       str(ROOT / "Engine/Utility_Framework/JobScheduler.cpp"),
                       str(ROOT / "Engine/Utility_Framework/ThreadPool.cpp"),
                       str(dependency / "TaskScheduler.cpp"), "-o", str(executable)]
            if mutant:
                command.append("-DRECORDING_WAVE_JOIN_MUTANT=1")
            if args.sanitize:
                command.extend(["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"])
            subprocess.run(command, check=True, timeout=120)
            result = subprocess.run([str(executable)], text=True, capture_output=True, timeout=30)
            if mutant:
                valid = result.returncode == 1 and any(message in result.stderr for message in (
                    "unrelated wave work became a dependency", "consumer waited for unrelated"))
            else:
                valid = result.returncode == 0 and "workers=1,4,8 targets=8 policies=legacy,native" in result.stdout
            if not valid:
                raise RuntimeError(f"Recording task graph probe failed (mutant={mutant}):\n{result.stdout}{result.stderr}")
            row = {"mutant": mutant, "passed": True, "stdout": result.stdout.strip(), "stderr": result.stderr.strip()}
            rows.append(row)
            print(json.dumps(row), flush=True)
    if before != source_hashes():
        raise RuntimeError("production, probe or dependency source changed during verification")
    evidence = {"scope": "CPU recording DAG correctness only; no GPU/frame benchmark", "sanitized": args.sanitize,
                "asan_options": os.environ.get("ASAN_OPTIONS", ""), "source_sha256": before, "rows": rows}
    if args.output:
        args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
