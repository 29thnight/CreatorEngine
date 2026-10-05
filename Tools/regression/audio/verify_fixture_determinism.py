#!/usr/bin/env python3
"""Compare all generated fixture bytes across two independent probe executions."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def digest_tree(path):
    return {str(file.relative_to(path)): hashlib.sha256(file.read_bytes()).hexdigest()
            for file in sorted(path.rglob("*")) if file.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, default=ROOT / "Build/Validation/Phase22Portable/debug/audio_voice_contract_probe")
    parser.add_argument("--output", type=Path, default=ROOT / "Build/Validation/Phase22Portable")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifests = []
    for index in range(2):
        work = Path(tempfile.mkdtemp(prefix=f"fixture-determinism-{index}-", dir=output))
        subprocess.run([str(args.probe.resolve()), "fixtures", str(work)], check=True, timeout=30,
                       stdout=subprocess.DEVNULL)
        manifests.append(digest_tree(work / "Build/Validation/AudioVoiceContract"))
    result = {"identical": manifests[0] == manifests[1], "files": len(manifests[0]),
              "sha256": manifests[0], "generated_runs": 2}
    (output / "fixture-determinism.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items() if key != "sha256"}))
    return 0 if result["identical"] and result["files"] > 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
