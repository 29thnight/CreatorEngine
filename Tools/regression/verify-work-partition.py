"""Check the production allocation-free partitioner against every cut of bounded inputs."""

import argparse
import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    header = root / "Engine/Utility_Framework/WorkPartition.h"
    probe = Path(__file__).with_name("work_partition_probe.cpp")
    inputs = (header, header.with_name("StandardLibrary.h"), probe)
    hashes = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    with tempfile.TemporaryDirectory(prefix="creator-work-partition-") as directory:
        executable = Path(directory) / "work-partition"
        command = [args.compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-DCE_CONTRACT_STANDALONE=1",
                   str(probe), "-o", str(executable)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        subprocess.run(command, check=True, timeout=60)
        result = subprocess.run([str(executable)], check=True, text=True, capture_output=True, timeout=60)
        print(result.stdout, end="")
    for path, digest in hashes.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f"Source changed during verification: {path}")
        print(f"SOURCE_SHA256 {path.relative_to(root)} {digest}")
    if args.output:
        evidence = {"scope": "allocation-free CPU partition oracle; no frame benchmark",
                    "sanitized": args.sanitize, "asan_options": os.environ.get("ASAN_OPTIONS", ""),
                    "source_sha256": {str(path.relative_to(root)): digest for path, digest in hashes.items()},
                    "output": result.stdout.splitlines()}
        args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
