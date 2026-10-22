#!/usr/bin/env python3
"""Run actual feature contracts and graph declarations through a portable CPU fixture.

EnhancedRenderPass.h contains a large platform scene/material payload and the
RenderProxy base depends on platform object identity. Those two headers are the
fixture boundaries; feature, light-selection and graph sources are unchanged.
This does not test native passes, Editor rendering or GPU correctness.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def mirror(source: Path, destination: Path, replacements: dict[Path, bytes]) -> None:
    destination.mkdir()
    for child in source.iterdir():
        selected = {path.relative_to(child.name): data for path, data in replacements.items()
                    if path.parts[0] == child.name}
        target = destination / child.name
        if Path(".") in selected:
            target.write_bytes(selected[Path(".")])
        elif selected:
            mirror(child, target, selected)
        else:
            target.symlink_to(child, target_is_directory=child.is_dir())


def run_snapshot(root: Path, source: Path, frozen: dict[Path, bytes], args, feature_ref: str | None) -> dict:
    replacements = dict(frozen)
    replacements.update({
        Path("Engine/RenderEngine/Render/Graph/EnhancedRenderPass.h"):
            frozen[Path("Tools/regression/fixtures/live-pipeline-portable-pass.h")],
        Path("Engine/RenderEngine/RenderProxy.h"):
            frozen[Path("Tools/regression/fixtures/light-packing-portable-proxy-base.h")],
    })
    core = Path("Engine/RenderEngine/Render/Core")
    for name in ("EnhancedLivePipelineDesc.h", "EnhancedLivePipelineDesc.cpp", "RenderFeatureContributor.h",
                 "EnhancedLightPacking.h"):
        path = core / name
        if feature_ref:
            replacements[path] = subprocess.check_output(["git", "show", f"{feature_ref}:{path.as_posix()}"], cwd=root)
    if feature_ref:
        probe = Path("Tools/regression/live_pipeline_contract_probe.cpp")
        replacements[probe] = b"#define CE_FEATURE_ALLOCATION_LEGACY_LIGHT_API 1\n" + frozen[probe]
    mirror(root, source, replacements)
    command = [sys.executable, str(source / "Tools/regression/verify-render-graph-program.py"),
               "--source-root", str(source), "--compiler", args.compiler,
               "--probe", "Tools/regression/live_pipeline_contract_probe.cpp",
               "--extra-source", "Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp"]
    if args.sanitize:
        command.append("--sanitize")
    environment = os.environ.copy()
    allow_allocations = args.allow_warm_allocations or bool(args.compare_feature_core_ref and feature_ref)
    environment["CE_LIVE_CONTRACT_ALLOW_ALLOCATIONS"] = "1" if allow_allocations else "0"
    result = subprocess.run(command, cwd=root, text=True, capture_output=True, env=environment)
    if result.returncode:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(result.returncode)
    report = json.loads(result.stdout)
    lines = report["result"].splitlines()
    report["result"] = lines[-1]
    report["allocation_measurements"] = [json.loads(line) for line in lines if line.startswith("{")]
    report["feature_core_ref"] = feature_ref or "working-tree"
    report["frozen_source_files"] = len(frozen)
    report["source_changed_during_run"] = [str(path) for path, data in frozen.items()
                                           if (root / path).read_bytes() != data]
    report["feature_source_sha256"] = {
        str(path): hashlib.sha256(replacements[path]).hexdigest() for path in replacements if path.parent == core
    }
    report["scope"] = "production feature contracts, light selection and graph declarations; mocked native services and frame payload"
    report["boundaries"] = ["Tools/regression/fixtures/live-pipeline-portable-pass.h",
                            "Tools/regression/fixtures/light-packing-portable-proxy-base.h",
                            "Tools/regression/fixtures/render-graph-portable-services.h",
                            "Tools/regression/fixtures/render-graph-portable-platform.cpp"]
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--allow-warm-allocations", action="store_true",
                        help="record an unfixed baseline without enforcing the warm allocation budget")
    comparison = parser.add_mutually_exclusive_group()
    comparison.add_argument("--feature-core-ref",
                            help="load feature/light Core files from a Git revision for allocation comparison")
    comparison.add_argument("--compare-feature-core-ref",
                            help="run baseline and current Core against one shared frozen Graph/RHI/fixture snapshot")
    args = parser.parse_args()
    root = args.source_root.resolve()
    # Multi-TU builds must see one class layout even while another contributor
    # edits Graph/RHI. Freeze local production headers/sources and fixture inputs
    # together before starting the compiler; third-party inputs are read-only.
    frozen = {}
    for directory in ("Engine/RenderEngine", "Engine/Utility_Framework", "Tools/regression"):
        for path in (root / directory).rglob("*"):
            if path.is_file() and path.suffix in {".h", ".hpp", ".inl", ".cpp", ".py"}:
                frozen[path.relative_to(root)] = path.read_bytes()
    common_digest = hashlib.sha256()
    core = Path("Engine/RenderEngine/Render/Core")
    for path in sorted(frozen):
        if path.parent != core:
            common_digest.update(str(path).encode() + b"\0" + frozen[path])
    with tempfile.TemporaryDirectory(prefix="live-pipeline-contracts-") as temporary:
        if args.compare_feature_core_ref:
            report = {
                "status": "passed",
                "comparison": "same frozen Graph/RHI/fixture inputs; only Core and legacy light API selection differ",
                "baseline": run_snapshot(root, Path(temporary) / "baseline", frozen, args, args.compare_feature_core_ref),
                "current": run_snapshot(root, Path(temporary) / "current", frozen, args, None),
            }
        else:
            report = run_snapshot(root, Path(temporary) / "source", frozen, args, args.feature_core_ref)
        report["common_source_snapshot_sha256"] = common_digest.hexdigest()
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
