#!/usr/bin/env python3
"""Execute scene-input reuse contracts with real ownership, containers and mesh identities.

The production pin table, identity comparisons, granular hint lookup and material
borrow method are extracted unchanged. Only the surrounding draw/material payloads
are reduced to the fields these operations use. This does not compile SceneViewInput
or run Windows/GPU preparation; the native material probes cover that integration.
"""

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def extract(path, anchor, terminator=False):
    source = path.read_text(encoding="utf-8-sig")
    start = source.index(anchor)
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: "".join("\n" if char == "\n" else " " for char in match.group()), source)
    begin = masked.index("{", start)
    depth = 0
    for index in range(begin, len(masked)):
        depth += (masked[index] == "{") - (masked[index] == "}")
        if depth == 0:
            end = masked.index(";", index) + 1 if terminator else index + 1
            line = source.count("\n", 0, start) + 1
            return f'#line {line} "{path.as_posix()}"\n{source[start:end]}\n'
    raise ValueError(f"Unclosed production block: {anchor}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    root = args.source_root.resolve()
    render = root / "Engine/RenderEngine"
    pins = render / "MaterialGraphInstancePins.h"
    scene = render / "MaterialGraphSceneInput.cpp"
    scene_header = render / "MaterialGraphSceneInput.h"
    identity = render / "Render/Graph/EnhancedDrawIdentity.h"
    services = render / "RHI/IRenderDeviceServices.h"
    probe = root / "Tools/regression/scene_input_reuse_probe.cpp"
    paths = [pins, scene, scene_header, identity, services, probe,
             render / "Assets/ModelAssetGeneration.h", render / "Assets/ModelMeshDescriptor.h",
             render / "Experiment/Cooked/CookedAssetManifest.h",
             root / "ThirdParty/ownership_cpp/include/own/ownership.hpp"]
    before = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    source = scene.read_text(encoding="utf-8-sig")
    first = source.index("const auto& pins = candidate->modelPins_->meshes;")
    last = source.index("if (const auto* stream = geometry.StreamMetadata())", first)
    lookup = f'#line {source.count(chr(10), 0, first) + 1} "{scene.as_posix()}"\n{source[first:last]}'
    with tempfile.TemporaryDirectory(prefix="creator-scene-input-reuse-") as temporary:
        output = Path(temporary)
        sections = {
            "SceneInputPins.inl": extract(pins, "class InstanceFramePins final", True),
            "SceneInputIdentity.inl": extract(identity, "[[nodiscard]] inline std::size_t GeometryKey(") +
                extract(identity, "[[nodiscard]] inline bool ValidateGeometryIdentities("),
            "SceneInputComparisons.inl": extract(scene, "bool MatchesMeshDescriptor(") +
                extract(scene, "bool MatchesGeometryIdentityOrder("),
            "SceneInputModelHint.inl": lookup,
            "SceneInputBorrow.inl": extract(scene_header, "own::local_view<const Instance> MaterialBorrow("),
            "SceneInputMeshView.inl": extract(services, "struct RHIModelMeshView", True) +
                extract(services, "[[nodiscard]] inline size_t HashModelMeshHandle("),
        }
        for name, text in sections.items():
            (output / name).write_text(text, encoding="utf-8")
        executable = output / "scene-input-reuse"
        command = [args.compiler, "-std=c++23", "-O1", "-g", "-pthread", "-DCE_CONTRACT_STANDALONE=1",
                   "-DCE_SHIPPING=1", "-Wno-attributes", "-Wno-nonnull", "-I" + str(root),
                   "-I" + str(output), "-I" + str(root / "Engine/Utility_Framework"),
                   "-I" + str(root / "ThirdParty/Mathematics/include"), str(probe), "-o", str(executable)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        subprocess.run(command, check=True, timeout=120)
        subprocess.run([str(executable)], check=True, timeout=60)
    if before != {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}:
        raise RuntimeError("Production or probe source changed during verification")


if __name__ == "__main__":
    main()
