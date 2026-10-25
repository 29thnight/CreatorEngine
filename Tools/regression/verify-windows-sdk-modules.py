"""Check owned SDK dependencies are imports and backend API state stays private."""
from pathlib import Path
import json
import re
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SDK_ROOT = "Engine/Platform/WindowsSDK/"
SOURCE_ROOTS = ("Engine", "Editor", "Player", "Tools", "Lattice")


def main():
    mapping = json.loads((ROOT / "Tools/build/windows-sdk-headers.json").read_text(encoding="utf-8-sig"))
    names = set(subprocess.check_output(["git", "ls-files", "--", *SOURCE_ROOTS], cwd=ROOT, text=True).splitlines())
    names.update(subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "--", *SOURCE_ROOTS], cwd=ROOT, text=True).splitlines())
    failures = []
    imports = 0
    for name in sorted(names):
        path = ROOT / name
        if not path.is_file() or path.suffix not in {".h", ".hpp", ".cpp", ".inl", ".ixx"}:
            continue
        source = path.read_bytes().decode("latin1")
        if re.search(r'\b(?:Microsoft::)?WRL::ComPtr\b|using\s+namespace\s+Microsoft::WRL\s*;', source):
            failures.append(f"{name}: COM ownership must use winrt::com_ptr")
        imports += len(re.findall(r'^\s*import "Creator\.(?:WinAPI|GraphicsSDK|LegacyGraphicsSDK|AppRuntimeSDK|GameInputSDK)\.h";', source, re.M))
        if name.startswith(SDK_ROOT):
            continue
        for match in re.finditer(r'^\s*#\s*include\s*[<"]([^">]+)[">]', source, re.M):
            header = match[1].replace("\\", "/").lower()
            if header in mapping:
                failures.append(f"{name}: textual SDK include {header}")
        if re.search(r'^\s*#\s*include\s*"[^"\n]*AppBackendWinAPI\.h"', source, re.M):
            failures.append(f"{name}: removed backend header is still consumed")
    interface = (ROOT / "Engine/Platform/Windows/AppBackendWinAPI/Creator.AppBackend.WinAPI.ixx").read_text(encoding="utf-8-sig")
    if "export module Creator.AppBackend.WinAPI;" not in interface or "std::unique_ptr<state>" not in interface:
        failures.append("Backend must export its API with opaque private state")
    if "#include" in interface or "m_hWnd" in interface or "DumpHandler" in interface:
        failures.append("Backend interface exposes implementation headers or native state")
    if (ROOT / "Engine/Utility_Framework/AppBackendWinAPI.h").exists():
        failures.append("Backend implementation header must not be restored")
    runpy.run_path(str(ROOT / "Tools/build/generate-module-adapters.py"))["generate"](check=True)
    if failures:
        raise SystemExit("\n".join(failures))
    print(f"PASS: {len(mapping)} SDK header mappings; {imports} SDK imports; no owned textual SDK includes outside the SDK backend")


if __name__ == "__main__":
    main()
