"""Static assertion migration check. Does not compile or execute C++ code."""
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
EXCLUDED = re.compile(r"ThirdParty|External|external|vendor|imgui|ImGuizmo|Artifacts/|plf_colony")
TOKENS = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\b\w+\b|[^\s]'
)


def main():
    tracked = subprocess.check_output(
        ["git", "ls-files", "*.cpp", "*.h", "*.hpp", "*.inl", "*.ixx", "*.cppm", "*.c"],
        cwd=ROOT, text=True,
    ).splitlines()
    untracked = subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "--", "Engine", "Editor", "Player", "Tools", "Lattice"],
        cwd=ROOT, text=True,
    ).splitlines()
    failures = []
    calls = 0
    enforced = 0
    migrated = 0
    for name in sorted(set(tracked + untracked)):
        # git ls-files also reports tracked paths deleted in the working tree.
        if not (ROOT / name).is_file():
            continue
        if EXCLUDED.search(name) or Path(name).suffix not in {".cpp", ".h", ".hpp", ".inl", ".ixx", ".cppm", ".c"}:
            continue
        text = (ROOT / name).read_bytes().decode("latin1")
        tokens = [m.group() for m in TOKENS.finditer(text)]
        for index, token in enumerate(tokens[:-1]):
            if token in {"assert", "_ASSERT", "_ASSERTE", "ASSERT", "CE_ASSERT"} and tokens[index + 1] == "(":
                failures.append(f"{name}: legacy assertion {token}")
        count = text.count("::ce::contracts::assert_that(") + text.count("::ce::contracts::enforce_that(")
        enforced += text.count("::ce::contracts::enforce_that(")
        if re.search(r"::ce::contracts::(?:assert_that|enforce_that)\(\s*(?!\s|\[)", text):
            failures.append(f"{name}: expected a lazy predicate")
        calls += count
        migrated += bool(count)
        if re.search(r'#\s*include\s*[<"](?:cassert|assert.h)[>"]', text):
            failures.append(f"{name}: legacy assertion include")
        for include in re.findall(r'#\s*include\s*"([^"]*Contract.h)"', text):
            if Path(include).name != "Contract.h":
                continue
            # This fragment is injected after the copied probe preamble.
            if name == "Tools/regression/audio_managed_contract_probe.cpp" and include == "Contract.h":
                continue
            if not ((ROOT / name).parent / include).is_file():
                failures.append(f"{name}: missing {include}")

    ns = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}
    for suffix in ["vcxproj", "vcxproj.filters"]:
        project = ET.parse(ROOT / f"Engine/Utility_Framework/Utility_Framework.{suffix}")
        entries = project.findall('.//ms:ClInclude[@Include="Contract.h"]', ns)
        if len(entries) != 1:
            failures.append(f"{suffix}: expected one Contract.h entry, found {len(entries)}")
    if failures:
        raise SystemExit("\n".join(failures))
    provider = ET.parse(ROOT / "Engine/Contracts/EngineContracts.vcxproj")
    if len(provider.findall('.//ms:ClCompile[@Include="Creator.Contracts.ixx"]', ns)) != 1:
        raise SystemExit("Module provider registration missing")
    ET.parse(ROOT / "Engine/Contracts/EngineContracts.vcxproj.filters")
    targets = ET.parse(ROOT / "Tools/build/Contracts.targets")
    modes = {node.attrib["Condition"]: node.text for node in targets.iter("_CreatorAssertionsEnabled")}
    if modes.get("'$(Configuration)' == 'Debug'") != "true" or modes.get("'$(Configuration)' == 'Release'") != "false":
        raise SystemExit("Incorrect configuration constants")
    if not targets.findall('.//WriteLinesToFile[@WriteOnlyWhenDifferent="true"]'):
        raise SystemExit("Configuration writer must preserve unchanged timestamps")
    parser_hook = targets.find('./Target[@Name="ConfigureCreatorContractsForReflgen"]')
    if parser_hook is None or parser_hook.get("BeforeTargets") != "_ReflgenWriteArguments":
        raise SystemExit("reflgen parser configuration hook missing")
    if parser_hook.findall(".//ClCompile") or parser_hook.findall(".//Exec"):
        raise SystemExit("Parser configuration must not change product compiler items or execute a tool")
    definition = parser_hook.find(".//_ReflgenCompile/PreprocessorDefinitions")
    if definition is None or "CE_CONTRACT_STANDALONE=1" not in definition.text:
        raise SystemExit("reflgen must explicitly select the standalone adapter")
    parser_writer = parser_hook.find(".//WriteLinesToFile")
    module_writer = targets.find('./Target[@Name="GenerateCreatorContractConfig"]/WriteLinesToFile')
    if parser_writer.get("Lines") != module_writer.get("Lines"):
        raise SystemExit("Parser and module configuration values must match")
    interface = (ROOT / "Engine/Contracts/Creator.Contracts.ixx").read_text(encoding="utf-8")
    if "export module Creator.Contracts;" not in interface:
        raise SystemExit("Named module export missing")
    if "import std;" not in interface:
        raise SystemExit("Engine contract module must import std")
    facade = (ROOT / "Engine/Utility_Framework/Contract.h").read_text(encoding="utf-8")
    if "import Creator.Contracts;" not in facade:
        raise SystemExit("Native consumers must import the module")
    print(f"PASS: {calls} lazy contract calls ({enforced} always enforced) in {migrated} files; no owned CRT assertions/includes; module/configuration/XML paths valid")


if __name__ == "__main__":
    main()
