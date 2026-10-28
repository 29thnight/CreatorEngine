"""Check the core value APIs have one definition and native import consumers."""
from pathlib import Path
import re
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MODULE_SOURCES = runpy.run_path(str(ROOT / "Tools/build/generate-module-adapters.py"))["MODULE_SOURCES"]
MODULES = {
    "Uuid": "Creator.Core.Uuid",
    "LayerTypes": "Creator.Core.Layers",
    "BitFlag": "Creator.Core.BitFlag",
}


def main():
    failures = []
    for retired in (
        "Engine/Contracts/Creator.Core.HashingString.ixx",
        "Tools/build/ModuleAdapters/Creator.Core.HashingString.h",
        "Engine/Utility_Framework/HashingString.h",
    ):
        if (ROOT / retired).exists():
            failures.append(f"Retired owning hash cache restored: {retired}")
    for header, module in MODULES.items():
        if (ROOT / f"Engine/Utility_Framework/{header}.h").exists():
            failures.append(f"Obsolete native header restored: {header}.h")
        source = (ROOT / MODULE_SOURCES[module]).read_text(encoding="utf-8-sig")
        if f"export module {module};" not in source or "#include" in source:
            failures.append(f"{module}: expected direct module definitions without includes")

    names = subprocess.check_output(
        ["git", "ls-files", "-co", "--exclude-standard", "--", "Engine", "Editor", "Player", "HeadlessHost", "Tools"],
        cwd=ROOT, text=True,
    ).splitlines()
    old_include = re.compile(rb'^\s*#\s*include\s*["<](?:[^">]*/)?(?:Uuid|LayerTypes|BitFlag|HashingString)\.h[">]', re.M)
    tokens = {
        "Creator.Core.Uuid": rb"\buuid::",
        "Creator.Core.Layers": rb"\bce::layers::",
        "Creator.Core.BitFlag": rb"\bbit_flag\b",
    }
    consumers = 0
    for name in sorted(set(names)):
        path = ROOT / name
        if not path.is_file() or path.suffix not in {".cpp", ".h", ".hpp", ".inl", ".ixx"}:
            continue
        source = path.read_bytes()
        code = re.sub(rb"/\*.*?\*/|//[^\r\n]*", b"", source, flags=re.S)
        if re.search(rb"\bHashingString\b|\bGetHashedName\b|\bUuid::|\bUuid16\b", code):
            failures.append(f"{name}: legacy string/UUID utility API")
        if re.search(rb"\b(?:class|struct)\s+(?:BitFlag|KeyBitFlag|MouseBitFlag|GamePadBitFlag)\b", code):
            failures.append(f"{name}: utility type must use snake_case")
        if re.search(rb"m_cur(?:KeyStates|MouseState|PadState)(?:\[[^\]]+\])?\s*\.\s*(?:Set|Clear|Reset|Test)\s*\(", code):
            failures.append(f"{name}: input value consumers must call snake_case utility members")
        if old_include.search(source):
            failures.append(f"{name}: obsolete core value header include")
        if path.suffix not in {".cpp", ".h", ".hpp"} or not name.startswith(("Engine/", "Editor/", "Player/", "HeadlessHost/")):
            continue
        for module, token in tokens.items():
            if re.search(token, code):
                consumers += 1
                if f"import {module};".encode() not in source:
                    failures.append(f"{name}: direct use must import {module}")
    runpy.run_path(str(ROOT / "Tools/build/generate-module-adapters.py"))["generate"](check=True)
    for module in ("Creator.Core.Uuid", "Creator.Core.BitFlag"):
        source = (ROOT / MODULE_SOURCES[module]).read_text(encoding="utf-8")
        code = re.sub(r"/\*.*?\*/|//[^\r\n]*", "", source, flags=re.S)
        if re.search(r"\bm_[A-Za-z]|\b(?:Set|Clear|Toggle|Test|Update|Finish|Parse|ToString|FromName|IsNil)\s*\(", code):
            failures.append(f"{module}: utility member naming violation")
    for name, field in (
        ("Engine/SceneRuntime/Object.h", "m_name"),
        ("Engine/SceneRuntime/Entity.h", "m_tag"),
        ("Engine/SceneRuntime/Scene.h", "m_sceneName"),
        ("Engine/Utility_Framework/Runtime/RenderSettings/ColorGradingPassSetting.h", "textureFilePath"),
    ):
        if not re.search(rb"std::string\s+" + field.encode() + rb"\b", (ROOT / name).read_bytes()):
            failures.append(f"{name}: {field} must own its required text with std::string")
    if failures:
        raise SystemExit("\n".join(failures))
    adapter_count = len(runpy.run_path(str(ROOT / "Tools/build/generate-module-adapters.py"))["MODULES"])
    print(f"PASS: 3 canonical value modules; {consumers} direct native dependencies; {adapter_count} adapters; plain string storage")


if __name__ == "__main__":
    main()
