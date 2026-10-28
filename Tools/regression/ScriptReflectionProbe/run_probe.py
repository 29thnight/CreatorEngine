#!/usr/bin/env python3
"""Build the actual F# binding project and execute the production reflection service fixture."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dotnet", required=True, type=Path)
    parser.add_argument("--generator", required=True, type=Path)
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    project_dir = Path(__file__).resolve().parent
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PATH"] = str(args.dotnet.resolve().parent) + os.pathsep + env.get("PATH", "")
    env["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
    env["DOTNET_NOLOGO"] = "1"
    clang_arguments = "-std=c++20 -target x86_64-pc-windows-msvc" if os.name == "nt" else "-std=c++20 -D__stdcall= -D__cdecl="
    command = [str(args.dotnet), "build", str(project_dir / "ScriptReflectionProbe.fsproj"),
               "-c", args.configuration, "--nologo", "-m:1", "-p:BuildInParallel=false",
               f"-p:ScriptNativeGenerator={args.generator.resolve()}",
               f"-p:ScriptNativeOutput={work / 'table'}", f"-p:ScriptNativeClangArguments={clang_arguments}"]
    result = subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (work / "managed-build.log").write_text(result.stdout, encoding="utf-8")
    if result.returncode:
        raise SystemExit(f"Managed build failed ({result.returncode}):\n{result.stdout}")
    output = project_dir / "bin" / args.configuration / "net10.0"
    shutil.copy2(args.library, output / args.library.name)
    dependency_file = output / "ScriptReflectionProbe.deps.json"
    dependencies = json.loads(dependency_file.read_text(encoding="utf-8"))["libraries"]
    forbidden = [name for name in dependencies if name.startswith(("ScriptCore/", "Mono.Cecil/", "Microsoft.CSharp/"))]
    if forbidden:
        raise SystemExit(f"Unexpected runtime bridge dependencies: {forbidden}")
    result = subprocess.run([str(args.dotnet), str(output / "ScriptReflectionProbe.dll")], env=env,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (work / "managed-run.log").write_text(result.stdout, encoding="utf-8")
    print(result.stdout, end="")
    if result.returncode or "SCRIPT_REFLECTION_OK" not in result.stdout:
        raise SystemExit(f"Native/F# reflection probe failed ({result.returncode})")


if __name__ == "__main__":
    main()
