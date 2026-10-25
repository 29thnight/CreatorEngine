#!/usr/bin/env python3
"""Compile the owned software queue implementation with the real CPU scheduler.

The native recorder/queues/fences are deterministic CPU fixtures. Actual software
recording, replay, owned translator, queue wrappers and JobScheduler/ThreadPool
are compiled unchanged, including the complete device-services header. Malloc
replaces mimalloc for allocation counts and sanitizer coverage. This is not a
native/GPU/frame test.
No dependencies are downloaded or production sources modified.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
RHI = Path("Engine/RenderEngine/RHI")
CPP = [RHI / name for name in (
    "RHISoftwareQueueRecorder.cpp", "RHISoftwareCommandList.cpp", "RHISoftwareEncoder.cpp",
    "RHISoftwareBatch.cpp", "RHISoftwareRecorder.cpp")]
CPP += [Path("Engine/RenderEngine/RenderArena.cpp"), Path("Engine/Utility_Framework/Concurrency/JobScheduler.cpp"),
        Path("Engine/Utility_Framework/Concurrency/ThreadPool.cpp"), Path("Engine/Utility_Framework/Memory/Arena.cpp")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--enkits", type=Path, required=True)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    dependency = args.enkits.resolve()
    if (dependency / "src/TaskScheduler.cpp").is_file():
        dependency /= "src"
    if not all((dependency / name).is_file() for name in ("TaskScheduler.cpp", "TaskScheduler.h")):
        parser.error("--enkits must contain TaskScheduler.cpp and TaskScheduler.h, directly or under src")
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("a C++23 GCC/Clang compiler is required")
    snapshots = {}
    adapted = {}
    pending = [root / path for path in CPP]
    probe = root / "Tools/regression/rhi_software_queue_probe.cpp"
    pending.append(probe)
    include_roots = (root, root / "Engine/Utility_Framework", root / RHI,
                     root / "ThirdParty/Mathematics/include", root / "ThirdParty/ownership_cpp/include")
    while pending:
        path = pending.pop().resolve()
        if path in snapshots:
            continue
        data = path.read_bytes()
        snapshots[path] = data
        text = data.decode("utf-8-sig")
        adapted[path] = text
        for delimiter, include in re.findall(r'^\s*#\s*include\s*(["<])([^">]+)[">]', text, re.MULTILINE):
            if include.endswith("mimalloc.h"):
                allocator = (path.parent / include).resolve()
                snapshots.setdefault(allocator, allocator.read_bytes())
                continue
            candidates = [path.parent / include] + [directory / include for directory in include_roots]
            match = next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)
            if match is None and delimiter == '"':
                raise RuntimeError(f"unresolved quoted include {include} in {path}")
            if match is not None:
                pending.append(match)
    runner = Path(__file__).resolve()
    snapshots[runner] = runner.read_bytes()
    dependency_names = ("TaskScheduler.cpp", "TaskScheduler.h", "LockLessMultiReadPipe.h")
    for name in dependency_names:
        path = dependency / name
        snapshots[path] = path.read_bytes()
    hashes = {str(path.relative_to(root) if path.is_relative_to(root) else path):
              hashlib.sha256(data).hexdigest() for path, data in snapshots.items()}
    with tempfile.TemporaryDirectory(prefix="rhi-software-queue-") as directory:
        target = Path(directory)
        mirror = target / "mirror"
        for path in adapted:
            destination = mirror / path.relative_to(root)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(snapshots[path])
        shim = mirror / "ThirdParty/mimalloc/include/mimalloc.h"
        shim.parent.mkdir(parents=True, exist_ok=True)
        shim.write_text("""#pragma once
#include <cstddef>
void* queue_probe_allocate(std::size_t, std::size_t);
void queue_probe_free(void*) noexcept;
inline void* mi_malloc(std::size_t bytes) { return queue_probe_allocate(bytes, alignof(std::max_align_t)); }
inline void* mi_malloc_aligned(std::size_t bytes, std::size_t alignment)
{ return queue_probe_allocate(bytes, alignment); }
inline void mi_free(void* pointer) { queue_probe_free(pointer); }
""", encoding="utf-8")
        enkits = target / "include/enkiTS"
        enkits.mkdir(parents=True)
        for name in dependency_names:
            (enkits / name).write_bytes(snapshots[dependency / name])
        executable = target / "software-queue"
        command = [compiler, "-std=c++23", "-O2", "-g", "-pthread", "-DCE_CONTRACT_STANDALONE=1",
                   "-DCE_SHIPPING=1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unknown-pragmas",
                   "-I", str(mirror), "-I", str(mirror / "Engine/Utility_Framework"),
                   "-I", str(mirror / "ThirdParty/Mathematics/include"),
                   "-I", str(mirror / "ThirdParty/ownership_cpp/include"),
                   "-I", str(enkits.parent), str(mirror / probe.relative_to(root))]
        command += [str(mirror / path) for path in CPP]
        command += [str(enkits / "TaskScheduler.cpp"), "-o", str(executable)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        print("Compiling actual software queue/recording/replay and actual JobScheduler/ThreadPool/enkiTS", flush=True)
        subprocess.run(command, check=True, timeout=240)
        environment = os.environ.copy()
        if args.sanitize:
            environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
            environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        result = subprocess.run([str(executable)], text=True, capture_output=True, env=environment, timeout=60)
        print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="")
        result.check_returncode()
        for path, data in snapshots.items():
            if path.read_bytes() != data:
                raise RuntimeError(f"source changed during verification: {path}")
        evidence = {
            "source_sha256": hashes,
            "sanitized": args.sanitize,
            "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
            "flags": command[1:command.index("-I")],
            "boundary": "actual software queue/recording/replay + actual scheduler/enkiTS; CPU native recorder/fence fixture; malloc shim; no native GPU/frame execution",
            "service_header": "Complete production IRenderDeviceServices.h and transitive model/math/ownership headers unchanged",
            "rows": [json.loads(line) for line in result.stdout.splitlines() if line.strip()]}
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
