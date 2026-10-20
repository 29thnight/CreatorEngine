#!/usr/bin/env python3
"""Execute exact history-HZB declaration bodies against the pinned baseline.

Production class/layout declarations and ReadHistory, ImportHistory, Declare,
AddReadUsages, BindingInputs, Bindings and view/epoch guards are extracted without
rewriting their bodies. Only graph/RHI services, pipeline initialization and GPU
retirement are fixtures. This verifies CPU ordering, lifetime guards, descriptors
and warm allocation requests; it does not execute a native graph or GPU shader.
Zero/over-capacity or changed-size histories are intentional new rejection cases.
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
SOURCE = Path("Engine/RenderEngine/GpuGeometryOcclusion.cpp")
HEADER = SOURCE.with_suffix(".h")
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
    probe = Path(__file__).with_name("hzb_declaration_cpu_probe.cpp")
    inputs = [root / SOURCE, root / HEADER, probe, Path(__file__).resolve()]
    snapshots = {path: path.read_bytes() for path in inputs}
    baseline = {path: subprocess.check_output(["git", "show", f"{args.baseline_ref}:{path.as_posix()}"], cwd=root)
                for path in (SOURCE, HEADER)}
    with tempfile.TemporaryDirectory(prefix="hzb-declaration-") as directory:
        target = Path(directory)
        extracted_hashes = {}
        anchors = [
            ("struct SharedGeometryOcclusionConstants", True),
            ("bool SameOcclusionHandle(", False),
            ("struct GpuGeometryOcclusion::DeclarationLayout", True),
            ("GpuGeometryOcclusion::ViewKey GpuGeometryOcclusion::CaptureView(", False),
            ("std::uint32_t GpuGeometryOcclusion::LevelCount(", False),
            ("void GpuGeometryOcclusion::Pyramid::CheckCurrent(", False),
            ("std::shared_ptr<const GpuGeometryOcclusion::Pyramid> GpuGeometryOcclusion::ReadHistory(", False),
            ("void GpuGeometryOcclusion::Pyramid::ImportHistory(", False),
            ("void GpuGeometryOcclusion::Pyramid::RequireView(", False),
            ("void GpuGeometryOcclusion::Pyramid::RequireCurrentView(", False),
            ("void GpuGeometryOcclusion::Pyramid::Declare(", False),
            ("void GpuGeometryOcclusion::Pyramid::AddReadUsages(", False),
            ("std::array<RHIBindingDesc, GpuGeometryOcclusion::kMaximumLevels> GpuGeometryOcclusion::Pyramid::BindingInputs(", False),
            ("RHIBindingTable GpuGeometryOcclusion::Pyramid::Bindings(", False),
        ]
        for prefix, source_bytes, header_bytes in (
                ("Baseline", baseline[SOURCE], baseline[HEADER]),
                ("Current", snapshots[root / SOURCE], snapshots[root / HEADER])):
            label = f"{args.baseline_ref}/{SOURCE}" if prefix == "Baseline" else str(root / SOURCE)
            header_label = f"{args.baseline_ref}/{HEADER}" if prefix == "Baseline" else str(root / HEADER)
            source = source_bytes.decode("utf-8-sig")
            header = header_bytes.decode("utf-8-sig")
            value = extract(header, header_label, "class GpuGeometryOcclusion final", True)
            value += "".join(extract(source, label, anchor, semicolon) for anchor, semicolon in anchors)
            name = f"{prefix}Hzb.inl"
            (target / name).write_text(value, encoding="utf-8")
            extracted_hashes[name] = hashlib.sha256(value.encode()).hexdigest()
        compiled_probe = target / probe.name
        compiled_probe.write_bytes(snapshots[probe])
        executable = target / "hzb-declaration"
        command = [compiler, "-std=c++23", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                   "-Wno-mismatched-new-delete", "-I", str(target), str(compiled_probe), "-o", str(executable)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        print("Compiling exact baseline/current HZB declaration bodies and deferred repeated-pass callbacks", flush=True)
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
            "scope": __doc__, "source_sha256": {str(path): hashlib.sha256(data).hexdigest()
                                                for path, data in snapshots.items()},
            "extracted_sha256": extracted_hashes, "baseline_ref": args.baseline_ref,
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
