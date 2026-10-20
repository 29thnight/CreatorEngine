#!/usr/bin/env python3
"""Exercise exact graph packet bodies with production scheduling and enkiTS.

Resource/Pass are metadata projections, not an RHI implementation. Declaration,
partition, materialization, finish and cancellation bodies are extracted without
rewriting their logic. A wrapper observes materialization ranges and injects a
failure. This complements verify-render-graph-declaration-storage.py, which
compiles the full production storage types. CPU timings are fixture timings.
No dependencies are downloaded and no production source is edited.
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
SOURCE = Path("Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp")
HEADER = SOURCE.with_suffix(".h")
BASELINE = "2e08a1929ffc911610ecfb48e7be50a5a8b3d317"


def block(source, anchor, semicolon=False):
    start = source.index(anchor)
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: ''.join('\n' if char == '\n' else ' ' for char in match.group()), source)
    begin = masked.index('{', start)
    depth = 0
    for index in range(begin, len(masked)):
        depth += (masked[index] == '{') - (masked[index] == '}')
        if depth == 0:
            end = masked.index(';', index + 1) + 1 if semicolon else index + 1
            return f'#line {source.count(chr(10), 0, start) + 1} "{anchor.split(chr(10))[0]}"\n' + source[start:end]
    raise ValueError(f"Unclosed production block: {anchor}")


def generate(header, source, fixture):
    types = '\n'.join(block(header, name, True) for name in (
        "struct RepeatedPhase\n", "struct RepeatedPhaseView\n", "struct ResourceDeclaration\n",
        "struct PassDeclaration\n", "struct DeclarationPacket\n"))
    copy = block(header, "template<class T>\n    std::span<const T> CopyPacketValues")
    methods = []
    for name in ("JoinDeclarationPackets", "DispatchDeclarationPackets", "BeginDeclarationPackets",
                 "NextDeclarationPacket", "FinishDeclarationPackets", "CancelDeclarationPackets"):
        methods.append(block(source, f"void EnhancedRenderGraph::{name}("))
    materialize = block(source, "void EnhancedRenderGraph::MaterializeDeclarationPacket(")
    materialize = materialize.replace("\nvoid EnhancedRenderGraph::MaterializeDeclarationPacket(",
                                      "\nvoid EnhancedRenderGraph::MaterializeBody(", 1)
    work = block(source, "struct EnhancedRenderGraph::DeclarationWork\n", True)
    helpers = block(source, "void CopyPassUsages(")
    return fixture.replace("/* GRAPH_TYPES */", types).replace("/* GRAPH_COPY */", copy) + '\n' + work + \
        '\n' + helpers + '\n' + materialize + '\n' + '\n'.join(methods)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--enkits", type=Path, required=True)
    parser.add_argument("--baseline-ref", default=BASELINE)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("a C++23 compiler is required")
    enki = args.enkits.resolve()
    if (enki / "src/TaskScheduler.cpp").is_file():
        enki /= "src"
    if not all((enki / name).is_file() for name in ("TaskScheduler.cpp", "TaskScheduler.h")):
        parser.error("--enkits must be a local enkiTS checkout or its src directory")
    probe = Path(__file__).with_name("render_graph_packet_dispatch_probe.cpp")
    utility = Path("Engine/Utility_Framework")
    render = Path("Engine/RenderEngine")
    mirrored = [utility / name for name in ("StandardLibrary.h", "JobScheduler.h", "JobScheduler.cpp",
                "ThreadPool.h", "ThreadPool.cpp", "JobGraph.h")]
    mirrored += [render / name for name in ("RenderArena.h", "RenderArena.cpp", "RenderAllocator.h")]
    inputs = [ROOT / SOURCE, ROOT / HEADER, probe, Path(__file__).resolve()] + [ROOT / p for p in mirrored]
    snapshots = {path: path.read_bytes() for path in inputs}
    baseline_commit = subprocess.check_output(["git", "rev-parse", args.baseline_ref + "^{commit}"],
                                              cwd=ROOT, text=True).strip()
    baseline = {p: subprocess.check_output(["git", "show", f"{baseline_commit}:{p.as_posix()}"], cwd=ROOT)
                for p in (HEADER, SOURCE)}
    fixture = snapshots[probe].decode().split("// PACKET_FIXTURE_BEGIN\n", 1)[1].split("// PACKET_FIXTURE_END", 1)[0]
    generated = {}
    for name, header, source in (
        ("baseline", baseline[HEADER], baseline[SOURCE]),
        ("current", snapshots[ROOT / HEADER], snapshots[ROOT / SOURCE])):
        generated[name] = generate(header.decode("utf-8-sig"), source.decode("utf-8-sig"), fixture)
    with tempfile.TemporaryDirectory(prefix="creator-declaration-packets-") as directory:
        target = Path(directory)
        mirror = target / "mirror"
        for path in mirrored:
            destination = mirror / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(snapshots[ROOT / path])
        shim = mirror / "ThirdParty/mimalloc/include/mimalloc.h"
        shim.parent.mkdir(parents=True)
        shim.write_text("""#pragma once
#include <cstdlib>
#include <algorithm>
inline void* mi_malloc(std::size_t size) { return std::malloc(size); }
inline void* mi_malloc_aligned(std::size_t size, std::size_t alignment)
{ void* value{}; return posix_memalign(&value, std::max(alignment, sizeof(void*)), size) ? nullptr : value; }
inline void mi_free(void* value) { std::free(value); }
""")
        include = target / "include/enkiTS"
        include.mkdir(parents=True)
        shutil.copyfile(enki / "TaskScheduler.h", include / "TaskScheduler.h")
        (target / "PacketProduction.inl").write_text('\n'.join(
            f'namespace {name}\n{{\n{code}\n}}' for name, code in generated.items()))
        (target / "probe.cpp").write_bytes(snapshots[probe])
        executable = target / "packet-probe"
        command = [compiler, "-std=c++23", "-O2", "-g", "-pthread", "-DCE_CONTRACT_STANDALONE=1",
                   "-DCE_SHIPPING=0", "-Wall", "-Wextra", "-Werror", "-Wno-unknown-pragmas",
                   "-I", str(target), "-I", str(mirror), "-I", str(include.parent),
                   str(target / "probe.cpp"), str(mirror / utility / "JobScheduler.cpp"),
                   str(mirror / utility / "ThreadPool.cpp"), str(mirror / render / "RenderArena.cpp"),
                   str(enki / "TaskScheduler.cpp"), "-o", str(executable)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        print("Compiling exact packet bodies + production arena, JobScheduler, ThreadPool and local enkiTS", flush=True)
        subprocess.run(command, check=True, timeout=180)
        environment = os.environ.copy()
        if args.sanitize:
            environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
            environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        result = subprocess.run([str(executable), "--sanitize" if args.sanitize else "--timing"],
                                text=True, capture_output=True, env=environment, timeout=90)
        print(result.stdout, end="")
        print(result.stderr, end="")
        result.check_returncode()
    for path, data in snapshots.items():
        if path.read_bytes() != data:
            raise RuntimeError(f"source changed during verification: {path}")
    if args.output:
        evidence = {"scope": __doc__, "baseline_commit": baseline_commit, "sanitized": args.sanitize,
                    "asan_options": environment.get("ASAN_OPTIONS", ""),
                    "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
                    "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(data).hexdigest()
                                      for p, data in snapshots.items()},
                    "baseline_sha256": {str(p): hashlib.sha256(data).hexdigest() for p, data in baseline.items()},
                    "enkits_sha256": {name: hashlib.sha256((enki / name).read_bytes()).hexdigest()
                                      for name in ("TaskScheduler.cpp", "TaskScheduler.h")},
                    "results": [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]}
        args.output.write_text(json.dumps(evidence, indent=2) + '\n')


if __name__ == "__main__":
    main()
