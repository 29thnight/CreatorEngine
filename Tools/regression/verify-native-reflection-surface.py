#!/usr/bin/env python3
"""Generate and compile the actual native reflection surface with the Windows toolchain.

This is a compile gate for real RenderEngine/SceneRuntime registration sources
and ScriptReflectionBindings.cpp. It deliberately does not link or run the Editor.
ReflgenGenerate supplies the project's real headers, registration includes,
standalone module adapters, preprocessor definitions and include search paths.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import urllib.request
import xml.etree.ElementTree as ET
import zipfile


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def diagnostic_excerpt(text: str) -> str:
    lines = text.splitlines()
    if len(lines) <= 160:
        return text
    # Keep setup context and the final summary, plus the first and last actual
    # errors. A cascade of template diagnostics must not hide its first cause.
    selected = set(range(40)) | set(range(len(lines) - 40, len(lines)))
    errors = [index for index, line in enumerate(lines)
              if re.search(r"\berror\b|\bFAILED:|\bTraceback\b", line, re.IGNORECASE)]
    for index in errors[:3] + errors[-3:]:
        selected.update(range(max(0, index - 3), min(len(lines), index + 10)))
    excerpt: list[str] = []
    previous = -1
    for index in sorted(selected):
        if index > previous + 1:
            excerpt.append(f"... {index - previous - 1} log lines omitted; see the full artifact log ...")
        excerpt.append(lines[index])
        previous = index
    return "\n".join(excerpt)


def run(command: list[str], *, cwd: Path, log: Path) -> str:
    log.parent.mkdir(parents=True, exist_ok=True)
    log.with_suffix(log.suffix + ".command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    print(f"SURFACE_RUN {log.name}", flush=True)
    with log.open("w", encoding="utf-8") as output:
        result = subprocess.run(command, cwd=cwd, stdout=output, stderr=subprocess.STDOUT, text=True)
    text = log.read_text(encoding="utf-8", errors="replace")
    if result.returncode:
        print(diagnostic_excerpt(text), flush=True)
        raise RuntimeError(f"{log.name} failed ({result.returncode}); full log: {log}")
    return text


def prepare_cppwinrt(engine: Path, work: Path) -> tuple[Path, Path, str]:
    properties = ET.parse(engine / "Tools/build/WindowsAppSdk.props").getroot()
    version = properties.findtext(".//CreatorCppWinRTVersion")
    require(bool(version), "The engine must pin Microsoft.Windows.CppWinRT.")
    package = work / "nuget" / f"microsoft.windows.cppwinrt.{version}"
    archive = package.with_suffix(package.suffix + ".nupkg")
    package.parent.mkdir(parents=True, exist_ok=True)
    url = f"https://api.nuget.org/v3-flatcontainer/microsoft.windows.cppwinrt/{version}/microsoft.windows.cppwinrt.{version}.nupkg"
    if not archive.exists():
        with urllib.request.urlopen(url, timeout=60) as response, archive.open("wb") as output:
            shutil.copyfileobj(response, output)
    if not (package / "build/native/Microsoft.Windows.CppWinRT.targets").is_file():
        with zipfile.ZipFile(archive) as contents:
            for name in contents.namelist():
                destination = (package / name).resolve()
                require(destination.is_relative_to(package.resolve()), f"Invalid package path: {name}")
            contents.extractall(package)
    tool = package / "bin/cppwinrt.exe"
    require(tool.is_file(), "Pinned C++/WinRT package lacks bin/cppwinrt.exe.")
    # The NuGet package contains the pinned generator, not prebuilt projection
    # headers. Generate its real SDK projection instead of using an older SDK
    # copy of winrt/base.h or an invented compatibility header.
    sdk = Path(os.environ.get("WindowsSdkDir", ""))
    sdk_version = os.environ.get("WindowsSDKVersion", "").strip("/\\")
    metadata = sdk / "UnionMetadata" / sdk_version / "Windows.winmd"
    require(metadata.is_file(), f"Visual Studio's Windows SDK metadata is missing: {metadata}")
    include = work / "cppwinrt-headers"
    if not (include / "winrt/base.h").is_file():
        run([str(tool), "-base", "-in", str(metadata), "-out", str(include)],
            cwd=engine, log=work / "logs/cppwinrt-projection.log")
    require((include / "winrt/base.h").is_file(), "Pinned C++/WinRT generator did not create winrt/base.h.")
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    return package, include, digest


def prepare_vcpkg(engine: Path, work: Path, executable: Path) -> tuple[Path, str]:
    baseline = json.loads((engine / "vcpkg.json").read_text(encoding="utf-8-sig"))["builtin-baseline"]
    manifest = work / "dependency-manifest"
    manifest.mkdir(parents=True, exist_ok=True)
    # These are header dependencies of the real registration surface. Full
    # renderer, physics and Editor libraries are not needed by a compile gate.
    (manifest / "vcpkg.json").write_text(json.dumps({
        "name": "creator-native-reflection-surface", "version-string": "1",
        "builtin-baseline": baseline, "dependencies": ["gameinput", "spdlog", "ryml"],
    }, indent=2), encoding="utf-8")
    installed = work / "installed"
    run([str(executable), "install", "--triplet=x64-windows", f"--x-manifest-root={manifest}",
         f"--x-install-root={installed}"], cwd=manifest, log=work / "logs/dependencies.log")
    include = installed / "x64-windows/include"
    for header in ("GameInput.h", "spdlog/spdlog.h", "fmt/format.h", "ryml/ryml.hpp"):
        require((include / header).is_file(), f"Pinned dependency did not install {header}.")
    return include, baseline


def msbuild_properties(values: dict[str, str]) -> list[str]:
    # Every value here is a scalar. Escape literal MSBuild separators without
    # shell interpolation; escaped semicolons cannot represent property lists.
    return ["-p:" + key + "=" + value.replace("%", "%25").replace(";", "%3B") for key, value in values.items()]


def compiler_arguments(path: Path) -> list[str]:
    result: list[str] = []
    for argument in path.read_text(encoding="utf-8-sig").splitlines():
        argument = argument.strip()
        if not argument:
            continue
        if argument.startswith("-isystem"):
            result.append("/I" + argument[len("-isystem"):])
        elif argument.startswith("-I"):
            result.append("/I" + argument[2:])
        elif argument.startswith("-D"):
            result.append("/D" + argument[2:])
        elif argument in ("-std=c++23", "-std=c++2b"):
            result.append("/std:c++23preview")
        elif argument in ("-std=c++26", "-std=c++2c"):
            result.append("/std:c++latest")
        elif argument.startswith("-std=c++"):
            result.append("/std:" + argument[len("-std="):])
        else:
            raise RuntimeError(f"Unmapped production parser argument {argument!r} in {path}")
    require(any(argument == "/DCE_CONTRACT_STANDALONE=1" for argument in result),
            "The production parser did not select the standalone module adapters.")
    return list(dict.fromkeys(result))


def compile_source(source: Path, arguments: list[str], *, engine: Path, work: Path,
                   configuration: str, name: str, injections: list[Path] = ()) -> Path:
    output = work / "objects" / (name + ".obj")
    output.parent.mkdir(parents=True, exist_ok=True)
    flags = ["/nologo", "/c", "/EHsc", "/bigobj", "/utf-8", "/permissive-", "/Zc:__cplusplus",
             "/Zc:preprocessor", "/W3", "/wd5030", "/wd4828", "/Od", "/MDd" if configuration == "Debug" else "/MD"]
    response = work / "commands" / (name + ".rsp")
    response.parent.mkdir(parents=True, exist_ok=True)
    command = flags + arguments + ["/FI" + str(header) for header in injections] + ["/Fo" + str(output), str(source)]
    # UTF-8 BOM makes non-ASCII repository paths deterministic for cl.exe.
    response.write_text("\n".join(subprocess.list2cmdline([argument]) for argument in command), encoding="utf-8-sig")
    run(["cl.exe", "@" + str(response)], cwd=engine, log=work / "logs" / (name + ".log"))
    require(output.is_file() and output.stat().st_size > 0, f"Compiler did not create {output}.")
    return output


def compile_surfaces(engine: Path, work: Path, generated: dict[str, Path], configuration: str) -> list[dict]:
    inputs = [(module + "-registration", directory / ("reflgen_" + module + ".cpp"),
               directory / ("reflgen_" + module + ".args"), [])
              for module, directory in generated.items()]
    inputs.append(("ScriptReflectionBindings", engine / "Engine/SceneRuntime/ScriptReflectionBindings.cpp",
                   generated["SceneRuntime"] / "reflgen_SceneRuntime.args",
                   [directory / ("reflgen_" + module + ".h") for module, directory in generated.items()]))
    results: list[dict] = []
    for name, source, arguments, injections in inputs:
        result = {"name": name, "source": str(source), "log": str(work / "logs" / (name + ".log"))}
        try:
            output = compile_source(source, compiler_arguments(arguments), engine=engine, work=work,
                                    configuration=configuration, name=name, injections=injections)
            result.update(status="success", object=output.name)
        except RuntimeError as error:
            result.update(status="failure", error=str(error))
            print(f"SURFACE_SOURCE_FAILED {name}: {error}", flush=True)
        results.append(result)
    return results


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--reflgen-root", type=Path, required=True)
    parser.add_argument("--vcpkg", type=Path, required=True)
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    require(os.name == "nt", "Actual engine headers require Windows SDK and MSVC; run this gate in a VS developer shell.")
    require(shutil.which("msbuild.exe") is not None and shutil.which("cl.exe") is not None,
            "Enter the x64 Visual Studio developer shell first.")
    engine, work = args.engine.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    include, baseline = prepare_vcpkg(engine, work, args.vcpkg.resolve())
    cppwinrt, cppwinrt_include, cppwinrt_hash = prepare_cppwinrt(engine, work)
    reflgen = args.reflgen_root.resolve()
    properties = msbuild_properties({
        "Configuration": args.configuration, "Platform": "x64", "BuildInParallel": "false",
        "BuildProjectReferences": "false", "VcpkgEnabled": "false", "VcpkgEnableManifest": "false",
        "EngineReflgenTargets": str(reflgen / "msbuild/reflgen.targets"),
        "ReflgenExecutable": str(args.generator.resolve()), "ReflgenIncludeDirectory": str(reflgen / "include") + os.sep,
        "ReflgenReflectionBackend": "Legacy", "CreatorGameInputIncludeDir": str(include) + os.sep,
        "CreatorCppWinRTPackageRoot": str(cppwinrt) + os.sep,
        # ConfigureCreatorContractsForReflgen already adds the installed include
        # directory through CreatorGameInputIncludeDir. Pass only this additional
        # root here: an escaped semicolon would become one invalid FullPath item.
        "ExternalIncludePath": str(cppwinrt_include),
    })
    modules = ("RenderEngine", "SceneRuntime")
    generated: dict[str, Path] = {}
    declarations: dict[str, dict] = {}
    for module in modules:
        project = engine / "Engine" / module / (module + ".vcxproj")
        run(["msbuild.exe", str(project), "-nologo", "-m:1", "-verbosity:minimal", "-t:ReflgenGenerate", *properties],
            cwd=engine, log=work / "logs" / (module + "-generate.log"))
        directory = engine / "Build/Obj" / module / ("x64-" + args.configuration) / "reflgen"
        for suffix in (".h", ".cpp", ".args", ".rsp"):
            require((directory / ("reflgen_" + module + suffix)).is_file(), f"Missing actual generated {module}{suffix}")
        generated[module] = directory

    # Generation itself does not run injection. The real injection target
    # collects cross-project schemas and writes each registration reference
    # header, so run it only after both modules have generated their own schemas.
    for module in modules:
        project = engine / "Engine" / module / (module + ".vcxproj")
        run(["msbuild.exe", str(project), "-nologo", "-m:1", "-verbosity:minimal", "-t:_ReflgenInject", *properties],
            cwd=engine, log=work / "logs" / (module + "-injections.log"))
        references = generated[module] / ("reflgen_" + module + ".references.h")
        require(references.is_file(), f"The actual injection target did not create {references}.")

    for module, directory in generated.items():
        project = engine / "Engine" / module / (module + ".vcxproj")
        # Reuse the authoritative response file verbatim and request its machine
        # readable declaration inventory. This does not synthesize source types.
        declaration_path = directory / ("reflgen_" + module + ".declarations.json")
        run([str(args.generator.resolve()), "@" + str(directory / ("reflgen_" + module + ".rsp")),
             "--declarations-json", str(declaration_path)], cwd=project.parent,
            log=work / "logs" / (module + "-declarations.log"))
        declarations[module] = json.loads(declaration_path.read_text(encoding="utf-8-sig"))
        artifact = work / "generated" / module
        artifact.mkdir(parents=True, exist_ok=True)
        for source in directory.iterdir():
            if source.is_file() and source.suffix in (".h", ".cpp", ".args", ".rsp", ".json", ".tsv", ".dependencies"):
                shutil.copy2(source, artifact / source.name)

    # Compare typed F# files with fresh native declarations from both owning
    # modules. A stale checked-in snapshot must not silently omit a new member.
    facade_command = [sys.executable, str(engine / "Tools/build/generate-fsharp-native-facades.py"),
                      "--engine", str(engine), "--check"]
    for module, directory in generated.items():
        facade_command.extend(["--declarations", str(directory / ("reflgen_" + module + ".declarations.json"))])
    run(facade_command, cwd=engine, log=work / "logs/typed-facades.log")

    # Prove the complete production component inventory was extracted, including
    # inherited bases. Source markers alone are never accepted as this gate.
    inventory_path = engine / "Tools/regression/verify-native-reflection-inventory.py"
    specification = importlib.util.spec_from_file_location("native_component_inventory", inventory_path)
    inventory = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(inventory)
    expected = inventory.inventory(engine)
    ownership = engine / "Tools/regression/verify-native-reflection-ownership.py"
    run([sys.executable, str(ownership), "--engine", str(engine), "--json", str(work / "ownership.json")],
        cwd=engine, log=work / "logs/source-ownership.log")
    source_ownership = json.loads((work / "ownership.json").read_text(encoding="utf-8"))
    native_projects = {f"Engine/{module}/{module}.vcxproj" for module in modules}
    owned_headers = [record for record in source_ownership if set(record["owners"]) & native_projects]
    emitted_headers = {str(Path(header["path"]).resolve()).casefold(): len(header["classes"]) + len(header["enums"])
                       for declaration in declarations.values() for header in declaration["headers"]}
    for record in owned_headers:
        count = emitted_headers.get(str((engine / record["header"]).resolve()).casefold(), 0)
        require(count >= record["declarations"],
                f"Generator omitted annotated declarations in {record['header']}: expected {record['declarations']}, extracted {count}")
    classes = {record["qualified_name"] for declaration in declarations.values()
               for header in declaration["headers"] for record in header["classes"]}
    require(expected <= classes, "Generated descriptors omit components: " + str(sorted(expected - classes)))

    compilations = compile_surfaces(engine, work, generated, args.configuration)
    failures = [item["name"] for item in compilations if item["status"] != "success"]
    result = {
        "result": "NATIVE_REFLECTION_SURFACE_COMPILE_FAILED" if failures else "NATIVE_REFLECTION_SURFACE_COMPILE_OK",
        "configuration": args.configuration,
        "scope": "Actual engine generation and object compilation; no Editor link or runtime execution",
        "modules": list(modules), "components": len(expected), "generated_records": len(classes),
        "annotated_native_headers": len(owned_headers),
        "annotated_native_declarations": sum(record["declarations"] for record in owned_headers),
        "objects": [item["object"] for item in compilations if item["status"] == "success"],
        "compilations": compilations,
        "vcpkg_baseline": baseline, "cppwinrt_sha256": cppwinrt_hash,
    }
    (work / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result), flush=True)
    require(not failures, "Native reflection surface compilation failed: " + ", ".join(failures))


if __name__ == "__main__":
    main()
