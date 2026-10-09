#!/usr/bin/env python3
"""Fail closed on retired audio SDK source/project/deployment dependencies."""
from pathlib import Path
import argparse
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary-root", type=Path,
                        help="Also reject middleware/miniaudio DLLs in an already-built payload (not a PE import audit)")
    args = parser.parse_args()
    failures = []
    checks = 0
    sdk = ROOT / "ThirdParty/Fmod"
    checks += 1
    if sdk.exists():
        failures.append("Retired ThirdParty/Fmod directory still exists")
    projects = ["Engine/SceneRuntime/SceneRuntime.vcxproj", "Editor/Editor.vcxproj",
                "Editor/CreatorEditor.vcxproj", "Player/Player.vcxproj", "Tools/AssetCooker/AssetCooker.vcxproj"]
    for relative in projects:
        path = ROOT / relative
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        checks += 1
        if re.search(r"(?i)fmod(?:L)?_vc\.lib|ThirdParty[\\/]Fmod", text):
            failures.append(f"Retired SDK reference: {relative}")
        tree = ET.fromstring(text)
        names = []
        for node in tree.findall(".//m:ClCompile", NS) + tree.findall(".//m:ClInclude", NS):
            name = node.get("Include")
            if not name or "$(" in name or "*" in name:
                continue
            checks += 1
            names.append((node.tag, name))
            source = path.parent / name.replace("\\", "/")
            if not source.is_file():
                failures.append(f"Missing project source: {relative} -> {name}")
        checks += 1
        if len(names) != len(set(names)):
            failures.append(f"Duplicate source entries: {relative}")
    runtime_project = (ROOT / "Engine/SceneRuntime/SceneRuntime.vcxproj").read_text(encoding="utf-8-sig")
    for source in (ROOT / "Engine/SceneRuntime/Audio").glob("*.cpp"):
        checks += 1
        entry = 'Include="Audio\\' + source.name + '"'
        if runtime_project.count(entry) != 1:
            failures.append(f"Audio source must compile exactly once in SceneRuntime: {source.name}")
    for directory in ("Engine", "Editor", "Player", "ScriptCore"):
        for path in (ROOT / directory).rglob("*"):
            if path.suffix not in {".h", ".hpp", ".cpp", ".cs"}:
                continue
            text = path.read_text(encoding="utf-8-sig", errors="replace")
            # History belongs in docs. Comments do not create an SDK dependency.
            code = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
            checks += 1
            if re.search(r"\bFMOD\s*::|\bFMOD_(?:VECTOR|MODE|RESULT|OK)\b|#\s*include\s*[<\"][^>\"]*fmod", code):
                failures.append(f"Retired SDK consumer: {path.relative_to(ROOT)}")
    for relative in ("Tools/runtime/deploy-runtime.ps1", "Tools/regression/verify-audio-voice-contract.ps1",
                     "Tools/regression/verify-experiment-contract.ps1"):
        checks += 1
        text = (ROOT / relative).read_text(encoding="utf-8-sig")
        if re.search(r"(?i)ThirdParty[\\/]Fmod|fmodL?_vc\.lib", text):
            failures.append(f"Retired deployment/test dependency: {relative}")
    if args.binary_root:
        for file in args.binary_root.rglob("*"):
            checks += 1
            if re.fullmatch(r"(?i)(?:fmod(?:L|studio|studioL)?|miniaudio)\.dll", file.name):
                failures.append(f"Forbidden audio DLL: {file}")
    for failure in failures:
        print("FAIL", failure)
    print(f"AUDIO_RETIREMENT checks={checks} failures={len(failures)}")
    print("Windows full builds, PE import closure, device playback and package smoke are separate gates.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
