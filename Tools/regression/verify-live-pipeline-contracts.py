#!/usr/bin/env python3
"""Run actual feature contracts and graph declarations through a portable CPU fixture.

EnhancedRenderPass.h contains a large platform scene/material payload. Only that
header is substituted in a temporary include tree; feature and graph sources are
unchanged. This does not test native passes, Editor rendering or GPU correctness.
"""
import argparse
import json
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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    root = args.source_root.resolve()
    replacements = {
        Path("Engine/RenderEngine/Render/Graph/EnhancedRenderPass.h"):
            (root / "Tools/regression/fixtures/live-pipeline-portable-pass.h").read_bytes(),
    }
    # Copy the unchanged production feature files so quoted relative includes
    # consistently traverse the temporary Graph header boundary on all compilers.
    for name in ("EnhancedLivePipelineDesc.h", "EnhancedLivePipelineDesc.cpp", "RenderFeatureContributor.h"):
        path = Path("Engine/RenderEngine/Render/Core") / name
        replacements[path] = (root / path).read_bytes()
    with tempfile.TemporaryDirectory(prefix="live-pipeline-contracts-") as temporary:
        source = Path(temporary) / "source"
        mirror(root, source, replacements)
        command = [sys.executable, str(root / "Tools/regression/verify-render-graph-program.py"),
                   "--source-root", str(source), "--compiler", args.compiler,
                   "--probe", "Tools/regression/live_pipeline_contract_probe.cpp",
                   "--extra-source", "Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp"]
        if args.sanitize:
            command.append("--sanitize")
        result = subprocess.run(command, cwd=root, text=True, capture_output=True)
        if result.returncode:
            sys.stderr.write(result.stdout + result.stderr)
            raise SystemExit(result.returncode)
        report = json.loads(result.stdout)
        report["scope"] = "production feature contracts, contributor registry and graph declarations; mocked native services and frame payload"
        report["boundaries"] = ["Tools/regression/fixtures/live-pipeline-portable-pass.h",
                                "Tools/regression/fixtures/render-graph-portable-services.h",
                                "Tools/regression/fixtures/render-graph-portable-platform.cpp"]
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
