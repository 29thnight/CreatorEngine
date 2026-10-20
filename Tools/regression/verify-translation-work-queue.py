#!/usr/bin/env python3
"""Compile the exact production Translate body and its bounded weighted queue.

The fixture replaces RHI/native encoders and the pool with fixed CPU storage and
joined std::threads. The production function is included unchanged after its
includes. This checks target ownership/order, weighted work claims, allocation
and failure draining; it does not measure native/GPU/frame performance.
With --enkits, normal/replay-failure cases use the actual engine recording-jobs
helper, JobScheduler and ThreadPool; controlled delay/admission-failure cases
still use joined std::threads. Use a local enkiTS v1.12 checkout at the engine's
pinned commit 0289cf6ffce86697e1f46cb4eeca1c946bffec71.
No dependencies are downloaded and no production source is modified.
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
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--enkits", type=Path, help="Optional local pinned enkiTS source checkout")
    parser.add_argument("--sanitize", action="store_true", help="Enable AddressSanitizer and UBSan, fail fast")
    parser.add_argument("--output", type=Path, help="Optional source-coherent JSON evidence")
    args = parser.parse_args()
    root = args.source_root.resolve()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("a GCC/Clang C++23 compiler is required")
    paths = [root / "Engine/RenderEngine/RHI/RHICommandTranslator.cpp",
             root / "Engine/RenderEngine/RHI/RHITranslationWorkQueue.h",
             ROOT / "Tools/regression/translation_work_queue_probe.cpp", Path(__file__).resolve(),
             root / "Engine/Utility_Framework/StandardLibrary.h"]
    dependency = args.enkits.resolve() if args.enkits else None
    if dependency:
        if (dependency / "src/TaskScheduler.cpp").is_file():
            dependency /= "src"
        if not all((dependency / name).is_file() for name in ("TaskScheduler.cpp", "TaskScheduler.h")):
            parser.error("--enkits must contain TaskScheduler.cpp and TaskScheduler.h, directly or under src")
        paths += [root / "Engine/RenderEngine/RHI/RHICommandRecordingJobs.h",
                  root / "Engine/RenderEngine/RHI/RHIRecordingTaskGraph.h",
                  root / "Engine/Utility_Framework/JobScheduler.h",
                  root / "Engine/Utility_Framework/JobScheduler.cpp",
                  root / "Engine/Utility_Framework/ThreadPool.h",
                  root / "Engine/Utility_Framework/ThreadPool.cpp",
                  root / "Engine/Utility_Framework/JobGraph.h",
                  dependency / "TaskScheduler.cpp", dependency / "TaskScheduler.h"]
    snapshots = {path: path.read_bytes() for path in paths}
    hashes = {str(path.relative_to(root) if path.is_relative_to(root) else path):
              hashlib.sha256(data).hexdigest() for path, data in snapshots.items()}
    source = snapshots[paths[0]].decode("utf-8")
    signature = "bool RHICommandTranslator::Translate("
    if source.count(signature) != 1:
        raise RuntimeError("expected one complete production Translate definition")
    body = source[source.index(signature):]
    if any(line.strip() and not line.startswith("#include ") for line in source[:source.index(signature)].splitlines()):
        raise RuntimeError("production declarations before Translate require a fixture review")

    with tempfile.TemporaryDirectory(prefix="translation-work-queue-") as directory:
        target = Path(directory)
        (target / "RHICommandTranslatorProduction.inl").write_bytes(body.encode("utf-8"))
        probe = target / "probe.cpp"
        probe.write_bytes(snapshots[paths[2]])
        executable = target / "translation-work-queue"
        command = [compiler, "-std=c++23", "-O2", "-pthread", "-DCE_SHIPPING=1", "-DCE_CONTRACT_STANDALONE=1",
                   "-I", str(target), "-I", str(root),
                   str(probe), "-o", str(executable)]
        if dependency:
            include = target / "include/enkiTS"
            include.mkdir(parents=True)
            (include / "TaskScheduler.h").write_bytes(snapshots[dependency / "TaskScheduler.h"])
            command += ["-DCE_TRANSLATION_NATIVE_JOBS=1",
                        "-I", str(root / "Engine/Utility_Framework"), "-I", str(include.parent),
                        str(root / "Engine/Utility_Framework/JobScheduler.cpp"),
                        str(root / "Engine/Utility_Framework/ThreadPool.cpp"), str(dependency / "TaskScheduler.cpp")]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        print("Compiling exact RHICommandTranslator::Translate and fixed weighted work queue", flush=True)
        subprocess.run(command, check=True, timeout=120)
        environment = os.environ.copy()
        if args.sanitize:
            environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
            environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        result = subprocess.run([str(executable)], text=True, capture_output=True,
                                timeout=30, env=environment)
        print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="")
        result.check_returncode()
        for path, data in snapshots.items():
            if path.read_bytes() != data:
                raise RuntimeError(f"source changed during verification: {path}")
        evidence = {"source_sha256": hashes, "sanitized": args.sanitize, "native_scheduler": bool(dependency),
                    "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
                    "boundary": "exact-production-function; fixed CPU RHI fixture; no native/GPU/frame timing",
                    "production_body_sha256": hashlib.sha256(body.encode()).hexdigest(),
                    "output": result.stdout.splitlines()}
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
