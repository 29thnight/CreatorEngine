#!/usr/bin/env python3
"""Compare exact SSGI Declare bodies and deferred callbacks against the pinned baseline.

The graph/RHI boundary records declarations, uploads, bindings and commands; it
does not render pixels or model driver cost. The production SSGI class, parameter
structs, Declare/ResetHistory bodies and graph ownership templates are extracted
without rewriting their bodies. Production RenderArena is compiled with only a
malloc allocator-service shim. Allocation counts cover this CPU boundary after
warmup, not native Editor/frame performance. No dependency is downloaded.
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
SOURCE = Path("Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSGIPass.cpp")
HEADER = SOURCE.with_suffix(".h")
GRAPH = Path("Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h")
BASELINE = "2e08a1929ffc911610ecfb48e7be50a5a8b3d317"


def block(source, anchor):
    start = source.index(anchor)
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: "".join("\n" if char == "\n" else " " for char in match.group()), source)
    first = masked.index("{", start)
    depth = 0
    for index in range(first, len(masked)):
        depth += (masked[index] == "{") - (masked[index] == "}")
        if depth == 0:
            return start, index + 1
    raise RuntimeError(f"Unclosed source block: {anchor}")


def extract(source, label, anchor, semicolon=False):
    start, end = block(source, anchor)
    if semicolon:
        end = source.index(";", end) + 1
    return f'#line {source.count(chr(10), 0, start) + 1} "{label}"\n{source[start:end]}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--baseline-ref", default=BASELINE)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("C++23 compiler unavailable")
    arena = [Path("Engine/RenderEngine") / name for name in ("RenderArena.cpp", "RenderArena.h", "RenderAllocator.h")]
    arena += [Path("Engine/Utility_Framework") / name for name in
              ("Arena.cpp", "Arena.h", "HeapAllocator.h", "Ownership.h", "LifetimeToken.h")]
    arena += [Path("ThirdParty/ownership_cpp/include/own/ownership.hpp")]
    probe = Path(__file__).with_name("ssgi_declaration_cpu_probe.cpp")
    inputs = [root / path for path in [SOURCE, HEADER, GRAPH] + arena]
    inputs += [probe, Path(__file__).resolve()]
    snapshots = {path: path.read_bytes() for path in inputs}
    hashes = {str(path): hashlib.sha256(data).hexdigest() for path, data in snapshots.items()}
    baseline = {path: subprocess.check_output(["git", "show", f"{args.baseline_ref}:{path.as_posix()}"], cwd=root)
                for path in (SOURCE, HEADER)}
    with tempfile.TemporaryDirectory(prefix="ssgi-declaration-") as directory:
        target = Path(directory)
        mirror = target / "mirror"
        for path in arena:
            destination = mirror / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(snapshots[root / path])
        shim = mirror / "ThirdParty/mimalloc/include/mimalloc.h"
        shim.parent.mkdir(parents=True)
        shim.write_text("""#pragma once
#include <cstdlib>
void ssgi_count_allocation();
inline void* mi_malloc(size_t size) { ssgi_count_allocation(); return std::malloc(size); }
inline void mi_free(void* value) { std::free(value); }
inline void* mi_malloc_aligned(size_t size, size_t alignment)
{
    ssgi_count_allocation();
    void* result{};
    if (posix_memalign(&result, alignment < sizeof(void*) ? sizeof(void*) : alignment,
                      size ? size : 1) != 0) return nullptr;
    return result;
}
""", encoding="utf-8")
        extracted_hashes = {}
        for prefix, data, header in (("Baseline", baseline[SOURCE], baseline[HEADER]),
                                     ("Current", snapshots[root / SOURCE], snapshots[root / HEADER])):
            source = data.decode("utf-8-sig")
            header_source = header.decode("utf-8-sig")
            label = f"{args.baseline_ref}/{SOURCE}" if prefix == "Baseline" else str(root / SOURCE)
            header_label = f"{args.baseline_ref}/{HEADER}" if prefix == "Baseline" else str(root / HEADER)
            params = "".join(extract(source, label, f"    struct {name}", True)
                             for name in ("ResolveParams", "FilterParams", "CompositeParams", "HiZParams", "TraceParams"))
            values = {
                f"{prefix}SsgiClass.inl": extract(header_source, header_label, "class EnhancedSSGIPass", True),
                f"{prefix}SsgiParams.inl": params,
                f"{prefix}SsgiDeclare.inl": extract(source, label, "void EnhancedSSGIPass::Declare("),
                f"{prefix}SsgiReset.inl": extract(source, label, "void EnhancedSSGIPass::ResetHistory()"),
            }
            for name, value in values.items():
                (target / name).write_text(value, encoding="utf-8")
                extracted_hashes[name] = hashlib.sha256(value.encode()).hexdigest()
        graph = snapshots[root / GRAPH].decode("utf-8-sig")
        ownership = "".join(extract(graph, str(root / GRAPH), anchor) for anchor in (
            "    template<class T, class... Args>\n    const T* RetainDeclaration(",
            "    template<class T>\n    std::span<const T> CopyDeclaration(",
            "    template<class F>\n    ExecuteCallback StoreExecute("))
        (target / "GraphOwnership.inl").write_text(ownership, encoding="utf-8")
        extracted_hashes["GraphOwnership.inl"] = hashlib.sha256(ownership.encode()).hexdigest()
        compiled_probe = target / probe.name
        compiled_probe.write_bytes(snapshots[probe])
        executable = target / "ssgi-declaration"
        command = [compiler, "-std=c++23", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                   "-Wno-mismatched-new-delete", "-I", str(target), "-I", str(mirror),
                   str(compiled_probe), *[str(mirror / path) for path in arena if path.suffix == ".cpp"],
                   "-o", str(executable)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        print("Compiling exact SSGI baseline/current bodies, deferred callbacks and RenderArena", flush=True)
        subprocess.run(command, check=True, timeout=120)
        environment = os.environ.copy()
        if args.sanitize:
            environment.setdefault("ASAN_OPTIONS", "detect_leaks=0")
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120, env=environment)
        print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="")
        result.check_returncode()
        for path, original in snapshots.items():
            if path.read_bytes() != original:
                raise RuntimeError(f"Source changed during validation: {path}")
        evidence = {
            "scope": __doc__, "source_sha256": hashes, "extracted_sha256": extracted_hashes,
            "baseline_ref": args.baseline_ref,
            "baseline_sha256": {str(path): hashlib.sha256(data).hexdigest() for path, data in baseline.items()},
            "sanitizer": "address,undefined; fail-fast; leak detection disabled" if args.sanitize else "none",
            "compiler_command": command, "compiler_exit": 0, "probe_exit": result.returncode,
            "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "results": [json.loads(line) for line in result.stdout.splitlines() if line.strip()],
        }
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
