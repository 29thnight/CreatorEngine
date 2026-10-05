#!/usr/bin/env python3
"""Build pinned official rapidyaml/c4core for portable audio asset tests only.

No repository dependency or production build file is changed. The version pair
matches vcpkg baseline 9e593bb18ea69cc5095e012465dcd675a822ed0d.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

PINS = {
    "rapidyaml": ("https://github.com/biojppm/rapidyaml.git", "v0.16.0", "f8ac8dd50f4f7916579d55a05ebf9c6488e52670"),
    "c4core": ("https://github.com/biojppm/c4core.git", "v0.6.0", "1885dca396de5aea84f176eccb92a00c8b85c5ff"),
}


def run(command):
    print(" ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True, timeout=300)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--config", choices=["debug", "release", "sanitize"], default="release")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--rapidyaml-source", type=Path)
    parser.add_argument("--c4core-source", type=Path)
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    sources = {}
    for name, (url, tag, revision) in PINS.items():
        source = getattr(args, name + "_source") or prefix / "sources" / name
        source = source.resolve()
        if not source.exists():
            source.parent.mkdir(parents=True, exist_ok=True)
            run(["git", "clone", "--depth", "1", "--branch", tag, url, source])
        actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        if actual != revision:
            raise RuntimeError(f"{name} revision differs from pinned official source: {actual}")
        if subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"], text=True).strip():
            raise RuntimeError(f"{name} source tree is modified; refusing unverified dependency build")
        sources[name] = source
    include = prefix / "include"
    include.mkdir(exist_ok=True)
    shutil.copytree(sources["c4core"] / "src/c4", include / "c4", dirs_exist_ok=True)
    shutil.copytree(sources["rapidyaml"] / "src/c4/yml", include / "c4/yml", dirs_exist_ok=True)
    debugbreak = sources["rapidyaml"] / "ext/c4core.src/c4/ext/debugbreak/debugbreak.h"
    (include / "c4/ext/debugbreak").mkdir(parents=True, exist_ok=True)
    shutil.copy2(debugbreak, include / "c4/ext/debugbreak/debugbreak.h")
    (include / "ryml").mkdir(exist_ok=True)
    for name in ("ryml.hpp", "ryml_std.hpp"):
        shutil.copy2(sources["rapidyaml"] / "src" / name, include / name)
        # The engine uses vcpkg's ryml/ include namespace. Preserve original
        # upstream headers byte-for-byte and provide the equivalent install alias.
        (include / "ryml" / name).write_text('#pragma once\n#include "../' + name + '"\n')
    objects = prefix / "obj"
    objects.mkdir(exist_ok=True)
    flags = ["-std=c++20", "-O1", "-DNDEBUG", "-I" + str(include), "-I" + str(sources["rapidyaml"] / "src")]
    if args.config == "debug":
        flags += ["-O0", "-g"]
    elif args.config == "sanitize":
        flags += ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    built = []
    for package, directory in (("c4core", "src/c4"), ("rapidyaml", "src/c4/yml")):
        for source in sorted((sources[package] / directory).glob("*.cpp")):
            obj = objects / (package + "-" + source.stem + ".o")
            run([args.cxx, *flags, "-c", source, "-o", obj])
            built.append(obj)
    (prefix / "lib").mkdir(exist_ok=True)
    run(["ar", "rcs", prefix / "lib/libryml.a", *built])
    (prefix / "dependency-pins.json").write_text(json.dumps({
        "pins": {name: {"url": url, "tag": tag, "commit": commit} for name, (url, tag, commit) in PINS.items()},
        "config": args.config, "compiler": args.cxx, "flags": flags}, indent=2) + "\n")


if __name__ == "__main__":
    main()
