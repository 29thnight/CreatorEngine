#!/usr/bin/env python3
"""Verify exact Scene packet admission/build/join bodies with the real scheduler.

The RHI upload boundary and pre-existing GPU packet-plan preparation are fixtures.
Production RenderArena uses a malloc shim in place of unavailable mimalloc; a
fixed admission-count adapter forwards directly to the actual preparation helper.
This tests CPU ownership, byte identity, ordering and allocations; it is not a native
Editor, GPU, or end-to-end frame-time measurement. No dependencies are downloaded.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
HOST = Path("Engine/RenderEngine/MaterialGraphSceneHost.cpp")
HELPER = Path("Engine/RenderEngine/MaterialGraphPreparationTasks.h")
INDIRECT = Path("Engine/RenderEngine/MaterialGraphIndirectBatch.h")
BASELINE = "7ebcd2b1a2a181e88e4c2fe1c8952242242ab536"


def block(source, anchor):
    start = source.index(anchor)
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: "".join("\n" if char == "\n" else " " for char in match.group()), source)
    begin = masked.index("{", start)
    depth = 0
    for index in range(begin, len(masked)):
        depth += (masked[index] == "{") - (masked[index] == "}")
        if depth == 0:
            return start, index + 1
    raise RuntimeError(f"Unclosed source block: {anchor}")


def section(source, label, first, last):
    start = source.index(first)
    end = source.index(last, start)
    return f'#line {source.count(chr(10), 0, start) + 1} "{label}"\n{source[start:end]}\n'


def emit(source, label, anchor, semicolon=False):
    begin, end = block(source, anchor)
    if semicolon:
        end = source.index(";", end) + 1
    return f'#line {source.count(chr(10), 0, begin) + 1} "{label}"\n{source[begin:end]}\n'


def host_body(source, label):
    result = section(source, label, "            using BucketKey =", "            const auto preparePacketPipelines =")
    result += section(source, label, "            const auto prepare = [&](const PacketGroups&",
                      "            const auto needsMaterialTiles =")
    result += "\nReleaseOnExit releaseOnExit{device_};\nAfterCameraAdmission();\n"
    result += section(source, label, "            for (const bool staticCasters : {true, false})",
                      "            // Only transmitting/blended surfaces replay geometry.")
    result += "\nBeforePacketJoin();\n"
    result += emit(source, label, '            {\n                ce::profile_scope packetJoin{ce::marker<"MaterialScenePacketJoin">()};')
    return result + "\nreturn true;\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--enkits", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--baseline-ref", default=BASELINE)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--sanitize", choices=("none", "address", "thread"), default="none")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--output-executable", type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    enki = args.enkits.resolve()
    if (enki / "src/TaskScheduler.cpp").is_file():
        enki /= "src"
    if not all((enki / name).is_file() for name in ("TaskScheduler.cpp", "TaskScheduler.h")):
        parser.error("--enkits must contain TaskScheduler.h/.cpp directly or under src")
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("C++23 compiler unavailable")
    arena = [Path("Engine/RenderEngine") / name for name in ("RenderArena.cpp", "RenderArena.h", "RenderAllocator.h")]
    arena += [Path("Engine/Utility_Framework") / name for name in
              ("Arena.cpp", "Arena.h", "HeapAllocator.h", "Ownership.h", "LifetimeToken.h")]
    arena += [Path("ThirdParty/ownership_cpp/include/own/ownership.hpp")]
    scheduler = [Path("Engine/Utility_Framework") / name for name in
                 ("JobScheduler.cpp", "JobScheduler.h", "ThreadPool.cpp", "ThreadPool.h")]
    probe = Path(__file__).with_name("scene_packet_preparation_cpu_probe.cpp")
    inputs = [root / path for path in [HOST, HELPER, INDIRECT] + arena + scheduler]
    inputs += [probe, Path(__file__).resolve(), enki / "TaskScheduler.cpp", enki / "TaskScheduler.h"]
    snapshots = {path: path.read_bytes() for path in inputs}
    before = {str(path): hashlib.sha256(data).hexdigest() for path, data in snapshots.items()}
    baseline = {}
    for path in (HOST, HELPER):
        baseline[path] = subprocess.check_output(["git", "show", f"{args.baseline_ref}:{path.as_posix()}"], cwd=root)
    with tempfile.TemporaryDirectory(prefix="scene-packet-preparation-") as directory:
        target = Path(directory)
        mirror = target / "mirror"
        for path in arena:
            destination = mirror / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(snapshots[root / path])
        # Only the allocator service boundary is substituted. Arena bodies stay exact.
        shim = mirror / "ThirdParty/mimalloc/include/mimalloc.h"
        shim.parent.mkdir(parents=True)
        shim.write_text("""#pragma once
#include <cstdlib>
void scene_packet_count_allocation();
inline void* mi_malloc(size_t size) { scene_packet_count_allocation(); return std::malloc(size); }
inline void mi_free(void* value) { std::free(value); }
inline void* mi_malloc_aligned(size_t size, size_t alignment)
{
    scene_packet_count_allocation();
    void* result{};
    if (posix_memalign(&result, alignment < sizeof(void*) ? sizeof(void*) : alignment,
                       size ? size : 1) != 0) return nullptr;
    return result;
}
""", encoding="utf-8")
        for name, content in (("CurrentPackets.inl", snapshots[root / HOST]), ("BaselinePackets.inl", baseline[HOST])):
            label = str(root / HOST) if name.startswith("Current") else f"{args.baseline_ref}/{HOST.as_posix()}"
            (target / name).write_text(host_body(content.decode("utf-8-sig"), label), encoding="utf-8")
        current_host = snapshots[root / HOST].decode("utf-8-sig")
        (target / "PacketPreparationScratch.inl").write_text(
            section(current_host, str(root / HOST), "        ce::RenderArenaPool m_pool;", "        struct Work\n"),
            encoding="utf-8")
        old_helper = baseline[HELPER].decode("utf-8-sig").replace("namespace material_graph", "namespace baseline_preparation", 1)
        old_helper = old_helper.replace('#include "../Utility_Framework/', '#include "Engine/Utility_Framework/')
        old_helper = old_helper.replace('#include "../EngineDiagnostics/', '#include "Engine/EngineDiagnostics/')
        (target / "BaselinePreparationTasks.h").write_text(old_helper, encoding="utf-8")
        indirect = snapshots[root / INDIRECT].decode("utf-8-sig")
        (target / "PacketDraw.inl").write_text(emit(indirect, str(root / INDIRECT), "    struct PacketDraw", True), encoding="utf-8")
        (target / "PacketBucket.inl").write_text(emit(indirect, str(root / INDIRECT), "    struct Bucket", True), encoding="utf-8")
        (target / "UploadPackets.inl").write_text(emit(indirect, str(root / INDIRECT), "    bool UploadPackets("), encoding="utf-8")
        include = target / "enkiTS"
        include.mkdir()
        (include / "TaskScheduler.h").write_bytes(snapshots[enki / "TaskScheduler.h"])
        compiled_probe = target / probe.name
        compiled_probe.write_bytes(snapshots[probe])
        executable = target / "scene-packet-preparation"
        command = [compiler, "-std=c++23", "-O2", "-g", "-pthread", "-DCE_CONTRACT_STANDALONE=1", "-DCE_SHIPPING=1",
                   "-I", str(target), "-I", str(mirror), "-I", str(root), str(compiled_probe),
                   *[str(mirror / path) for path in arena if path.suffix == ".cpp"],
                   str(root / scheduler[0]), str(root / scheduler[2]),
                   str(enki / "TaskScheduler.cpp"), "-o", str(executable)]
        if args.sanitize != "none":
            command += ["-fsanitize=" + ("address,undefined" if args.sanitize == "address" else "thread"),
                        "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        print("Compiling exact Host packet bodies, upload copy, RenderArena and actual JobScheduler/ThreadPool/enkiTS", flush=True)
        subprocess.run(command, check=True, timeout=120)
        if args.output_executable:
            args.output_executable.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(executable, args.output_executable)
            args.output_executable.chmod(executable.stat().st_mode)
        result = subprocess.run([str(executable)],
                                capture_output=True, text=True, timeout=120)
        print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="")
        result.check_returncode()
        for path, original in snapshots.items():
            if path.read_bytes() != original:
                raise RuntimeError(f"Source changed during validation: {path}")
        evidence = {"scope": __doc__, "source_sha256": before,
                    "baseline_ref": args.baseline_ref,
                    "baseline_sha256": {str(path): hashlib.sha256(data).hexdigest() for path, data in baseline.items()},
                    "sanitizer": args.sanitize, "compiler_command": command,
                    "rows": [json.loads(line) for line in result.stdout.splitlines() if line.strip()]}
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
