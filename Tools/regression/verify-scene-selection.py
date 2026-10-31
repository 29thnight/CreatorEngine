#!/usr/bin/env python3
"""Run production scene selection and revision publication against a reduced host.

SelectReadyInput, its exact-owner comparison, pin table, coverage classification,
readiness scan and revision-publication loop are extracted unchanged. The fixture
uses real ownership_cpp and ce containers; program requests/completion and the
surrounding view/program records are reduced CPU fixtures. This does not build the
native SceneHost translation unit or validate GPU program preparation/submission.

--source-ref <pre-change-ref> --behavior-only checks the pre-change behavior. Omitting
--behavior-only also enforces selection work proportional to unique slots, which
intentionally fails on the pre-optimization per-draw implementation.
Readiness counts cover selection decisions after polling, excluding work inside
the reduced RequestProgram/PollPrograms fixtures.
"""

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def extract(source, path, anchor, *, terminator=False, body_only=False, after=None):
    offset = source.index(after) if after else 0
    start = source.index(anchor, offset)
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: "".join("\n" if char == "\n" else " " for char in match.group()), source)
    begin = masked.index("{", start)
    depth = 0
    for index in range(begin, len(masked)):
        depth += (masked[index] == "{") - (masked[index] == "}")
        if depth == 0:
            end = masked.index(";", index) + 1 if terminator else index + 1
            if body_only:
                start, end = begin + 1, index
            line = source.count("\n", 0, start) + 1
            return f'#line {line} "{path.as_posix()}"\n{source[start:end]}\n'
    raise ValueError(f"Unclosed production block: {anchor}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--source-ref", help="Read production blocks from this local git revision")
    parser.add_argument("--behavior-only", action="store_true",
                        help="Report selection work without enforcing the unique-slot work budget")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    root = args.source_root.resolve()
    render = root / "Engine/RenderEngine"
    host = render / "MaterialGraphSceneHost.cpp"
    pins = render / "MaterialGraphInstancePins.h"
    coverage = render / "MaterialGraphSceneCoverage.h"
    render_pass = render / "Render/Graph/EnhancedRenderPass.h"
    probe = root / "Tools/regression/scene_selection_probe.cpp"
    paths = [host, pins, coverage, render_pass, probe,
             root / "ThirdParty/ownership_cpp/include/own/ownership.hpp"]
    before = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}

    def read(path):
        if args.source_ref:
            relative = path.relative_to(root).as_posix()
            return subprocess.run(["git", "show", f"{args.source_ref}:{relative}"], cwd=root,
                                  capture_output=True, text=True, check=True).stdout
        return path.read_text(encoding="utf-8-sig")

    host_text, pin_text, coverage_text, pass_text = map(read, [host, pins, coverage, render_pass])
    with tempfile.TemporaryDirectory(prefix="creator-scene-selection-") as temporary:
        output = Path(temporary)
        sections = {
            "SceneSelectionPins.inl": extract(pin_text, pins, "class InstanceFramePins final", terminator=True),
            "SceneSelectionCoverageData.inl": extract(pass_text, render_pass, "struct EnhancedMaterialCoverage", terminator=True),
            "SceneSelectionCoverageEnum.inl": extract(coverage_text, coverage, "enum class SceneCoverage", terminator=True),
            "SceneSelectionCoverage.inl": extract(coverage_text, coverage, "inline bool ClassifySceneCoverage("),
            "SceneSelectionIdentity.inl": extract(host_text, host, "template<class Left, class Right>\n        bool SamePinnedObject("),
            "SceneSelectionSlot.inl": extract(host_text, host, "struct SceneHost::Slot", terminator=True),
            "SceneSelectionReady.inl": extract(host_text, host, "bool SceneHost::IsProgramReady(", body_only=True),
            "SceneSelection.inl": extract(host_text, host, "bool SceneHost::SelectReadyInput("),
            "SceneSelectionPublication.inl": extract(host_text, host, "for (const auto& draw : frame->input->Draws())",
                after="BoundShadowViews(shadowHistories_, ce::pair{view.sceneEpoch, view.viewId});"),
        }
        for name, content in sections.items():
            (output / name).write_text(content, encoding="utf-8")
        executable = output / "scene-selection"
        command = [args.compiler, "-std=c++23", "-O1", "-g", "-pthread", "-DCE_CONTRACT_STANDALONE=1",
                   "-DCE_SHIPPING=1", "-Wno-attributes", "-Wno-nonnull", "-I" + str(root),
                   "-I" + str(output), "-I" + str(root / "Engine/Utility_Framework"),
                   str(probe), "-o", str(executable)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        subprocess.run(command, check=True, timeout=120)
        subprocess.run([str(executable)] + (["--behavior-only"] if args.behavior_only else []),
                       check=True, timeout=60)
    if before != {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}:
        raise RuntimeError("Production or probe source changed during verification")


if __name__ == "__main__":
    main()
