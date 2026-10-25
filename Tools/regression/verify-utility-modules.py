"""Guard utility module ownership, naming, and direct native dependencies."""
from pathlib import Path
import runpy
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
MODULE_SOURCES = runpy.run_path(str(ROOT / "Tools/build/generate-module-adapters.py"))["MODULE_SOURCES"]
MODULES = {
    "DynamicBitset": rb"\bce::dynamic_bitset\b",
    "WorkPartition": rb"\bce::partition_contiguous_work\b",
    "SpinLock": rb"\bspin_lock\b",
    "Stopwatch": rb"\bce::stopwatch\b",
    "RingBuffer": rb"\bce::ring_buffer\b",
    "BoundedMemory": rb"\bce::(?:linear_arena|stack_arena|fixed_pool)\b",
    "Base64": rb"\bce::base64::(?:encode|decode)\b",
    "ContentAbi": rb"\bce::content_abi::(?:version|token)\b",
    "EngineMode": rb"\bEngineMode::|\bEngineRunMode\b",
    "Progress": rb"\bce::progress::",
    "JobGraph": rb"\bjob_graph_(?:node|node_timing|diagnostics)\b|\bjob_timestamp_ns\b",
    "LayerCatalog": rb"\bLayerCatalog\b|\bcatalog_snapshot\b|(?:\.|->)catalog\.(?:find|active_mask)\s*\(",
}

def main():
    failures = []
    counts = dict.fromkeys(MODULES, 0)
    for project in ("Engine/Contracts/EngineContracts.vcxproj",
                    "Engine/Contracts/EngineContracts.vcxproj.filters",
                    "Engine/Utility_Framework/Utility_Framework.vcxproj",
                    "Engine/Utility_Framework/Utility_Framework.vcxproj.filters"):
        ET.parse(ROOT / project)
    for stem in MODULES:
        source = ROOT / MODULE_SOURCES[("Creator.Concurrency.JobGraph" if stem == "JobGraph" else f"Creator.Core.{stem}")]
        code = source.read_text(encoding="utf-8-sig")
        module = "Creator.Concurrency.JobGraph" if stem == "JobGraph" else f"Creator.Core.{stem}"
        if f"export module {module};" not in code:
            failures.append(f"{stem}: missing module interface")
        if stem not in ("LayerCatalog", "EngineMode") and re.search(r"\bm_[A-Za-z]|\bkBits\b|\bSpinLock\s*[<(]", code):
            failures.append(f"{stem}: legacy utility naming")
        legacy = {"Stopwatch": "Diagnostics/Benchmark.hpp", "RingBuffer": "Containers/RingBuffer.h", "Base64": "Authoring/AuthoringBase64.h", "ContentAbi": "Packaging/ContentAbi.h", "JobGraph": "Concurrency/JobGraph.h", "LayerCatalog": "Runtime/LayerCatalog.h", "EngineMode": "Runtime/EngineMode.h", "Progress": "Diagnostics/ProgressSink.h"}.get(stem, f"{stem}.h")
        if (ROOT / "Engine/Utility_Framework" / legacy).exists():
            failures.append(f"{stem}: duplicate header definition")
    for legacy in ("LinearArena", "StackArena", "FixedPool"):
        if (ROOT / f"Engine/Utility_Framework/Memory/{legacy}.h").exists():
            failures.append(f"{legacy}: duplicate bounded-memory header definition")
    paths = subprocess.check_output(["git", "ls-files", "-co", "--exclude-standard", "--",
                                    "Engine", "Editor", "Player", "HeadlessHost", "Tools"], cwd=ROOT, text=True).splitlines()
    inline_owners = {
        "Engine/Utility_Framework/Diagnostics/ProgressSink.cpp": "Engine/Utility_Framework/Diagnostics/ProgressSink.cpp",
        "Engine/RenderEngine/Experiment/Cooked/CookedModelStreaming.inl": "Engine/RenderEngine/Experiment/Cooked/CookedModelSubAssetCodec.cpp",
        "Tools/regression/support/MeshletBatchDispatchProbe.inl": "Tools/regression/material_raster_surface_probe.cpp",
    }
    for name in set(paths):
        p = ROOT / name
        if p.suffix not in (".h", ".hpp", ".cpp", ".ixx", ".inl") or not p.exists():
            continue
        if "ModuleAdapters" in p.parts or name.startswith("Engine/Contracts/"):
            continue
        code = p.read_bytes()
        for stem, token in MODULES.items():
            module = ("Creator.Concurrency.JobGraph" if stem == "JobGraph" else f"Creator.Core.{stem}")
            # Remove comments and import/adapter spelling before checking actual API use.
            body = re.sub(rb"/\*.*?\*/|//[^\r\n]*", b"", code, flags=re.S)
            body = re.sub(rb"^[ \t]*(?:#include|import)[^\r\n]*", b"", body, flags=re.M)
            if re.search(token, body):
                counts[stem] += 1
                importer = (ROOT / inline_owners[name]).read_bytes() if name in inline_owners else code
                if f"import {module};".encode() not in importer and f"module {module};".encode() not in importer:
                    failures.append(f"{name}: must directly import {module}")
            if re.search(rb'#include\s+"(?![^"\r\n]*ModuleAdapters)[^"\r\n]*\b'+stem.encode()+rb'\.h"', code):
                failures.append(f"{name}: legacy include for {stem}")
        if re.search(rb'#include\s+"(?![^"\r\n]*ModuleAdapters)[^"\r\n]*(?:LinearArena|StackArena|FixedPool)\.h"', code):
            failures.append(f"{name}: legacy bounded-memory include")
        if re.search(rb'\bAuthoring::Base64::|\bCreatorContentAbi::', body) or re.search(rb'#include\s+"[^"\r\n]*AuthoringBase64\.h"', code):
            failures.append(f"{name}: legacy encoding/content ABI API")
        if re.search(rb'(?:\.|->)catalog\.(?:Find|ActiveMask)\s*\(', body):
            failures.append(f"{name}: old inferred catalog snapshot lookup")
        if re.search(rb"\bSpinLock\s*[<(]", body):
            failures.append(f"{name}: old SpinLock type")
        if re.search(rb'#include\s+"[^"\r\n]*Benchmark\.hpp"', code) or re.search(rb'\bBenchmark\s+\w+\s*;|\bRingBuffer\s*<', body):
            failures.append(f"{name}: legacy timing/container API")
    if failures:
        raise SystemExit("\n".join(failures))
    print("PASS: utility modules; direct consumers " + str(counts))

if __name__ == "__main__":
    main()
