#!/usr/bin/env python3
"""Compile and execute the actual backend-neutral graph translation units.

The only substitutes are native device services, mimalloc/platform environment
allocation and an explicitly rejecting unused worker-wait boundary. Native GPU,
parallel recording and performance are not covered. No dependencies downloaded.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def compile_probe(root, compiler, executable, probe=None, extra_sources=(), sanitize=False):
    sources = [
        "Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp",
        "Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp",
        "Engine/RenderEngine/Render/Graph/EnhancedRenderGraphSubresources.cpp",
        "Engine/RenderEngine/RenderArena.cpp",
        "Tools/regression/fixtures/render-graph-portable-platform.cpp",
        str(probe or Path("Tools/regression/render_graph_program_probe.cpp")),
    ] + [str(source) for source in extra_sources]
    # Older source snapshots keep the implementation in RenderArena.cpp.
    utility_arena = "Engine/Utility_Framework/Memory/Arena.cpp"
    if (root / utility_arena).is_file():
        sources.append(utility_arena)
    command = [compiler, "-std=c++23", "-O1", "-g", "-pthread", "-ffunction-sections", "-fdata-sections",
               "-DCE_CONTRACT_STANDALONE=1", "-DCE_SHIPPING=1", "-D_dupenv_s=mi_dupenv_s",
               "-include", str(root / "ThirdParty/mimalloc/include/mimalloc.h"),
               "-I", str(root), "-I", str(root / "ThirdParty/Mathematics/include"),
               "-I", str(root / "ThirdParty/ownership_cpp/include"),
               "-I", str(root / "Engine/RenderEngine"), "-I", str(root / "Engine/Utility_Framework"),
               *[str(root / source) for source in sources], "-Wl,--gc-sections", "-o", str(executable)]
    if sanitize:
        command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=root, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--extra-source", type=Path, action="append", default=[])
    args = parser.parse_args()
    root = args.source_root.resolve()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("a GCC/Clang C++23 compiler is required")
    with tempfile.TemporaryDirectory(prefix="render-graph-program-") as temporary:
        executable = Path(temporary) / "probe"
        compile_probe(root, compiler, executable, args.probe, args.extra_source, args.sanitize)
        result = subprocess.run([str(executable)], cwd=root, text=True, capture_output=True)
        if result.returncode:
            print(result.stdout, end="")
            print(result.stderr, end="")
            result.check_returncode()
        print(json.dumps({"status": "passed", "sanitized": args.sanitize,
                          "scope": "production CPU graph compiler and bindings; mocked native services",
                          "result": result.stdout.strip()}, indent=2))


if __name__ == "__main__":
    main()
