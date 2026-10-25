#!/usr/bin/env python3
"""Run the declaration lifetime contract with the real engine scheduler and enkiTS.

Requires a local enkiTS source checkout and GCC/Clang with C++23 support. This
runner never downloads dependencies or patches Utility sources. The current
vcpkg baseline 9e593bb18ea69cc5095e012465dcd675a822ed0d pins enkiTS v1.12,
commit 0289cf6ffce86697e1f46cb4eeca1c946bffec71.

Run: python3 Tools/regression/verify-render-graph-declaration-task.py --enkits /path/to/enkiTS

The probe uses CE_CONTRACT_STANDALONE=1 and CE_SHIPPING=1. It validates CPU task
execution and exception/lifetime draining, not RHI behavior or profiler events.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--enkits", required=True, type=Path, help="Local enkiTS checkout root or its src directory")
    parser.add_argument("--compiler", help="GCC/Clang C++ compiler; defaults to g++ or clang++")
    parser.add_argument("--timeout", type=float, default=30, help="Probe execution timeout in seconds")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    source = args.enkits.resolve()
    if (source / "src" / "TaskScheduler.cpp").is_file():
        source = source / "src"
    if not all((source / name).is_file() for name in ("TaskScheduler.cpp", "TaskScheduler.h")):
        parser.error("--enkits must contain TaskScheduler.cpp and TaskScheduler.h, directly or under src")
    compiler = shutil.which(args.compiler) if args.compiler else shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        parser.error("a GCC/Clang C++23 compiler is required")

    with tempfile.TemporaryDirectory(prefix="render-graph-declaration-task-") as directory:
        target = Path(directory)
        include = target / "include" / "enkiTS"
        include.mkdir(parents=True)
        # Preserve the dependency bytes while supplying the vcpkg include layout.
        shutil.copyfile(source / "TaskScheduler.h", include / "TaskScheduler.h")
        executable = target / "declaration-task-probe"
        command = [
            compiler, "-std=c++23", "-O2", "-pthread", "-DCE_CONTRACT_STANDALONE=1", "-DCE_SHIPPING=1",
            "-I", str(ROOT), "-I", str(include.parent),
            str(ROOT / "Tools/regression/render_graph_declaration_task_probe.cpp"),
            str(ROOT / "Engine/Utility_Framework/Concurrency/JobScheduler.cpp"),
            str(ROOT / "Engine/Utility_Framework/Concurrency/ThreadPool.cpp"),
            str(source / "TaskScheduler.cpp"), "-o", str(executable),
        ]
        print("Compiling production declaration helper + JobScheduler.cpp + ThreadPool.cpp", flush=True)
        subprocess.run(command, check=True, timeout=120)
        subprocess.run([str(executable)], check=True, timeout=args.timeout)


if __name__ == "__main__":
    main()
