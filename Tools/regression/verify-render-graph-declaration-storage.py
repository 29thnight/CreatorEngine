#!/usr/bin/env python3
"""Compile production declaration storage bodies against their pinned baseline.

Resource/Pass/cache declarations and declaration/reset/cache functions are copied
unchanged from EnhancedRenderGraph. RenderArena and neutral RHI value headers are
compiled unchanged. The fixture substitutes only native services, scheduling and
profiling boundaries, and malloc for unavailable mimalloc. It does not compile
the whole engine or measure Windows Debug/native/GPU/frame performance.
No dependencies are downloaded and no production source is modified.
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
HEADER = Path("Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h")
SOURCE = HEADER.with_suffix(".cpp")
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
            end = index + 1
            if semicolon:
                if not masked[end:].lstrip().startswith(';'):
                    raise ValueError(f"Missing declaration terminator: {anchor}")
                end = masked.index(';', end) + 1
            return source[start:end], source.count('\n', 0, start) + 1
    raise ValueError(f"Unclosed production block: {anchor}")


def emit(source, label, anchor, semicolon=False):
    value, line = block(source, anchor, semicolon)
    return f'#line {line} "{label}"\n{value}\n'


def production_graph(header, source, label, fixture):
    types = ''.join(emit(header, label + ".h", anchor, True) for anchor in (
        "struct RGPassUsage\n", "struct RepeatedPhase\n", "struct RepeatedPhaseView\n",
        "struct Resource\n", "struct PhaseBarrierPlan\n", "struct Pass\n",
        "struct ResourceDeclaration\n", "struct PassDeclaration\n", "struct DeclarationPacket\n",
        "struct FinalStateRequirement\n", "struct StructuralSchedule\n"))
    templates = ''.join(emit(header, label + ".h", anchor) for anchor in (
        "template<class T, class... Args>\n    const T* RetainDeclaration",
        "template<class T>\n    std::span<const T> CopyDeclaration",
        "template<class T>\n    std::span<const T> CopyPacketValues"))
    anchors = [
        "void EnhancedRenderGraph::ClearResourceMetadata(",
        "std::string_view EnhancedRenderGraph::CopyPacketName(",
        "void EnhancedRenderGraph::SetDeclarationResourceName(",
        "void EnhancedRenderGraph::AppendDeclarationVersion(",
        "void EnhancedRenderGraph::SetDeclarationUsages(",
        "void EnhancedRenderGraph::MaterializeDeclarationPacket(",
        "void EnhancedRenderGraph::PinDeclarationNames(",
        "void EnhancedRenderGraph::Reset(",
        "bool EnhancedRenderGraph::CheckDeclarationCapacity(",
        "EnhancedRenderGraph::Resource& EnhancedRenderGraph::AppendResource(",
        "RGHandle EnhancedRenderGraph::ImportBuffer(",
        "RGHandle EnhancedRenderGraph::ImportTexture(",
        "RGHandle EnhancedRenderGraph::CreateTexture(",
        "RGHandle EnhancedRenderGraph::CreateBuffer(",
        "RGHandle EnhancedRenderGraph::VersionHandle(",
        "bool EnhancedRenderGraph::ValidVersionHandle(",
        "RGHandle EnhancedRenderGraph::AdvanceVersion(",
        "RGHandle EnhancedRenderGraph::Write(",
        "RGHandle EnhancedRenderGraph::Modify(",
        "RGHandle EnhancedRenderGraph::AtomicMin64(",
        "void EnhancedRenderGraph::RequireImportedFinalState(",
        "EnhancedRenderGraph::Pass& EnhancedRenderGraph::AppendPass(",
        "void EnhancedRenderGraph::ClearPassMetadata(",
        "RGPassId EnhancedRenderGraph::InitializePass(",
        "bool EnhancedRenderGraph::MatchesStructuralSchedule(",
        "void EnhancedRenderGraph::CaptureStructuralDeclarations("]
    for name in ("AddPass", "AddSplitPass", "AddRepeatedPass", "AddRepeatedPassView"):
        for match in re.finditer(r"RGPassId EnhancedRenderGraph::" + name + r"\(", source):
            end = source.index('{', match.start())
            anchors.append(source[match.start():end].rstrip())
    declarations = []
    definitions = []
    for anchor in anchors:
        value, line = block(source, anchor)
        signature = value[:value.index('{')].strip().replace("EnhancedRenderGraph::", "")
        signature = signature.replace("bool allowUnwritten)", "bool allowUnwritten = false)")
        signature = signature.replace("std::string_view descriptorName)", "std::string_view descriptorName = {})")
        if " MaterializeDeclarationPacket(" in signature:
            signature = "static " + signature
        declarations.append(signature + ";")
        definitions.append(f'#line {line} "{label}.cpp"\n{value}\n')
    helpers = ''.join(emit(source, label + ".cpp", anchor, semicolon) for anchor, semicolon in (
        ("class ImportedIndexCache final", True),
        ("void ResetImportedIndices(", False),
        ("bool RememberImportedIndex(", False),
        ("template<class Values>\n    void ClearNonempty(", False),
        ("uint64_t NextResourceEpoch(", False)))
    usage_copy = emit(source, label + ".cpp", "void CopyPassUsages(")
    result = fixture.replace("/* GRAPH_TYPES */", types).replace("/* GRAPH_TEMPLATES */", templates)
    result = result.replace("/* GRAPH_DECLARATIONS */", '\n'.join(declarations))
    return helpers + result + usage_copy + '\n'.join(definitions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--baseline-ref", default=BASELINE)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("a GCC/Clang C++23 compiler is required")
    root = args.source_root.resolve()
    mirrored = [Path("Engine/RenderEngine") / name for name in
                ("RenderArena.h", "RenderArena.cpp", "RenderAllocator.h")]
    mirrored += [Path("Engine/RenderEngine/RHI") / name for name in
                 ("RHIHandle.h", "RHIResourceState.h", "RHIFormat.h")]
    mirrored += [Path("Engine/Utility_Framework") / name for name in
                 ("StandardLibrary.h", "DynamicBitset.h")]
    probe = Path(__file__).with_name("render_graph_declaration_storage_probe.cpp")
    paths = [root / HEADER, root / SOURCE, probe, Path(__file__).resolve()] + [root / path for path in mirrored]
    snapshots = {path: path.read_bytes() for path in paths}
    hashes = {str(path.relative_to(root) if path.is_relative_to(root) else path):
              hashlib.sha256(data).hexdigest() for path, data in snapshots.items()}
    baseline_commit = subprocess.check_output(
        ["git", "rev-parse", args.baseline_ref + "^{commit}"], cwd=root, text=True).strip()
    baseline = {path: subprocess.check_output(["git", "show", f"{baseline_commit}:{path.as_posix()}"], cwd=root)
                for path in (HEADER, SOURCE)}
    probe_text = snapshots[probe].decode("utf-8")
    fixture = probe_text.split("// GRAPH_FIXTURE_BEGIN\n", 1)[1].split("// GRAPH_FIXTURE_END", 1)[0]
    current_header = snapshots[root / HEADER].decode("utf-8-sig")
    globals_text = ''.join(emit(current_header, HEADER.as_posix(), anchor, True) for anchor in (
        "enum class RGResourceKind", "struct RGHandle\n", "struct RGPassId\n", "enum class RGAccessMode",
        "enum class RGSchedulingMode", "enum class RGOrderPolicy", "enum class RGMeasurementDomain",
        "struct RGTextureDesc\n", "struct RGBufferDesc\n"))
    generated = {}
    for namespace, header, source, label in (
        ("current", current_header, snapshots[root / SOURCE].decode("utf-8-sig"), SOURCE.with_suffix("").as_posix()),
        ("baseline", baseline[HEADER].decode("utf-8-sig"), baseline[SOURCE].decode("utf-8-sig"),
         baseline_commit + "/" + SOURCE.with_suffix("").as_posix())):
        generated[namespace] = production_graph(header, source, label, fixture)
    with tempfile.TemporaryDirectory(prefix="render-graph-declaration-storage-") as directory:
        target = Path(directory)
        mirror = target / "mirror"
        for path in mirrored:
            destination = mirror / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(snapshots[root / path])
        shim = mirror / "ThirdParty/mimalloc/include/mimalloc.h"
        shim.parent.mkdir(parents=True)
        shim.write_text("""#pragma once
#include <cstddef>
void* graph_declaration_allocate(std::size_t size, std::size_t alignment);
void graph_declaration_free(void* pointer) noexcept;
inline void* mi_malloc(std::size_t size) { return graph_declaration_allocate(size, alignof(std::max_align_t)); }
inline void* mi_malloc_aligned(std::size_t size, std::size_t alignment)
{ return graph_declaration_allocate(size, alignment); }
inline void mi_free(void* pointer) { graph_declaration_free(pointer); }
""", encoding="utf-8")
        (target / "GraphGlobals.inl").write_text(globals_text, encoding="utf-8")
        (target / "GraphProduction.inl").write_text(''.join(
            f'namespace {namespace}\n{{\n{value}\n}}\n' for namespace, value in generated.items()), encoding="utf-8")
        (target / "probe.cpp").write_bytes(snapshots[probe])
        executable = target / "declaration-storage"
        command = [compiler, "-std=c++23", "-O2", "-g", "-pthread", "-DCE_CONTRACT_STANDALONE=1",
                   "-DCE_SHIPPING=1", "-Wall", "-Wextra", "-Werror", "-I", str(target), "-I", str(mirror),
                   str(target / "probe.cpp"), str(mirror / "Engine/RenderEngine/RenderArena.cpp"),
                   "-o", str(executable)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        print("Compiling exact declaration/cache bodies and production arena headers", flush=True)
        subprocess.run(command, check=True, timeout=180)
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
        evidence = {"source_sha256": hashes, "baseline_commit": baseline_commit,
                    "baseline_sha256": {str(path): hashlib.sha256(data).hexdigest() for path, data in baseline.items()},
                    "generated_sha256": {key: hashlib.sha256(value.encode()).hexdigest() for key, value in generated.items()},
                    "sanitized": args.sanitize,
                    "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
                    "boundary": "exact declaration/cache bodies and arena; CPU RHI fixture; malloc shim; no native/GPU/frame timing",
                    "output": result.stdout.splitlines()}
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
