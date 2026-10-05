#!/usr/bin/env python3
"""Prove regression assertions go red for isolated, explicit source mutations.

Only generated copies beneath --build/canaries are changed. Production sources
and the passing objects are never overwritten. Requires a passing Debug build.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from run_portable import ROOT, CORE, INCLUDES, execute


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "Build/Validation/Phase22Portable/debug")
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    build = args.build.resolve()
    target = build / "canaries"
    target.mkdir(exist_ok=True)
    definitions = [
        ("stale-generation", "VoiceTable",
         "slot.generation = NextGeneration(slot.generation, m_generationNamespace);",
         "slot.generation = NextGeneration(0u, m_generationNamespace);"),
        ("missing-clip-guard", "AudioRuntime",
         "if (!m_started || input.clip.IsEmpty() || !m_backend.HasClip(input.clip))",
         "if (!m_started || input.clip.IsEmpty())"),
        ("graph-voice-bound", "SoundGraph",
         "if (count > definition.maximumVoices)", "if (false)"),
    ]
    results = []
    include = ["-I" + str(ROOT / path) for path in INCLUDES]
    for name, unit, before, after in definitions:
        source = ROOT / "Engine/SceneRuntime/Audio" / (unit + ".cpp")
        original = source.read_bytes()
        text = original.decode()
        if text.count(before) != 1:
            raise RuntimeError(f"Canary {name} target moved; review mutation before proceeding")
        mutant = target / (name + ".cpp")
        mutant.write_text(text.replace(before, after))
        obj = target / (name + ".o")
        code, elapsed = execute([args.cxx, "-std=c++23", "-O0", "-g", *include, "-c", mutant, "-o", obj],
                                target / (name + ".build.log"))
        if code:
            raise RuntimeError(f"Canary {name} did not compile; this is not evidence of detection")
        other = [build / "audio_voice_contract_probe.o"]
        other += [build / (Path(path).stem + ".o") for path in CORE if Path(path).stem != unit]
        executable = target / name
        code, elapsed = execute([args.cxx, *other, obj, "-pthread", "-ldl", "-lm", "-o", executable],
                                target / (name + ".link.log"))
        if code:
            raise RuntimeError(f"Canary {name} did not link; this is not evidence of detection")
        work = Path(tempfile.mkdtemp(prefix=name + "-", dir=target))
        log = target / (name + ".log")
        code, elapsed = execute([executable, "core", work], log, 60)
        failures = [line.strip() for line in log.read_text().splitlines() if "[FAIL]" in line]
        detected = code == 1 and bool(failures)
        unchanged = hashlib.sha256(source.read_bytes()).digest() == hashlib.sha256(original).digest()
        record = {"canary": name, "source": str(source.relative_to(ROOT)), "exit_code": code,
                  "detected": detected, "production_source_unchanged": unchanged,
                  "failed_assertions": failures}
        results.append(record)
        print(json.dumps(record), flush=True)
        if not detected or not unchanged:
            return 1
    (target / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
