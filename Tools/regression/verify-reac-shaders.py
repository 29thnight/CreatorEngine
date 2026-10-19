#!/usr/bin/env python3
"""Compile REAC changes from frozen source bytes and run focused CPU oracles.

Slang 2026.14 and DXC 1.9.2607 match ThirdParty/Slang/README.md. Linux compiler
results do not replace the Windows RHI build, GPU output or native performance
gates. No compiler or dependency is downloaded by this runner.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys


SHADERS = Path("Dynamic_CPP/Assets/Shaders/DefaultPassShader")
REGRESSION = Path("Tools/regression")
BINDINGS = Path("Engine/RenderEngine/RHI/Vulkan/VulkanBindingModel.h")
COMPILER = Path("Engine/RenderEngine/RHI/RHIShaderCompiler.cpp")
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"\n]+)"', re.MULTILINE)
BASELINE_DIAGNOSTIC_CAPABILITIES = ("spvShaderNonUniformEXT", "spvGroupNonUniform",
                                    "spvGroupNonUniformBallot", "spvGroupNonUniformVote")
PROFILE_DIAGNOSTIC_CAPABILITIES = ("SPV_KHR_non_semantic_info", "SPV_GOOGLE_user_type", "spvDerivativeControl",
                                   "spvImageQuery", "spvImageGatherExtended", "spvSparseResidency", "spvMinLod",
                                   "spvFragmentFullyCoveredEXT")


@dataclass(frozen=True)
class ShaderCase:
    name: str
    source: Path
    entry: str
    stage: str = "compute"
    defines: tuple[str, ...] = ()
    int64_atomics: bool = False
    fine_derivatives: bool = False
    quad_operations: bool = False


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run(command: list[str], environment: dict[str, str], timeout: int = 120) -> dict:
    result = subprocess.run(command, env=environment, text=True, capture_output=True, timeout=timeout)
    return {"command": command, "exit_code": result.returncode,
            "stdout": result.stdout, "stderr": result.stderr}


def cases(groups: list[str]) -> list[ShaderCase]:
    result: list[ShaderCase] = []
    if "visibility" in groups:
        source = SHADERS / "GeometryVisibility.slang"
        for entry in ("GeometryVisibilityResetCS", "GeometryVisibilityCullCS", "ShadowMeshletResetCS",
                      "ShadowMeshletCullCS", "MeshletCountCS", "MeshletFinalizeCS", "MeshletScatterCS"):
            result.append(ShaderCase(entry, source, entry))
        for suffix, defines in (
            ("occlusion", ("GEOMETRY_VISIBILITY_OCCLUSION",)),
            ("history", ("GEOMETRY_VISIBILITY_OCCLUSION", "GEOMETRY_VISIBILITY_HISTORY")),
            ("recovery", ("GEOMETRY_VISIBILITY_OCCLUSION", "GEOMETRY_VISIBILITY_RECOVERY")),
            ("camera-prediction", ("GEOMETRY_VISIBILITY_OCCLUSION", "GEOMETRY_VISIBILITY_CAMERA_PREDICTION")),
        ):
            result.append(ShaderCase("ShadowMeshletCullCS-" + suffix, source, "ShadowMeshletCullCS", defines=defines))
        for entry in ("ShadowMeshletResetCS", "ShadowMeshletCullCS", "MeshletCountCS",
                      "MeshletFinalizeCS", "MeshletScatterCS"):
            result.append(ShaderCase(entry + "-shared", source, entry,
                                     defines=("GEOMETRY_VISIBILITY_SHARED_VIEWS",)))
        result.append(ShaderCase("ShadowMeshletCullCS-shared-occlusion", source, "ShadowMeshletCullCS",
                                 defines=("GEOMETRY_VISIBILITY_SHARED_VIEWS", "GEOMETRY_VISIBILITY_OCCLUSION")))
        result.append(ShaderCase("GeometryVisibilityCullCS-occlusion", source, "GeometryVisibilityCullCS",
                                 defines=("GEOMETRY_VISIBILITY_OCCLUSION",)))
        for name, entry, defines in (
            ("ShadowMeshletCullCS-partial", "ShadowMeshletCullCS", ("GEOMETRY_VISIBILITY_QUEUE_PARTIAL_WAVE",)),
            ("ShadowMeshletCullCS-stress", "ShadowMeshletCullCS", ("GEOMETRY_VISIBILITY_RING_STRESS",)),
            ("ShadowMeshletCullCS-partial-stress", "ShadowMeshletCullCS",
             ("GEOMETRY_VISIBILITY_RING_STRESS", "GEOMETRY_VISIBILITY_QUEUE_PARTIAL_WAVE")),
            ("ShadowMeshletResetCS-stress", "ShadowMeshletResetCS", ("GEOMETRY_VISIBILITY_RING_STRESS",)),
        ):
            result.append(ShaderCase(name, source, entry, defines=defines))
        result.append(ShaderCase("GeometryOcclusionBuildCS", SHADERS / "GeometryOcclusionBuild.slang",
                                 "GeometryOcclusionBuildCS"))
    if "material" in groups:
        for entry, stage in (("LXSceneGBufferPS", "fragment"), ("LXSceneTemporalPS", "fragment"),
                             ("LXSceneCachedGBufferPS", "fragment"), ("LXSceneCachedTemporalPS", "fragment"),
                             ("LXSceneReconstructMS", "mesh"), ("LXSceneVisibilityMS", "mesh"),
                             ("LXSceneVisibilityPS", "fragment"), ("LXSceneShadowMS", "mesh"),
                             ("LXSceneShadowPS", "fragment")):
            result.append(ShaderCase(entry, REGRESSION / "reac_material_host_probe.slang", entry, stage,
                                     ("LX_MATERIAL_PIXEL_FOOTPRINT",), True, True, True))
    if "primitive" in groups:
        for entry in ("ClearPrimitiveCache", "PopulatePrimitiveCache"):
            result.append(ShaderCase(entry, SHADERS / "MaterialGraphPrimitiveCache.slang", entry))
    return result


def freeze_sources(repo: Path, output: Path, roots: list[Path]) -> dict[Path, bytes]:
    """Freeze the literal include closure; the compiler reads these same bytes."""
    snapshots: dict[Path, bytes] = {}
    pending = list(roots)
    search = (SHADERS, SHADERS / "Includes", REGRESSION)
    while pending:
        relative = pending.pop()
        if relative in snapshots:
            continue
        path = repo / relative
        data = path.read_bytes()
        snapshots[relative] = data
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        if path.suffix != ".slang":
            continue
        for include in INCLUDE.findall(data.decode("utf-8-sig")):
            candidates = [relative.parent / include, *(directory / include for directory in search)]
            resolved = next((candidate for candidate in candidates if (repo / candidate).is_file()), None)
            if resolved is None:
                raise RuntimeError(f"Unresolved quoted shader include {include!r} in {relative}")
            pending.append(resolved)
    return snapshots


def binding_shifts(text: str) -> dict[str, int]:
    result = {}
    for flag, name in (("b", "ConstantBuffer"), ("t", "ShaderResource"),
                       ("u", "UnorderedAccess"), ("s", "Sampler")):
        match = re.search(rf"k{name}Shift\s*=\s*(\d+)", text)
        if match is None:
            raise RuntimeError(f"Missing production Vulkan {flag} binding shift")
        result[flag] = int(match[1])
    return result


def spirv_metadata(data: bytes) -> dict:
    if len(data) < 20 or len(data) % 4:
        raise RuntimeError("Malformed SPIR-V byte length")
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    if words[0] != 0x07230203:
        raise RuntimeError("Missing SPIR-V magic")
    offset = 5
    capabilities, extensions, decorations, strides, offsets = [], [], [], [], []
    while offset < len(words):
        length, operation = words[offset] >> 16, words[offset] & 0xffff
        if not length or offset + length > len(words):
            raise RuntimeError("Malformed SPIR-V instruction extent")
        operands = words[offset + 1:offset + length]
        if operation == 17:
            capabilities.append(operands[0])
        elif operation == 10:
            extensions.append(struct.pack("<" + "I" * len(operands), *operands).split(b"\0")[0].decode())
        elif operation == 71 and len(operands) == 3 and operands[1] in (33, 34):
            decorations.append(list(operands))
        elif operation == 71 and len(operands) == 3 and operands[1] == 6:
            strides.append(list(operands))
        elif operation == 72 and len(operands) == 4 and operands[2] == 35:
            offsets.append(list(operands))
        offset += length
    return {"version_word": words[1], "capabilities": capabilities, "extensions": extensions,
            "binding_and_set_decorations": decorations, "array_strides": strides,
            "member_offsets": offsets}


def cpu_oracle(args, source_root: Path, output: Path, environment: dict[str, str],
               name: str, entry: str, success_marker: str) -> dict:
    generated = output / (name + ".generated.cpp")
    command = [str(args.slangc.resolve()), str(source_root / REGRESSION / (name + "_probe.slang")),
               "-I", str(source_root / SHADERS), "-entry", entry, "-stage", "compute",
               "-target", "cpp", "-matrix-layout-column-major", "-O3", "-fp-mode", "precise",
               "-warnings-as-errors", "all", "-o", str(generated)]
    cpu = {"slang": run(command, environment), "passed": False}
    if name == "reac_geometry_position":
        cpu["host_compatibility"] = "precise token macro; arithmetic protected by -fno-fast-math -ffp-contract=off"
    if cpu["slang"]["exit_code"] != 0:
        return cpu
    binary = output / (name + "_probe")
    command = [args.cxx, "-std=c++20", "-O2", "-fno-fast-math", "-ffp-contract=off",
               "-I", str(output), str(source_root / REGRESSION / (name + "_probe.cpp")), "-o", str(binary)]
    if args.sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
    cpu["compile"] = run(command, environment)
    if cpu["compile"]["exit_code"] == 0:
        execution_environment = environment
        if args.sanitize:
            # ASan and UBSan remain fail-fast. LSan cannot enumerate the sandbox's
            # separate PID namespace; this runner makes no leak-check claim.
            execution_environment = environment | {"ASAN_OPTIONS": "detect_leaks=0"}
            cpu["leak_sanitizer"] = "disabled; sandbox PID namespace cannot enumerate task registry"
        cpu["run"] = run([str(binary)], execution_environment)
        cpu["passed"] = cpu["run"]["exit_code"] == 0 and success_marker in cpu["run"]["stdout"]
        print(cpu["run"]["stdout"], end="", flush=True)
    cpu["generated_sha256"] = digest(generated.read_bytes())
    return cpu


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--slangc", type=Path, required=True)
    parser.add_argument("--dxc-root", type=Path, help="DXC root containing bin/dxv and lib/libdxcompiler.so")
    parser.add_argument("--spirv-val", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="Fresh output directory")
    parser.add_argument("--group", choices=("visibility", "material", "primitive"), action="append")
    parser.add_argument("--cpu-bounds", action="store_true", help="Run actual Slang C++ bounds helpers against independent oracles")
    parser.add_argument("--cpu-bounds-only", action="store_true")
    parser.add_argument("--cpu-positions", action="store_true", help="Compare production position/full-surface Slang C++ decoders with packet and skin oracles")
    parser.add_argument("--cpu-positions-only", action="store_true")
    parser.add_argument("--diagnostic-capabilities", action="store_true",
                        help="Acknowledge existing nonuniform and subgroup usage for diagnostics; NOT current RHI options or device support proof")
    parser.add_argument("--diagnostic-profile-capabilities", action="store_true",
                        help="Acknowledge the full Slang E41012 inferred capability set; reports actual emitted SPIR-V capabilities separately; NOT current RHI options")
    parser.add_argument("--sanitize", action="store_true", help="Use fail-fast ASan/UBSan for the CPU oracle")
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if output.exists():
        parser.error("--output must be a fresh directory")
    selected = [] if args.cpu_bounds_only or args.cpu_positions_only else cases(args.group or ["visibility", "material", "primitive"])
    cpu_bounds = args.cpu_bounds or args.cpu_bounds_only
    cpu_positions = args.cpu_positions or args.cpu_positions_only
    diagnostic_capabilities = BASELINE_DIAGNOSTIC_CAPABILITIES if (
        args.diagnostic_capabilities or args.diagnostic_profile_capabilities) else ()
    if args.diagnostic_profile_capabilities:
        diagnostic_capabilities += PROFILE_DIAGNOSTIC_CAPABILITIES
    if selected and (args.dxc_root is None or args.spirv_val is None):
        parser.error("Shader matrix requires --dxc-root and --spirv-val; neither validation is silently skipped")
    output.mkdir(parents=True)
    source_root = output / "sources"
    roots = [case.source for case in selected]
    roots += [BINDINGS, COMPILER, REGRESSION / Path(__file__).name]
    if cpu_bounds:
        roots += [REGRESSION / "reac_geometry_bounds_probe.slang", REGRESSION / "reac_geometry_bounds_probe.cpp"]
    if cpu_positions:
        roots += [REGRESSION / "reac_geometry_position_probe.slang", REGRESSION / "reac_geometry_position_probe.cpp"]
    snapshots = freeze_sources(repo, source_root, roots)
    identity = {str(path): {"sha256": digest(data), "bytes": len(data)} for path, data in sorted(snapshots.items())}
    (output / "sources.json").write_text(json.dumps(identity, indent=2) + "\n")
    environment = dict(os.environ)
    libraries = [str(args.slangc.resolve().parent.parent / "lib")]
    if args.dxc_root:
        libraries.append(str(args.dxc_root.resolve() / "lib"))
    environment["LD_LIBRARY_PATH"] = os.pathsep.join(libraries + [environment.get("LD_LIBRARY_PATH", "")])
    report = {"scope": "Linux shader compilation/validation and optional CPU reference only",
              "native_windows_rhi_build": "not run", "native_gpu_execution": "not run",
              "native_performance": "not measured", "tools": {}, "cases": [],
              "compiler_option_mode": "full inferred-profile diagnostic; NOT current RHI options or device support proof"
              if args.diagnostic_profile_capabilities else "nonuniform/subgroup diagnostic; NOT current RHI options or device support proof"
              if args.diagnostic_capabilities else "current RHI explicit options",
              "diagnostic_capabilities_acknowledged": diagnostic_capabilities}
    slang = str(args.slangc.resolve())
    version = run([slang, "-version"], environment)
    if version["exit_code"] or "2026.14" not in version["stdout"] + version["stderr"]:
        raise RuntimeError("Use the engine-pinned Slang 2026.14")
    report["tools"]["slang"] = version | {"sha256": digest(args.slangc.read_bytes())}
    tool_files = [args.slangc.resolve().parent.parent / "lib/libslang-compiler.so",
                  args.slangc.resolve().parent.parent / "include/slang-cpp-prelude.h",
                  args.slangc.resolve().parent.parent / "include/slang-cpp-types.h"]
    if args.dxc_root:
        tool_files += [args.dxc_root.resolve() / name for name in
                       ("bin/dxc", "bin/dxv", "lib/libdxcompiler.so", "lib/libdxil.so")]
    if args.spirv_val:
        tool_files.append(args.spirv_val.resolve())
    report["tool_files"] = {str(path): {"sha256": digest(path.read_bytes()), "bytes": path.stat().st_size}
                            for path in tool_files}
    if cpu_bounds or cpu_positions:
        report["tools"]["cxx"] = run([args.cxx, "--version"], environment)
    if selected:
        for name, command in (("dxc", [str(args.dxc_root.resolve() / "bin/dxc"), "--version"]),
                              ("spirv-val", [str(args.spirv_val.resolve()), "--version"])):
            report["tools"][name] = run(command, environment)
            if report["tools"][name]["exit_code"]:
                raise RuntimeError(f"Cannot run {name}")
        shifts = binding_shifts(snapshots[BINDINGS].decode("utf-8-sig"))
        for case in selected:
            for target in ("dxil", "spirv"):
                binary = output / (case.name + "." + target)
                profile = ("sm_6_6" if case.int64_atomics else "sm_6_5" if case.stage == "mesh" else "sm_6_0")
                if target == "spirv":
                    profile = "spirv_1_5" if case.stage == "mesh" else "spirv_1_3"
                command = [slang, str(source_root / case.source), "-entry", case.entry, "-stage", case.stage,
                           "-lang", "slang", "-target", target, "-profile", profile, "-matrix-layout-column-major",
                           "-O3", "-fp-mode", "precise", "-warnings-as-errors", "all", "-o", str(binary)]
                for directory in (SHADERS, SHADERS / "Includes", REGRESSION):
                    command += ["-I", str(source_root / directory)]
                command += ["-D" + define + "=1" for define in case.defines]
                if target == "spirv":
                    command += ["-D__spirv__=1", "-fvk-use-entrypoint-name", "-fvk-use-dx-layout"]
                    for flag, shift in shifts.items():
                        command += ["-fvk-" + flag + "-shift", str(shift), "0"]
                    for enabled, capability in ((case.int64_atomics, "spvInt64Atomics"),
                                                 (case.stage == "mesh", "spvMeshShadingEXT"),
                                                 (case.fine_derivatives, "spvDerivativeControl"),
                                                 (case.quad_operations, "spvGroupNonUniformQuad")):
                        if enabled:
                            command += ["-capability", capability]
                    for capability in diagnostic_capabilities:
                        command += ["-capability", capability]
                item = {"name": case.name, "entry": case.entry, "stage": case.stage, "target": target,
                        "compile": run(command, environment), "passed": False}
                if item["compile"]["exit_code"] == 0:
                    data = binary.read_bytes()
                    item.update(bytes=len(data), sha256=digest(data))
                    validator = [str(args.dxc_root.resolve() / "bin/dxv"), str(binary)]
                    if target == "spirv":
                        item["spirv"] = spirv_metadata(data)
                        validator = [str(args.spirv_val.resolve()), "--target-env", "vulkan1.3", str(binary)]
                    item["validation"] = run(validator, environment)
                    item["passed"] = item["validation"]["exit_code"] == 0
                    if "SPV_NV_shader_subgroup_partitioned" in item.get("spirv", {}).get("extensions", []):
                        item["passed"] = False
                        item["unsupported_extension"] = "SPV_NV_shader_subgroup_partitioned"
                report["cases"].append(item)
                (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
                print(f"REAC_SHADER_RESULT {case.name} {target} passed={item['passed']}", flush=True)
    if cpu_bounds:
        report["cpu_bounds"] = cpu_oracle(args, source_root, output, environment,
                                          "reac_geometry_bounds", "BoundsOracleCS", "REAC_GEOMETRY_BOUNDS_OK")
    if cpu_positions:
        report["cpu_positions"] = cpu_oracle(args, source_root, output, environment,
                                             "reac_geometry_position", "PositionOracleCS", "REAC_GEOMETRY_POSITION_OK")
    changed = [str(path) for path, data in snapshots.items() if (repo / path).read_bytes() != data]
    report["changed_sources"] = changed
    report["passed"] = not changed and all(item["passed"] for item in report["cases"]) and (
        not cpu_bounds or report["cpu_bounds"]["passed"]) and (
        not cpu_positions or report["cpu_positions"]["passed"])
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"REAC_VALIDATION_RESULT passed={report['passed']} shaders={len(report['cases'])} report={output / 'report.json'}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"REAC_VALIDATION_ERROR {error}", file=sys.stderr)
        raise SystemExit(1)
