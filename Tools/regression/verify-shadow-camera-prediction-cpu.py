#!/usr/bin/env python3
"""Execute frozen production Slang shadow-prediction/culling helpers on the CPU.

The real shader bodies run through Slang's C++ target with checked fake texture
and buffer bindings. This does not execute a native GPU dispatch or raster pass.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


SHADERS = Path("Dynamic_CPP/Assets/Shaders/DefaultPassShader")
REGRESSION = Path("Tools/regression")
INPUTS = (
    SHADERS / "GeometryVisibility.slang",
    SHADERS / "GeometryOcclusion.slang",
    SHADERS / "Includes/GeometryInstanceBounds.slang",
    REGRESSION / "support/ShadowCameraPredictionCpu.slang",
    REGRESSION / "shadow_camera_prediction_cpu_probe.cpp",
    REGRESSION / "verify-shadow-camera-prediction-cpu.py",
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def declaration(source: str, name: str, kind: str) -> str:
    match = re.search(rf"\b{kind}\s+{re.escape(name)}\b", source)
    if match is None:
        raise ValueError(f"Missing production {kind} {name}")
    opening = source.index("{", match.end())
    # Ignore braces in comments and quoted literals while preserving offsets.
    tokens = re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|[{}]', source[opening:])
    level = 0
    for token in tokens:
        value = token.group()
        if value == "{":
            level += 1
        elif value == "}":
            level -= 1
            if level == 0:
                end = opening + token.end()
                if kind == "struct":
                    end = source.index(";", end) + 1
                return source[match.start():end]
    raise ValueError(f"Unclosed production declaration {name}")


def run(command: list[str], environment: dict[str, str]) -> dict:
    completed = subprocess.run(command, env=environment, text=True, capture_output=True, check=False)
    return {"command": command, "exit_code": completed.returncode,
            "stdout": completed.stdout, "stderr": completed.stderr}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--slangc", type=Path, required=True)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--output", type=Path, required=True, help="Fresh output directory")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--leak-check", action="store_true",
                        help="Also enable LeakSanitizer; requires --sanitize and readable /proc task state")
    parser.add_argument("--mutation", choices=("bypass-coverage", "drop-recovery"),
                        help="Controlled mutation of derived shader source; a correct oracle rejects it")
    args = parser.parse_args()
    if args.leak_check and not args.sanitize:
        parser.error("--leak-check requires --sanitize")
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if output.exists():
        parser.error("--output must be a fresh directory")
    snapshots = {path: (root / path).read_bytes() for path in INPUTS}
    output.mkdir(parents=True)
    frozen = output / "sources"
    for path, data in snapshots.items():
        target = frozen / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    source = snapshots[SHADERS / "GeometryVisibility.slang"].decode("utf-8")
    wrapper = declaration(source, "GeometryMeshletBoxOccluded", "bool")
    if args.mutation == "bypass-coverage":
        wrapper, changed = re.subn(
            r"!GeometryPredictionContainsBounds\(\s*minimumWorld,\s*maximumWorld,\s*"
            r"(?:projection|meshletHistoryColumns)\s*\)", "false", wrapper)
        if changed != 2:
            raise ValueError("Coverage mutation must change both real shared/single guards")
    elif args.mutation == "drop-recovery":
        wrapper, changed = re.subn(r"testOcclusion\s*=\s*[^\n;]*==\s*0u\s*;",
                                   "testOcclusion = true;", wrapper)
        if changed != 2:
            raise ValueError("Recovery mutation must change both real shared/single gates")
    fixture = snapshots[REGRESSION / "support/ShadowCameraPredictionCpu.slang"].decode("utf-8")
    replacements = {
        "/*__VISIBILITY_TYPES__*/": declaration(source, "MeshletViewData", "struct") + "\n" +
                                    declaration(source, "GeometryVisibilityCandidate", "struct"),
        "/*__SHARED_OCCLUSION__*/": declaration(source, "GeometrySharedBoxOccluded", "bool"),
        "/*__MESHLET_OCCLUSION__*/": wrapper,
    }
    for marker, body in replacements.items():
        if fixture.count(marker) != 1:
            raise ValueError(f"Expected one extraction marker {marker}")
        fixture = fixture.replace(marker, body)
    shader = output / "ShadowCameraPredictionCpu.generated.slang"
    shader.write_text(fixture, encoding="utf-8")
    environment = dict(os.environ)
    library = args.slangc.resolve().parent.parent / "lib"
    environment["LD_LIBRARY_PATH"] = str(library) + os.pathsep + environment.get("LD_LIBRARY_PATH", "")
    if args.sanitize:
        environment["ASAN_OPTIONS"] = f"detect_leaks={int(args.leak_check)}:halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    slang = str(args.slangc.resolve())
    report = {
        "scope": "CPU execution of exact production Slang helpers with fake resource bindings",
        "native_gpu_executed": False,
        "mutation": args.mutation,
        "sanitize": args.sanitize,
        "leak_check": args.leak_check,
        "sources": {str(path): {"sha256": digest(data), "bytes": len(data)}
                    for path, data in snapshots.items()},
        "tools": {"slang": run([slang, "-version"], environment),
                  "slang_sha256": digest(args.slangc.read_bytes()),
                  "cxx": run([args.cxx, "--version"], environment)},
        "cases": [],
    }
    variants = (("prediction", "GEOMETRY_VISIBILITY_CAMERA_PREDICTION", "1"),
                ("recovery", "GEOMETRY_VISIBILITY_RECOVERY", "2"),
                ("shared", "GEOMETRY_VISIBILITY_SHARED_VIEWS", "3"))
    for name, define, variant_id in variants:
        directory = output / name
        directory.mkdir()
        generated = directory / "shadow_camera_prediction.generated.cpp"
        command = [slang, str(shader), "-I", str(frozen / SHADERS), "-entry", "ShadowCameraPredictionCpu",
                   "-stage", "compute", "-target", "cpp", "-matrix-layout-column-major", "-O3", "-fp-mode",
                   "precise", "-warnings-as-errors", "all", "-DGEOMETRY_VISIBILITY_OCCLUSION=1",
                   f"-D{define}=1", "-o", str(generated)]
        case = {"name": name, "slang": run(command, environment), "passed": False}
        if case["slang"]["exit_code"] == 0:
            binary = directory / "shadow_camera_prediction_cpu_probe"
            cxx = [args.cxx, "-std=c++20", "-O2", "-fno-fast-math", "-ffp-contract=off",
                   "-I", str(directory), f"-DSHADOW_PREDICTION_VARIANT={variant_id}",
                   str(frozen / REGRESSION / "shadow_camera_prediction_cpu_probe.cpp"), "-o", str(binary)]
            if args.sanitize:
                cxx[3:3] = ["-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
            case["compile"] = run(cxx, environment)
            case["generated_sha256"] = digest(generated.read_bytes())
            if case["compile"]["exit_code"] == 0:
                case["run"] = run([str(binary)], environment)
                case["passed"] = case["run"]["exit_code"] == 0 and \
                    "SHADOW_CAMERA_PREDICTION_CPU_OK" in case["run"]["stdout"]
                print(case["run"]["stdout"], end="", flush=True)
        report["cases"].append(case)
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"SHADOW_CAMERA_CPU_VARIANT name={name} passed={case['passed']}", flush=True)
        if not case["passed"]:
            failure = case.get("run", case.get("compile", case["slang"]))
            print(failure["stderr"], end="", file=sys.stderr)
    changed = [str(path) for path, data in snapshots.items() if (root / path).read_bytes() != data]
    report["changed_sources"] = changed
    report["passed"] = not changed and all(case["passed"] for case in report["cases"])
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"SHADOW_CAMERA_CPU_RESULT passed={report['passed']} report={output / 'report.json'}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
