#!/usr/bin/env python3
"""Recheck the final UTF-8 cook-only change against a proven clean audio build.

This intentionally accepts exactly two changed inputs. It uses each original
compiler command's preprocessing flags to prove no other compiled object consumes
those inputs. Any ambiguous/missing provenance fails closed: run a clean build.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess

from run_portable import ROOT, execute

EXPECTED = {"Tools/AssetCooker/SoundAssetCookProducer.h", "Tools/regression/audio/sound_cook_contract_probe.cpp"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "Build/Validation/Phase22Final")
    args = parser.parse_args()
    base = args.build.resolve()
    old = json.loads((base / "debug/source-sha256.json").read_text())
    current = {path: sha(ROOT / path) for path in old}
    changed = {path for path in old if old[path] != current[path]}
    if changed != EXPECTED:
        raise RuntimeError(f"Expected only the two reviewed UTF-8 cook inputs, got {sorted(changed)}")
    results = {}
    for config in ("debug", "release", "sanitize"):
        build = base / config
        if json.loads((build / "source-sha256.json").read_text()) != old:
            raise RuntimeError("Configuration baseline source ledgers differ")
        proof = []
        affected = []
        manifests = sorted(build.glob("*.provenance.json"))
        if len(manifests) != 26:
            raise RuntimeError("Expected 26 clean object provenance records")
        for manifest in manifests:
            saved = json.loads(manifest.read_text())
            inputs = saved["inputs"]
            obj = manifest.with_name(manifest.name.replace(".provenance.json", ".o"))
            if sha(obj) != saved["object_sha256"] or old[inputs["source"]] != inputs["source_sha256"]:
                raise RuntimeError("Object/source provenance differs from clean baseline")
            compiler = Path(shutil.which(inputs["command"][0]) or inputs["command"][0]).resolve()
            if sha(compiler) != inputs["compiler"]["binary_sha256"]:
                raise RuntimeError("Compiler changed after clean build")
            for path, digest in inputs["headers"].items():
                if old[path] != digest:
                    raise RuntimeError("Header provenance differs from clean baseline")
            prefixes = [Path(arg[2:]).parent for arg in inputs["command"] if arg.startswith("-I") and arg.endswith("/include")]
            for path, digest in inputs["external_headers"].items():
                if not any((prefix / path).is_file() and sha(prefix / path) == digest for prefix in prefixes):
                    raise RuntimeError("External dependency header changed after clean build")
            command = inputs["command"]
            split = command.index("-c")
            if command[split + 1] != str(ROOT / inputs["source"]):
                raise RuntimeError("Unrecognized compile command layout")
            depfile = build / (obj.name + ".current-dependencies.d")
            dep_command = command[:split] + ["-MM", "-MF", str(depfile), "-MT", obj.name, command[split + 1]]
            subprocess.run(dep_command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL, timeout=60)
            dependency_text = depfile.read_text().replace("\\\n", " ")
            dependencies = shlex.split(dependency_text.split(":", 1)[1])
            consumed = {}
            for dependency in dependencies:
                path = Path(dependency)
                path = path.resolve() if path.is_absolute() else (ROOT / path).resolve()
                if path.is_relative_to(ROOT):
                    key = str(path.relative_to(ROOT))
                    if key not in current:
                        raise RuntimeError(f"Previously untracked repository dependency: {key}")
                    consumed[key] = current[key]
            intersection = sorted(changed.intersection(consumed))
            proof.append({"object": obj.name, "source": inputs["source"], "changed_dependencies": intersection,
                          "consumed_repository_inputs": consumed, "prior_object_sha256": saved["object_sha256"]})
            if intersection:
                affected.append((manifest, saved, obj))
        if [obj.name for _, _, obj in affected] != ["sound_cook_contract_probe.o"]:
            raise RuntimeError("Change reaches a different compilation closure; use a full clean build")
        manifest, saved, obj = affected[0]
        code, seconds = execute(saved["inputs"]["command"], build / "sound-cook.targeted.build.log")
        if code:
            raise RuntimeError("Affected cook probe compilation failed")
        if {path: sha(ROOT / path) for path in old} != current:
            raise RuntimeError("Repository inputs changed during targeted compilation")
        saved["inputs"]["source_sha256"] = current[saved["inputs"]["source"]]
        saved["inputs"]["headers"] = {path: current[path] for path in saved["inputs"]["headers"]}
        saved["object_sha256"] = sha(obj)
        manifest.write_text(json.dumps(saved, indent=2) + "\n")
        link_text = (build / "sound_cook_contract_probe.link.log").read_text().splitlines()[0]
        if not link_text.startswith("COMMAND "):
            raise RuntimeError("Missing original link command")
        link = shlex.split(link_text[len("COMMAND "):])
        if not all(Path(arg).exists() for arg in link if arg.endswith((".o", ".a"))):
            raise RuntimeError("Link paths are ambiguous or missing; use a clean build")
        code, _ = execute(link, build / "sound-cook.targeted.link.log")
        if code:
            raise RuntimeError("Targeted cook link failed")
        import os
        env = dict(os.environ)
        if config == "sanitize":
            env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1"
            env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        code, seconds = execute([build / "sound_cook_contract_probe"], build / "sound-cook.log", 60, env)
        text = (build / "sound-cook.log").read_text()
        count = re.search(r"sound-cook-contract checks=(\d+) failures=(\d+)", text)
        if code or not count or int(count.group(2)):
            raise RuntimeError("Targeted cook contract failed")
        record = {"config": config, "suite": "sound-cook", "exit_code": 0, "seconds": seconds,
                  "log": f"{config}/sound-cook.log", "assertions": int(count.group(1)),
                  "passed": int(count.group(1)), "failed": 0, "verification": "targeted UTF-8 recompile; other suites retain clean baseline"}
        configuration = json.loads((build / "results.json").read_text())
        configuration["suites"] = [record if item["suite"] == "sound-cook" else item for item in configuration["suites"]]
        (build / "results.json").write_text(json.dumps(configuration, indent=2) + "\n")
        (build / "source-sha256.baseline.json").write_text(json.dumps(old, indent=2) + "\n")
        (build / "source-sha256.json").write_text(json.dumps(current, indent=2) + "\n")
        results[config] = {"recompiled_objects": [obj.name], "objects_unchanged_from_clean_build": 25,
                           "result": record, "dependency_proof": proof}
        print(json.dumps(record), flush=True)
    if {path: sha(ROOT / path) for path in old} != current:
        raise RuntimeError("Repository inputs changed during targeted tests")
    summary = {"changed_inputs": sorted(changed), "baseline_source_sha256": old,
               "final_source_sha256": current, "configurations": results,
               "method": "Original compile flags plus GCC -MM; only sound_cook_contract_probe.o consumes either changed input"}
    (base / "targeted-recheck.json").write_text(json.dumps(summary, indent=2) + "\n")
    (base / "source-sha256.json").write_text(json.dumps(current, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
