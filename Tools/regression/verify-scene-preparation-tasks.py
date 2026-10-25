#!/usr/bin/env python3
"""Verify ScenePreparationTasks with the real engine scheduler and local enkiTS.

Uses CE_CONTRACT_STANDALONE=1 and CE_SHIPPING=1; no GPU or Windows profiler
coverage is claimed. --benchmark measures CPU task submission/assisted joins,
not MaterialGraphScenePrepare frame time. The local enkiTS checkout must match
the engine's pinned dependency (v1.12 / 0289cf6ffce86697e1f46cb4eeca1c946bffec71).
No dependency is downloaded or production source patched by this runner.
"""

import runpy
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
BASELINE = "001ff417b7745401622f4f9ab8e8b9b15fb9ab4d"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--enkits", required=True, type=Path)
    parser.add_argument("--source-root", type=Path, default=ROOT, help="Production checkout to compile")
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--baseline-ref", default=BASELINE, help="Unmodified helper compared on the same active pool")
    parser.add_argument("--sanitize", action="store_true", help="Compile with AddressSanitizer and UBSan")
    parser.add_argument("--output", type=Path, help="Optional JSON evidence file")
    args = parser.parse_args()
    if args.sanitize and args.benchmark:
        parser.error("sanitizer checks and performance measurements must run separately")
    root = args.source_root.resolve()
    dependency = args.enkits.resolve()
    if (dependency / "src/TaskScheduler.cpp").is_file():
        dependency /= "src"
    if not all((dependency / name).is_file() for name in ("TaskScheduler.h", "TaskScheduler.cpp")):
        parser.error("--enkits must contain TaskScheduler.cpp and TaskScheduler.h, directly or under src")
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("a GCC/Clang C++23 compiler is required")
    sources = [root / "Engine/RenderEngine/MaterialGraphPreparationTasks.h",
               root / "Engine/Utility_Framework/Concurrency/JobScheduler.cpp",
               root / "Engine/Utility_Framework/Concurrency/ThreadPool.cpp"]
    probe = ROOT / "Tools/regression/scene_preparation_tasks_cpu_probe.cpp"
    inputs = sources + [probe,
                        dependency / "TaskScheduler.cpp", dependency / "TaskScheduler.h"]

    inputs = [root / path if not path.is_absolute() else path for path in runpy.run_path(str(root / "Tools/build/generate-module-adapters.py"))["jobs_source_inputs"](inputs)]

    def source_hashes():
        return {str(path.relative_to(root) if path.is_relative_to(root) else path):
                hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}

    before = source_hashes()
    baseline = subprocess.check_output(
        ["git", "show", args.baseline_ref + ":Engine/RenderEngine/MaterialGraphPreparationTasks.h"],
        cwd=root, text=True)
    with tempfile.TemporaryDirectory(prefix="scene-preparation-tasks-") as directory:
        target = Path(directory)
        include = target / "include/enkiTS"
        include.mkdir(parents=True)
        shutil.copyfile(dependency / "TaskScheduler.h", include / "TaskScheduler.h")
        # Only qualify the namespace/includes; both helper class bodies stay intact.
        baseline_header = baseline.replace("namespace material_graph", "namespace baseline_preparation", 1)
        baseline_header = baseline_header.replace('#include "../Utility_Framework/', '#include "Engine/Utility_Framework/')
        baseline_header = baseline_header.replace('#include "../EngineDiagnostics/', '#include "Engine/EngineDiagnostics/')
        (target / "BaselinePreparationTasks.h").write_text(baseline_header, encoding="utf-8")
        executable = target / "scene-preparation-tasks"
        command = [compiler, "-std=c++23", "-O2", "-pthread", "-DCE_CONTRACT_STANDALONE=1", "-DCE_SHIPPING=1",
                   "-DCE_SCENE_PREPARATION_COMPARE=1", "-I", str(target), "-I", str(root), "-I", str(include.parent),
                   str(probe),
                   str(sources[1]), str(sources[2]), str(dependency / "TaskScheduler.cpp"), "-o", str(executable)]
        if args.sanitize:
            command.extend(["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"])
        print("Compiling production preparation helper, JobScheduler.cpp and ThreadPool.cpp", flush=True)
        subprocess.run(command, check=True, timeout=120)
        result = subprocess.run([str(executable)] + (["--benchmark"] if args.benchmark else []),
                                check=True, text=True, capture_output=True, timeout=60)
        print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="")
        after = source_hashes()
        if before != after:
            raise RuntimeError("production, probe or dependency source changed during verification")
        evidence = {"source_sha256": before, "sanitized": args.sanitize,
                    "baseline_ref": args.baseline_ref, "baseline_sha256": hashlib.sha256(baseline.encode()).hexdigest(),
                    "comparison": "same-active-pool alternating-per-sample three-blocks",
                    "rows": [json.loads(line) for line in result.stdout.splitlines() if line.strip()]}
        if args.output:
            args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
