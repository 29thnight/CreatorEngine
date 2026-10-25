"""Check text/hash module ownership and shared string utility consumers."""
from pathlib import Path
import re
import runpy
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
MODULE_SOURCES = runpy.run_path(str(ROOT / "Tools/build/generate-module-adapters.py"))["MODULE_SOURCES"]
MODULES = {
    "Creator.Core.Sha256": rb"\bce::hash::(?:sha256|sha256_digest|to_hex)\b",
    "Creator.Core.Strings": rb"\bce::strings::(?:to_lower\w*|ascii_to_lower\w*|equals_ascii_case_insensitive|is_space|trim(?:_view)?|split|tokenize_preserve_quotes|unquote|split_endpoint|remove_whitespace|contains_case_insensitive)\b",
    "Creator.Platform.Windows.Strings": rb"\bce::strings::(?:try_)?(?:ansi_to_utf8|utf8_to_ansi|wide_to_utf8|utf8_to_wide)\b",
}

def main():
    failures = []
    counts = dict.fromkeys(MODULES, 0)
    for path in ("Engine/Utility_Framework/Sha256.h", "Engine/Utility_Framework/StringHelper.h", "Engine/RenderEngine/BoneRegion.cpp"):
        if (ROOT / path).exists():
            failures.append(f"Duplicate legacy implementation: {path}")
    for module in MODULES:
        text = (ROOT / MODULE_SOURCES[module]).read_text(encoding="utf-8-sig")
        if f"export module {module};" not in text:
            failures.append(f"Missing canonical module: {module}")
        text = re.sub(r"/\*.*?\*/|//[^\r\n]*", "", text, flags=re.S)
        if re.search(r"\bm_[A-Za-z]|\b(?:class Sha256|namespace Hash|Sha256Digest|Update|Finish|Compute|ToHex)\b", text):
            failures.append(f"Utility naming: {module}")
    for path in ("Engine/Contracts/EngineContracts.vcxproj", "Engine/Contracts/EngineContracts.vcxproj.filters",
                 "Engine/Utility_Framework/Utility_Framework.vcxproj", "Engine/Utility_Framework/Utility_Framework.vcxproj.filters",
                 "Engine/RenderEngine/RenderEngine.vcxproj", "Engine/RenderEngine/RenderEngine.vcxproj.filters",
                 "Tools/runtime/RuntimeLauncher.vcxproj", "Tools/regression/TextHashModuleProbe.vcxproj"):
        ET.parse(ROOT / path)
    paths = subprocess.check_output(["git", "ls-files", "-co", "--exclude-standard", "--",
                                    "Engine", "Editor", "Player", "HeadlessHost", "Tools", "Lattice", "Dynamic_CPP", "BuildTool"], cwd=ROOT, text=True).splitlines()
    for name in set(paths):
        p = ROOT / name
        if not p.exists() or p.suffix not in (".h", ".hpp", ".cpp", ".inl", ".ixx") or "ModuleAdapters" in p.parts or name.startswith("Engine/Contracts/"):
            continue
        source = p.read_bytes()
        # Ignore comments, preserving quoted literals and their escape sequences.
        code = re.sub(rb'//[^\r\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                      lambda m: b"" if m[0].startswith((b"//", b"/*")) else m[0], source, flags=re.S)
        for module, token in MODULES.items():
            if re.search(token, code):
                counts[module] += 1
                if f"import {module};".encode() not in source:
                    failures.append(f"{name}: must directly import {module}")
        if re.search(rb"\bHash::(?:Sha256|Sha256Digest|ToHex)\b|\b(?:std::tolower|std::isspace|MultiByteToWideChar|WideCharToMultiByte)\s*\(", code):
            failures.append(f"{name}: duplicated text/hash primitive outside the utility owner")
        if re.search(rb"\b(?:SearchKey|TrimLine|ToAsciiLower|LowercaseAscii)\s*\(|\bPak::(?:wide_to_utf8|utf8_to_wide)\s*\(", code):
            failures.append(f"{name}: retired local string utility call")
        if re.search(rb'#include\s+"(?![^"\r\n]*ModuleAdapters)[^"\r\n]*\b(?:Sha256|StringHelper)\.h"', source):
            failures.append(f"{name}: obsolete utility include")
    runpy.run_path(str(ROOT / "Tools/build/generate-module-adapters.py"))["generate"](check=True)
    if failures:
        raise SystemExit("\n".join(failures))
    print("PASS: text/hash modules; direct consumers " + str(counts))

if __name__ == "__main__":
    main()
