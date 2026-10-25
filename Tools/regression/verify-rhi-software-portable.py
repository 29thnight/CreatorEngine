#!/usr/bin/env python3
"""Build the actual RHI recorder/translator/FIFO with CPU-only native boundaries.

The frozen build uses unmodified production RHI and scheduler bodies, actual
vendored mimalloc, and the caller-supplied pinned enkiTS source. ProfileService.h
is the only header boundary: its three unconditional thread-registration APIs
are inert because the real service implementation requires Windows. Shipping
profile scopes and all RHI public types are otherwise the production headers.
No GPU, driver, Windows Editor, or end-to-end frame timing is executed here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time

PRODUCTION = [
    "Engine/RenderEngine/RHI/RHISoftwareCommandList.cpp",
    "Engine/RenderEngine/RHI/RHISoftwareEncoder.cpp",
    "Engine/RenderEngine/RHI/RHISoftwareBatch.cpp",
    "Engine/RenderEngine/RHI/RHISoftwareRecorder.cpp",
    "Engine/RenderEngine/RHI/RHICommandTranslator.cpp",
    "Engine/RenderEngine/RHI/RHISubmissionThread.cpp",
    "Engine/RenderEngine/RenderArena.cpp",
    "Engine/Utility_Framework/Concurrency/ThreadPool.cpp",
    "Engine/Utility_Framework/Concurrency/JobScheduler.cpp",
]
PROBE = "Tools/regression/rhi_software_translation_probe.cpp"
COMMAND_PROBE = "Tools/regression/rhi_software_commands_probe.cpp"
PROFILE_HEADER = "Engine/EngineDiagnostics/ProfileService.h"
PROFILE_BOUNDARY = r'''#pragma once
// CPU regression boundary only: the actual profiler implementation is Win32.
#include "ProfileThreadStream.h"
namespace ce
{
    class profiler_service
    {
      public:
        void register_thread(const char*, track_kind, uint32_t) {}
        void unregister_thread() {}
    };
    inline profiler_service& profiler()
    {
        static profiler_service instance;
        return instance;
    }
}
'''


def production_sources(root: Path) -> list[str]:
    # Preserve comparisons with revisions before the Utility extraction.
    utility_arena = "Engine/Utility_Framework/Memory/Arena.cpp"
    return [*PRODUCTION, *([utility_arena] if (root / utility_arena).is_file() else [])]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def execute(command: list[str], *, cwd: Path, timeout: int = 300,
            environment: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True,
                            timeout=timeout, env=environment)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {shlex.join(command)}\n"
                           f"{result.stdout}\n{result.stderr}")
    return result


def include_flags(root: Path, external_include: Path) -> list[str]:
    return [f"-I{root}", f"-I{root / 'Engine/Utility_Framework'}",
            f"-I{root / 'ThirdParty/Mathematics/include'}",
            f"-I{root / 'ThirdParty/ownership_cpp/include'}", f"-I{external_include}"]


def dependencies(root: Path, enkits: Path, compiler: str, build: Path) -> set[Path]:
    include = build / "dependency-include"
    include.mkdir()
    (include / "enkiTS").symlink_to(enkits / "src", target_is_directory=True)
    sources = [root / path for path in [PROBE, COMMAND_PROBE, *production_sources(root)]] + [enkits / "src/TaskScheduler.cpp"]
    args = [compiler, "-std=c++23", "-pthread", "-DCE_CONTRACT_STANDALONE=1", "-DCE_SHIPPING=1",
            *include_flags(root, include), "-MM", *map(str, sources)]
    result = execute(args, cwd=root)
    paths: set[Path] = set(sources)
    for line in result.stdout.replace("\\\n", " ").splitlines():
        if ":" in line:
            for value in shlex.split(line.split(":", 1)[1]):
                paths.add((root / value).resolve())
    c_result = execute(["gcc", "-std=c11", "-DMI_STATIC_LIB", f"-I{root / 'ThirdParty/mimalloc/include'}",
                        "-MM", str(root / "ThirdParty/mimalloc/src/static.c")], cwd=root)
    for line in c_result.stdout.replace("\\\n", " ").splitlines():
        if ":" in line:
            for value in shlex.split(line.split(":", 1)[1]):
                paths.add((root / value).resolve())
    return paths


def snapshot(root: Path, enkits: Path, paths: set[Path], destination: Path,
             baseline: str | None, probe_bytes: bytes, captured_bytes: dict[Path, bytes]) -> dict[str, str]:
    destination.mkdir()
    local_paths = sorted(path.relative_to(root).as_posix() for path in paths if path.is_relative_to(root))
    if baseline:
        existing = execute(["git", "ls-tree", "-r", "--name-only", baseline, "--", *local_paths], cwd=root).stdout.splitlines()
        # Read exactly the dependency blobs. git archive can request unrelated
        # ancestor .gitattributes from a partial clone's promisor remote, even
        # when every source needed by this fixture is already present locally.
        objects = subprocess.run(["git", "cat-file", "--batch"], cwd=root,
                                 input="".join(f"{baseline}:{path}\n" for path in existing).encode(),
                                 capture_output=True, timeout=300)
        if objects.returncode:
            raise RuntimeError(f"Pinned baseline blob read failed: {objects.stderr.decode(errors='replace')}")
        offset = 0
        for relative in existing:
            header_end = objects.stdout.find(b"\n", offset)
            header = objects.stdout[offset:header_end].split()
            if header_end < 0 or len(header) != 3 or header[1] != b"blob":
                raise RuntimeError(f"Pinned baseline dependency is not a blob: {relative}")
            count = int(header[2])
            first = header_end + 1
            last = first + count
            if objects.stdout[last:last + 1] != b"\n":
                raise RuntimeError(f"Truncated pinned baseline dependency: {relative}")
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(objects.stdout[first:last])
            offset = last + 1
    else:
        for relative in local_paths:
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(captured_bytes[root / relative])
    # The new test is the identical external observer for both production trees.
    target = destination / PROBE
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(probe_bytes)
    for source in sorted(paths):
        if source.is_relative_to(enkits):
            target = destination / "external/enkiTS" / source.relative_to(enkits)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(captured_bytes[source])
        elif not source.is_relative_to(root):
            raise RuntimeError(f"Unclassified build dependency: {source}")
    (destination / "external/include").mkdir(parents=True)
    (destination / "external/include/enkiTS").symlink_to(destination / "external/enkiTS/src", target_is_directory=True)
    # Hash the real input bytes first; boundary bytes are identified separately.
    hashes = {path.relative_to(destination).as_posix(): digest(path.read_bytes())
              for path in sorted(destination.rglob("*")) if path.is_file() and not path.is_symlink()
              and "external/include/" not in path.as_posix()}
    (destination / PROFILE_HEADER).write_text(PROFILE_BOUNDARY)
    return hashes


def build_and_run(source: Path, build: Path, compiler: str, sanitize: bool, benchmark: bool,
                  commands_probe: bool) -> dict:
    build.mkdir()
    flags = ["-std=c++23", "-O2", "-g", "-pthread", "-DCE_CONTRACT_STANDALONE=1", "-DCE_SHIPPING=1",
             "-DMI_STATIC_LIB", *include_flags(source, source / "external/include")]
    sanitize_flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"] if sanitize else []
    if sanitize:
        flags[1] = "-O1"
    # Do not define MI_MALLOC_OVERRIDE (even =0 enables the vendor #if defined).
    c_command = ["gcc", "-std=c11", "-O2", "-g", "-pthread", "-DMI_STATIC_LIB", *sanitize_flags,
                 f"-I{source / 'ThirdParty/mimalloc/include'}", "-c",
                 str(source / "ThirdParty/mimalloc/src/static.c"), "-o", str(build / "mimalloc.o")]
    execute(c_command, cwd=source)
    byte_capacity = "kSoftwareByteCapacity" in (source / "Engine/RenderEngine/RHI/RHISubmissionThread.h").read_text()
    structured_failure = "GetCommandFailure" in (source / "Engine/RenderEngine/RHI/RHISubmissionThread.h").read_text()
    compile_command = [compiler, *flags, *sanitize_flags, f"-DCE_RHI_BYTE_ADMISSION={int(byte_capacity)}",
                       f"-DCE_RHI_STRUCTURED_FAILURE={int(structured_failure)}",
                       str(source / PROBE), *[str(source / path) for path in production_sources(source)],
                       str(source / "external/enkiTS/src/TaskScheduler.cpp"), str(build / "mimalloc.o"),
                       "-Wl,--wrap=mi_malloc", "-Wl,--wrap=mi_malloc_aligned", "-o", str(build / "translation")]
    compiled = execute(compile_command, cwd=source)
    (build / "compile.stderr").write_text(compiled.stderr)
    environment = dict(os.environ)
    environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1:abort_on_error=1"
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    command = [str(build / "translation"), *(["--benchmark"] if benchmark else [])]
    started = time.monotonic()
    run = execute(command, cwd=source, timeout=300, environment=environment)
    result = json.loads(run.stdout)
    result["wall_seconds"] = time.monotonic() - started
    result["stderr"] = run.stderr
    result["build_command"] = compile_command
    result["allocator_build_command"] = c_command
    result["byte_admission_available"] = byte_capacity
    result["structured_failure_available"] = structured_failure
    if commands_probe:
        selected = ["Engine/RenderEngine/RenderArena.cpp", *PRODUCTION[:4]]
        if (source / "Engine/Utility_Framework/Memory/Arena.cpp").is_file():
            selected.append("Engine/Utility_Framework/Memory/Arena.cpp")
        old_command = [compiler, *flags, *sanitize_flags, str(source / COMMAND_PROBE),
                       *[str(source / path) for path in selected], str(build / "mimalloc.o"),
                       "-o", str(build / "commands")]
        execute(old_command, cwd=source)
        old_run = execute([str(build / "commands")], cwd=source, environment=environment)
        if "RHI_SOFTWARE_COMMANDS_OK" not in old_run.stdout:
            raise RuntimeError("Existing production command probe did not publish its PASS marker")
        result["existing_commands_probe"] = {"status": "PASS", "stdout": old_run.stdout, "stderr": old_run.stderr}
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--enkits", type=Path, required=True)
    parser.add_argument("--baseline-ref")
    parser.add_argument("--baseline-only", action="store_true")
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--skip-commands-probe", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    enkits = args.enkits.resolve()
    if args.sanitize and args.benchmark:
        parser.error("Sanitizer instrumentation is a correctness gate, not the timing configuration")
    if args.baseline_only and not args.baseline_ref:
        parser.error("--baseline-only requires a pinned --baseline-ref")
    if args.baseline_ref:
        args.baseline_ref = execute(["git", "rev-parse", f"{args.baseline_ref}^{{commit}}"],
                                    cwd=root).stdout.strip()
    work = args.work_dir.resolve() if args.work_dir else Path(tempfile.mkdtemp(prefix="creator-rhi-portable-"))
    work.mkdir(parents=True, exist_ok=True)
    # Capture bytes once, then derive both the content used and its digest.
    runner_bytes = Path(__file__).read_bytes()
    probe_bytes = (root / PROBE).read_bytes()
    paths = dependencies(root, enkits, args.compiler, work)
    captured_bytes = {path: path.read_bytes() for path in sorted(paths)}
    captured_bytes[root / PROBE] = probe_bytes
    captured = {str(path): digest(data) for path, data in captured_bytes.items()}
    report = {"schema": "creator.rhi_software.portable.v1", "status": "RUNNING",
              "baseline_ref": args.baseline_ref, "work_dir": str(work),
              "compiler": execute([args.compiler, "--version"], cwd=root).stdout.splitlines()[0],
              "runner_sha256": digest(runner_bytes), "probe_sha256": digest(probe_bytes),
              "profiling_boundary": {"path": PROFILE_HEADER, "sha256": digest(PROFILE_BOUNDARY.encode()),
                                     "apis": ["profiler", "profiler_service::register_thread", "profiler_service::unregister_thread"]},
              "limitations": ["CPU fixture replaces native device, encoder, queue submission and fence observation only",
                              "CE_SHIPPING=1; Windows profiler registration is inert at the explicit boundary",
                              "Actual production recorder, command list, translator, FIFO, arena, mimalloc and scheduler bodies are compiled",
                              "No Windows Editor, GPU execution, pixels, driver cost, or full-frame performance measured",
                              "Sanitizer mode disables LeakSanitizer because the sandbox restricts process inspection"],
              "sanitize": args.sanitize, "benchmark": args.benchmark, "builds": {}}
    report["host"] = {"platform": platform.platform(),
                      "cpu_affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None}
    cpu_max = Path("/sys/fs/cgroup/cpu.max")
    if cpu_max.is_file():
        report["host"]["cgroup_cpu_max"] = cpu_max.read_text().strip()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        variants = [("baseline", args.baseline_ref)] if args.baseline_ref else []
        if not args.baseline_only:
            variants.append(("current", None))
        for name, revision in variants:
            source = work / f"{name}-source"
            hashes = snapshot(root, enkits, paths, source, revision, probe_bytes, captured_bytes)
            result = build_and_run(source, work / f"{name}-build", args.compiler, args.sanitize,
                                   args.benchmark, not args.skip_commands_probe)
            result["input_sha256"] = hashes
            report["builds"][name] = result
        checked = captured if not args.baseline_only else {
            path: expected for path, expected in captured.items()
            if path == str(root / PROBE) or Path(path).is_relative_to(enkits)}
        mismatches = [path for path, expected in checked.items() if digest(Path(path).read_bytes()) != expected]
        report["source_unchanged"] = not mismatches and Path(__file__).read_bytes() == runner_bytes
        report["source_mismatches"] = mismatches
        if not report["source_unchanged"]:
            raise RuntimeError("Live source changed after its frozen build snapshot; rerun the final gate")
        report["status"] = "PASS"
    except Exception as exception:
        report["status"] = "FAIL"
        report["error"] = str(exception)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        raise
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "output": str(args.output),
                      "builds": {name: {"checks": result["checks"], "scenarios": result["scenarios"],
                                         "allocations": result["allocations"]}
                                 for name, result in report["builds"].items()}}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
