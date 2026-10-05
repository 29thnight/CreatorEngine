#!/usr/bin/env python3
"""Build and test exact production audio sources; never rewrite copied engine code."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
AUDIO = "Engine/SceneRuntime/Audio/"
UTILITY = "Engine/Utility_Framework/"
COOKED = "Engine/RenderEngine/Experiment/Cooked/"
TEST = "Tools/regression/audio/"
CORE = [AUDIO + name + ".cpp" for name in ("VoiceTable", "NullAudioBackend", "AudioRuntime",
        "AudioHost", "ClipDirectory", "MiniaudioBackend", "SoundGraph", "PlaybackService")]
MANIFEST = COOKED + "CookedAssetManifest.cpp"
AUTHORING = [AUDIO + name + ".cpp" for name in ("SoundAssetSerialization", "AudioCatalog", "EditorAudioClipCache")]
AUTHORING += [UTILITY + name + ".cpp" for name in ("AuthoringParsedDocument", "AuthoringCookedDocument",
              "AuthoringRymlErrorPolicy", "AuthoringScalarConvert")]
AUTHORING += [COOKED + "CookedAssetCatalog.cpp", TEST + "sound_asset_contract_probe.cpp"]
INCLUDES = ["Engine/SceneRuntime", "Engine/SceneRuntime/Audio", "Engine/RenderEngine",
            "Engine/Utility_Framework", "ThirdParty/Mathematics/include", "Tools/regression", "Tools/AssetCooker"]
CONFIGS = {"debug": ["-O0", "-g"], "release": ["-O2", "-DNDEBUG"],
           "sanitize": ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]}


def execute(command, log, timeout=300, env=None):
    started = time.monotonic()
    with log.open("w") as output:
        output.write("COMMAND " + " ".join(map(str, command)) + "\n")
        output.flush()
        try:
            result = subprocess.run(list(map(str, command)), cwd=ROOT, stdout=output,
                                    stderr=subprocess.STDOUT, timeout=timeout, env=env)
            code = result.returncode
        except subprocess.TimeoutExpired:
            output.write("\nTIMEOUT\n")
            code = 124
    return code, round(time.monotonic() - started, 3)


def snapshot(paths):
    return {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(set(paths))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=[*CONFIGS, "all"], default="debug")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--output", type=Path, default=ROOT / "Build/Validation/Phase22Portable")
    parser.add_argument("--ryml-prefix", type=Path, help="Official rapidyaml/c4core include/ and lib/libryml.a prefix")
    parser.add_argument("--suites", nargs="+", choices=["core", "decode", "render", "software-device"],
                        default=["core", "decode", "render", "software-device"])
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--resume", action="store_true", help="Reuse only successfully compiled per-object provenance manifests with matching input/command/object hashes")
    parser.add_argument("--core-only", action="store_true", help="Omit auxiliary source/cook/asset tests")
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()
    if not args.core_only and not args.ryml_prefix:
        parser.error("Full audio regression requires --ryml-prefix; use --core-only only for an explicitly limited run")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    records = []
    (output / "results.json").write_text(json.dumps({"status": "building", "suites": []}, indent=2) + "\n")
    includes = ["-I" + str(ROOT / path) for path in INCLUDES]
    if args.ryml_prefix:
        includes += ["-I" + str(args.ryml_prefix.resolve() / "include")]
    executable_sources = {"audio_voice_contract_probe": ["Tools/regression/audio_voice_contract_probe.cpp", *CORE]}
    if not args.core_only:
        executable_sources.update({
            "cooked_contract_probe": [TEST + "cooked_contract_probe.cpp", MANIFEST],
            "cooked_playback_probe": [TEST + "cooked_playback_probe.cpp", MANIFEST,
                                      *[path for path in CORE if Path(path).stem in
                                        ("VoiceTable", "NullAudioBackend", "AudioRuntime", "MiniaudioBackend")]],
            "cook_decoder_probe": [TEST + "cook_decoder_probe.cpp", "Tools/AssetCooker/AudioDecodeValidation.cpp"],
            "cold_device_probe": [TEST + "cold_device_probe.cpp", AUDIO + "AudioRuntime.cpp",
                                  AUDIO + "VoiceTable.cpp", AUDIO + "MiniaudioBackend.cpp",
                                  AUDIO + "AudioHost.cpp", AUDIO + "NullAudioBackend.cpp"],
        })
        if args.ryml_prefix:
            executable_sources["sound_asset_contract_probe"] = [*AUTHORING, MANIFEST,
                *[path for path in CORE if Path(path).stem not in ("AudioHost", "ClipDirectory")]]
            executable_sources["sound_cook_contract_probe"] = [TEST + "sound_cook_contract_probe.cpp", MANIFEST,
                AUDIO + "SoundGraph.cpp", AUDIO + "SoundAssetSerialization.cpp",
                *[path for path in AUTHORING if path.startswith(UTILITY)]]
    all_sources = list(dict.fromkeys(path for sources in executable_sources.values() for path in sources))
    tracked = [ROOT / path for path in all_sources]
    for directory in (*INCLUDES, "ThirdParty/miniaudio", COOKED):
        tracked.extend(path for path in (ROOT / directory).rglob("*")
                       if path.is_file() and path.suffix in (".h", ".hpp", ".inl", ".c"))
    ledger = snapshot(tracked)
    header_paths = [path for path in tracked if path.suffix != ".cpp"]
    header_ledger = snapshot(header_paths)
    external_paths = sorted(path for path in (args.ryml_prefix / "include").rglob("*")
                            if path.is_file()) if args.ryml_prefix else []
    def external_snapshot():
        return {str(path.relative_to(args.ryml_prefix)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in external_paths}
    external_ledger = external_snapshot()
    compiler_path = Path(shutil.which(args.cxx) or args.cxx).resolve()
    compiler_identity = {"version": subprocess.check_output([args.cxx, "--version"], text=True),
                         "binary_sha256": hashlib.sha256(compiler_path.read_bytes()).hexdigest()}
    (output / "source-sha256.json").write_text(json.dumps(ledger, indent=2) + "\n")
    configurations = list(CONFIGS) if args.config == "all" else [args.config]
    failures = 0
    for config in configurations:
        build = output / config
        build.mkdir(exist_ok=True)
        config_record_start = len(records)
        def compile_object(source, obj, log, command):
            # Do not associate a global ledger with objects that may predate an
            # aborted build. A manifest is published ONLY after this exact
            # compiler invocation succeeds with unchanged inputs.
            expected = {"source": source, "source_sha256": ledger[source],
                        "headers": header_ledger, "external_headers": external_ledger,
                        "command": list(map(str, command)), "compiler": compiler_identity}
            manifest = obj.with_suffix(".provenance.json")
            if args.resume and obj.exists() and manifest.exists():
                saved = json.loads(manifest.read_text())
                if (saved.get("inputs") == expected
                        and saved.get("object_sha256") == hashlib.sha256(obj.read_bytes()).hexdigest()):
                    print("REUSE verified per-object provenance " + str(obj.relative_to(ROOT)), flush=True)
                    return 0, 0.0
            if (hashlib.sha256((ROOT / source).read_bytes()).hexdigest() != ledger[source]
                    or snapshot(header_paths) != header_ledger or external_snapshot() != external_ledger):
                print("SOURCE_CHANGED_BEFORE_COMPILE: " + source, file=sys.stderr)
                return 2, 0.0
            code, elapsed = execute(command, log)
            if code:
                return code, elapsed
            if (hashlib.sha256((ROOT / source).read_bytes()).hexdigest() != ledger[source]
                    or snapshot(header_paths) != header_ledger or external_snapshot() != external_ledger):
                print("SOURCE_CHANGED_DURING_COMPILE: " + source, file=sys.stderr)
                return 2, elapsed
            data = {"inputs": expected, "object_sha256": hashlib.sha256(obj.read_bytes()).hexdigest()}
            temporary = manifest.with_suffix(".tmp")
            temporary.write_text(json.dumps(data, indent=2) + "\n")
            temporary.replace(manifest)
            return 0, elapsed
        (build / "source-sha256.json").write_text(json.dumps(ledger, indent=2) + "\n")
        flags = ["-std=c++23", *CONFIGS[config], "-DWAVE_AUDIO_PROBE",
                 "-DMA_ENABLE_ONLY_SPECIFIC_BACKENDS", "-DMA_ENABLE_NULL",
                 "-Wall", "-Wextra", "-Wno-unknown-pragmas", *includes]
        objects = {}
        for source in all_sources:
            obj = build / (Path(source).stem + ".o")
            extra = ["-DNDEBUG"] if source in AUTHORING or source == TEST + "sound_cook_contract_probe.cpp" else []
            command = [args.cxx, *flags, *extra, "-c", ROOT / source, "-o", obj]
            log = build / (obj.stem + ".build.log")
            code, elapsed = compile_object(source, obj, log, command)
            if code:
                print((build / (obj.stem + ".build.log")).read_text())
                return code
            objects[source] = obj
        cold_object = build / "MiniaudioBackend.no-device.o"
        if not args.core_only:
            cold_flags = [flag for flag in flags if flag != "-DMA_ENABLE_NULL"]
            command = [args.cxx, *cold_flags, "-c", ROOT / (AUDIO + "MiniaudioBackend.cpp"), "-o", cold_object]
            log = build / "MiniaudioBackend.no-device.build.log"
            code, elapsed = compile_object(AUDIO + "MiniaudioBackend.cpp", cold_object, log, command)
            if code:
                print((build / "MiniaudioBackend.no-device.build.log").read_text())
                return code
        for name, sources in executable_sources.items():
            libraries = [args.ryml_prefix.resolve() / "lib/libryml.a"] if name in ("sound_asset_contract_probe", "sound_cook_contract_probe") else []
            selected_objects = [cold_object if name == "cold_device_probe" and path == AUDIO + "MiniaudioBackend.cpp"
                                else objects[path] for path in sources]
            code, elapsed = execute([args.cxx, *CONFIGS[config], *selected_objects, *libraries,
                                     "-pthread", "-ldl", "-lm", "-o", build / name], build / (name + ".link.log"))
            if code:
                print((build / (name + ".link.log")).read_text())
                return code
        if snapshot(tracked) != ledger:
            print("SOURCE_CHANGED_DURING_BUILD: evidence rejected; rerun after source edits finish", file=sys.stderr)
            (output / "results.json").write_text(json.dumps({"status": "invalidated", "reason": "sources changed during build"}, indent=2) + "\n")
            return 2
        if args.build_only:
            continue
        env = dict(os.environ)
        if config == "sanitize":
            # LSan cannot run under this host's ptrace. ASan and UBSan remain enabled.
            env.setdefault("ASAN_OPTIONS", "detect_leaks=0:abort_on_error=1")
            env.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
        root = Path(tempfile.mkdtemp(prefix="run-", dir=build))
        fixture_work = root / "fixtures"
        fixture_code, _ = execute([build / "audio_voice_contract_probe", "fixtures", fixture_work],
                                  build / "fixtures.log", args.timeout, env)
        if fixture_code:
            print((build / "fixtures.log").read_text(errors="replace"))
            (build / "results.json").write_text(json.dumps({"status": "failed", "reason": "fixture generation failed",
                "exit_code": fixture_code}, indent=2) + "\n")
            return fixture_code
        formats = fixture_work / "Build/Validation/AudioVoiceContract/Formats"
        runs = [(suite, [build / "audio_voice_contract_probe", suite, root / suite]) for suite in args.suites]
        if not args.core_only:
            runs.extend([
                ("cooked-contract", [build / "cooked_contract_probe", root / "cooked-contract"]),
                ("cooked-playback", [build / "cooked_playback_probe", formats]),
                ("cook-decoder", [build / "cook_decoder_probe", formats]),
                ("cold-device", [build / "cold_device_probe", formats]),
            ])
            if args.ryml_prefix:
                runs.append(("sound-assets", [build / "sound_asset_contract_probe", formats, root / "sound-assets"]))
                runs.append(("sound-cook", [build / "sound_cook_contract_probe"]))
        for suite, command in runs:
            log = build / (suite + ".log")
            code, elapsed = execute(command, log, args.timeout, env)
            content = log.read_text(errors="replace")
            summary = re.search(r"SUMMARY checks=(\d+) passed=(\d+) failed=(\d+)", content)
            record = {"config": config, "suite": suite, "exit_code": code, "seconds": elapsed,
                      "log": str(log.relative_to(output))}
            if summary:
                record.update(zip(("assertions", "passed", "failed"), map(int, summary.groups())))
            else:
                summary = re.search(r"COOKED_CONTRACT_OK checks=(\d+)", content)
                if summary:
                    record.update(assertions=int(summary.group(1)), passed=int(summary.group(1)), failed=0)
                else:
                    summary = re.search(r"sound-cook-contract checks=(\d+) failures=(\d+)", content)
                    if summary:
                        count, failed = map(int, summary.groups())
                        record.update(assertions=count, passed=count - failed, failed=failed)
            records.append(record)
            print(json.dumps(record), flush=True)
            if code:
                failures += 1
                print("\n".join(line for line in content.splitlines() if "FAIL" in line or "ERROR" in line or "runtime error" in line), flush=True)
        (build / "results.json").write_text(json.dumps({"config": config, "suites": records[config_record_start:]}, indent=2) + "\n")
    if snapshot(tracked) != ledger or external_snapshot() != external_ledger:
        print("SOURCE_CHANGED_DURING_TEST: evidence rejected; rerun against final sources", file=sys.stderr)
        (output / "results.json").write_text(json.dumps({"status": "invalidated", "reason": "sources changed during test"}, indent=2) + "\n")
        return 2
    (output / "results.json").write_text(json.dumps({"status": "failed" if failures else "passed", "host": sys.platform,
        "compiler": subprocess.check_output([args.cxx, "--version"], text=True).splitlines()[0],
        "source_mode": "exact production translation units", "physical_device": "not tested",
        "ryml_prefix": str(args.ryml_prefix) if args.ryml_prefix else None,
        "authoring_tests": "enabled" if args.ryml_prefix else "not built; pass --ryml-prefix",
        "leak_sanitizer": "disabled under ptrace; leak freedom not verified",
        "suites": records}, indent=2) + "\n")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
