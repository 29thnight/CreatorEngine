#!/usr/bin/env python3
"""Verify production Utility allocators without a renderer or native graphics API.

The isolated build contains only the allocator Utility sources, this probe and
the vendored mimalloc implementation. GNU linker wrapping counts actual public
new/mimalloc allocation requests without replacing the allocator. Measurements
enforce allocation budgets, not execution-time thresholds or GPU performance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[2]
UTILITY = Path("Engine/Utility_Framework")
PROBE = Path("Tools/regression/utility_allocators_probe.cpp")
RUNNER = Path("Tools/regression/verify-utility-allocators.py")
REQUIRED = [UTILITY / name for name in (
    "Arena.h", "Arena.cpp", "HeapAllocator.h", "LinearArena.h", "StackArena.h", "FixedPool.h"
)] + [PROBE, RUNNER]


def execute(command, cwd, environment=None):
    result = subprocess.run(command, cwd=cwd, env=environment, capture_output=True, text=True, timeout=300)
    if result.returncode:
        sys.stderr.write(result.stdout + result.stderr)
        raise RuntimeError(f"command failed ({result.returncode}): {shlex.join(command)}")
    return result


def file_hash(data):
    return hashlib.sha256(data).hexdigest()


def dependencies(path):
    # GCC/Clang -MMD makefiles escape spaces and continue long input lists.
    body = path.read_text().replace("\\\n", " ").split(":", 1)[1]
    return [Path(name).resolve() for name in shlex.split(body)]


def verify(root, work, compiler, c_compiler, sanitize):
    source = work / "source"
    build = work / "build"
    source.mkdir(parents=True)
    build.mkdir()
    inputs = {path: (root / path).read_bytes() for path in REQUIRED}
    for folder in ("ThirdParty/mimalloc/include", "ThirdParty/mimalloc/src"):
        for path in sorted((root / folder).rglob("*")):
            if path.is_file():
                inputs[path.relative_to(root)] = path.read_bytes()
    for path, data in inputs.items():
        target = source / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    if (source / "Engine/RenderEngine").exists():
        raise RuntimeError("Renderer sources unexpectedly entered the isolated Utility build")

    sanitizer_flags = (["-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-fno-sanitize-recover=all"] if sanitize else [])
    common = ["-O1" if sanitize else "-O2", "-g", "-pthread", "-DMI_STATIC_LIB", *sanitizer_flags]
    allocator_command = [c_compiler, "-std=c11", *common,
                         f"-I{source / 'ThirdParty/mimalloc/include'}", "-MMD", "-MF", str(build / "mimalloc.d"),
                         "-c", str(source / "ThirdParty/mimalloc/src/static.c"), "-o", str(build / "mimalloc.o")]
    commands = [allocator_command]
    logs = [execute(allocator_command, source).stderr]
    objects = []
    for name, path in (("arena", UTILITY / "Arena.cpp"), ("probe", PROBE)):
        target = build / f"{name}.o"
        command = [compiler, "-std=c++23", *common, "-Wall", "-Wextra", "-Wpedantic", "-MMD", "-MF",
                   str(build / f"{name}.d"), "-c", str(source / path), "-o", str(target)]
        commands.append(command)
        logs.append(execute(command, source).stderr)
        objects.append(target)
    executable = build / "utility-allocators"
    link_command = [compiler, *common, *map(str, objects), str(build / "mimalloc.o"),
                    "-Wl,--wrap=mi_malloc", "-Wl,--wrap=mi_malloc_aligned", "-o", str(executable)]
    commands.append(link_command)
    logs.append(execute(link_command, source).stderr)

    actual_dependencies = set()
    for dependency_file in sorted(build.glob("*.d")):
        for path in dependencies(dependency_file):
            try:
                actual_dependencies.add(path.relative_to(source).as_posix())
            except ValueError:
                raise RuntimeError(f"Compiler escaped the isolated source snapshot: {path}") from None
    renderer_dependencies = sorted(path for path in actual_dependencies if "RenderEngine" in Path(path).parts)
    if renderer_dependencies:
        raise RuntimeError(f"Utility allocator build depends on RenderEngine: {renderer_dependencies}")

    environment = dict(os.environ)
    if sanitize:
        # LeakSanitizer requires process-inspection capabilities unavailable in
        # some execution sandboxes; address and undefined-behavior checks remain.
        environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1:abort_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    started = time.monotonic()
    run = execute([str(executable)], source, environment)
    probe = json.loads(run.stdout)
    if probe.get("status") != "passed" or len(probe.get("measurements", [])) != 7:
        raise RuntimeError("The utility probe did not report all required allocation budgets")
    for row in probe["measurements"]:
        if row["standard_calls"] != 0 or row["mimalloc_calls"] != 0:
            raise RuntimeError(f"Unexpected bounded/warm allocation: {row}")
    lifetime_rejections = []
    for mode in ("--death-arena-before-scope", "--death-outer-before-inner"):
        rejected = subprocess.run([str(executable), mode], cwd=source, env=environment,
                                  capture_output=True, text=True, timeout=30)
        if rejected.returncode != 73 or rejected.stdout or rejected.stderr:
            sys.stderr.write(rejected.stdout + rejected.stderr)
            raise RuntimeError(f"Stack arena lifetime misuse was not rejected cleanly: {mode}, exit {rejected.returncode}")
        lifetime_rejections.append({"scenario": mode.removeprefix("--death-"),
                                    "expected_termination_exit": 73, "observed_exit": rejected.returncode})
    changed = [path.as_posix() for path, data in inputs.items()
               if not (root / path).is_file() or (root / path).read_bytes() != data]
    hashes = {path.as_posix(): file_hash(data) for path, data in sorted(inputs.items())}
    aggregate = hashlib.sha256()
    for path, digest in hashes.items():
        aggregate.update(path.encode() + b"\0" + digest.encode() + b"\n")
    return {
        "status": "passed",
        "sanitized": sanitize,
        "compiler": execute([compiler, "--version"], source).stdout.splitlines()[0],
        "c_compiler": execute([c_compiler, "--version"], source).stdout.splitlines()[0],
        "scope": "production Utility allocators, real vendored mimalloc, one writer, no RenderEngine or native graphics API",
        "limits": "counts new and public mi_malloc/mi_malloc_aligned requests; excludes allocator internals, cold retained-arena growth and allocations made by user payload constructors; no timing or GPU claim",
        "leak_sanitizer": "disabled for process-inspection-restricted environments" if sanitize else "not enabled",
        "standalone_link": {
            "renderer_source_present": False,
            "renderer_dependencies": renderer_dependencies,
            "compiler_dependencies": sorted(actual_dependencies),
        },
        "source": {"frozen_files": len(inputs), "frozen_inputs_sha256": aggregate.hexdigest(), "sha256": hashes},
        "source_changed_after_snapshot": changed,
        "build_commands": commands,
        "build_stderr": "".join(logs),
        "executable_sha256": file_hash(executable.read_bytes()),
        "probe": probe,
        "lifetime_rejections": lifetime_rejections,
        "run_stderr": run.stderr,
        "run_wall_seconds": time.monotonic() - started,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--c-compiler", default="gcc")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--keep-build", type=Path, help="retain the isolated source/build in a new or empty directory")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    c_compiler = shutil.which(args.c_compiler)
    if not compiler or not c_compiler:
        parser.error("GCC/Clang C++23 and C compilers plus a GNU-compatible --wrap linker are required")
    root = args.source_root.resolve()
    if args.keep_build:
        work = args.keep_build.resolve()
        if work.exists() and any(work.iterdir()):
            parser.error("--keep-build must name a new or empty directory")
        work.mkdir(parents=True, exist_ok=True)
        result = verify(root, work, compiler, c_compiler, args.sanitize)
    else:
        with tempfile.TemporaryDirectory(prefix="utility-allocators-") as temporary:
            result = verify(root, Path(temporary), compiler, c_compiler, args.sanitize)
    output = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output)
    sys.stdout.write(output)


if __name__ == "__main__":
    main()
