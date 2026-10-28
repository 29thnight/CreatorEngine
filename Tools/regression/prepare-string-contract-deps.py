#!/usr/bin/env python3
"""Fetch the exact dependencies used by the portable string contract suite.

All checkouts are isolated from the engine's vcpkg installation. Existing
checkouts must match the requested commit; this script never resets user work.
The versions match vcpkg baseline 9e593bb18ea69cc5095e012465dcd675a822ed0d
and the repository's reflgen overlay.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import urllib.request


DEPENDENCIES = {
    "imgui": ("ocornut/imgui", "b61e56346a92cfcaf1f43a545ca37b0b32239654"),
    "imgui-node-editor": ("thedmd/imgui-node-editor", "c24a8a80484568ce85bd92cfcb5c24646b951ac4"),
    "fmt": ("fmtlib/fmt", "1be298e1bd68957e4cd352e1f676f00e07dcfb57"),
    "spdlog": ("gabime/spdlog", "79524ddd08a4ec981b7fea76afd08ee05f83755d"),
    "rapidyaml": ("biojppm/rapidyaml", "f8ac8dd50f4f7916579d55a05ebf9c6488e52670"),
    "reflgen": ("29thnight/reflgen_cpp", "357971b7f07f48393c0995cdd7fa52bfe29c3c47"),
}
BASELINE = "9e593bb18ea69cc5095e012465dcd675a822ed0d"
PATCHES = {
    "fix-vec2-math-operators.patch": "ccc9d0563ac29badcd48b63f597b92ea09693bc4895657beccadd0e5f9f08f78",
    "remove-getkeyindex.patch": "9e8af3e50e821737db89ca4bdd32b208b6a7e5879c847f6d467420552552c465",
    "fix-imgui-v1.92.5.patch": "0c4ffb0c7177e77580cd27e4b4ec2ea9fc1ac7fd39cb10b855685db4f54fc3ae",
}


def run(*arguments: str, cwd: Path) -> None:
    subprocess.run(arguments, cwd=cwd, check=True)


def prepare(destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for name, (repository, commit) in DEPENDENCIES.items():
        checkout = destination / name
        if not checkout.exists():
            checkout.mkdir()
            run("git", "init", str(checkout), cwd=destination)
            run("git", "config", "core.autocrlf", "false", cwd=checkout)
            run("git", "remote", "add", "origin", f"https://github.com/{repository}.git", cwd=checkout)
            run("git", "fetch", "--depth=1", "origin", commit, cwd=checkout)
            run("git", "checkout", "--detach", "FETCH_HEAD", cwd=checkout)
        actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=checkout, text=True).strip()
        if actual != commit:
            raise RuntimeError(f"{checkout}: expected {commit}, found {actual}; existing work was preserved")
        if name == "rapidyaml":
            run("git", "submodule", "update", "--init", "--recursive", "--depth=1", cwd=checkout)

    patch_directory = destination / "patches"
    patch_directory.mkdir(exist_ok=True)
    for filename, expected in PATCHES.items():
        url = f"https://raw.githubusercontent.com/microsoft/vcpkg/{BASELINE}/ports/imgui-node-editor/{filename}"
        with urllib.request.urlopen(url, timeout=60) as response:
            contents = response.read().replace(b"\r\n", b"\n")
        if hashlib.sha256(contents).hexdigest() != expected:
            raise RuntimeError(f"Unexpected contents for pinned patch {filename}")
        patch = patch_directory / filename
        patch.write_bytes(contents)
        checkout = destination / "imgui-node-editor"
        already_applied = subprocess.run(
            ["git", "apply", "--reverse", "--check", str(patch)], cwd=checkout,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        ).returncode == 0
        if not already_applied:
            run("git", "apply", "--check", str(patch), cwd=checkout)
            run("git", "apply", str(patch), cwd=checkout)
    print(f"STRING_CONTRACT_DEPENDENCIES_READY {destination}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    prepare(parser.parse_args().directory.resolve())
