#!/usr/bin/env python3
"""Cross-check component sources, lifetime registration and the runtime reflection catalog.

The source check is an omission guard, not a C++ parser or proof of engine execution.
Use --catalog with the engine's native catalog JSON to additionally prove runtime coverage.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re


def without_comments(source: str) -> str:
    return re.sub(r"//[^\n]*|/\*[\s\S]*?\*/", "", source)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit("NATIVE_REFLECTION_INVENTORY_FAILED: " + message)


def inventory(engine: Path) -> set[str]:
    scene = engine / "Engine" / "SceneRuntime"
    uuid_source = without_comments((scene / "ComponentTypeUUID.h").read_text(encoding="utf-8-sig"))
    lifecycle_source = without_comments((scene / "LifecycleRegistry.cpp").read_text(encoding="utf-8-sig"))
    uuid_names = re.findall(r'\{\s*"(\w+)"\s*,\s*"[0-9a-fA-F-]{36}"\s*\}', uuid_source)
    lifecycle_names = re.findall(r"\bRegister<(\w+)>\(\)", lifecycle_source)
    require(bool(uuid_names), "the native UUID component list is empty")
    for label, names in (("UUID", uuid_names), ("lifecycle", lifecycle_names)):
        duplicates = sorted(name for name, count in Counter(names).items() if count != 1)
        require(not duplicates, f"duplicate {label} entries: {duplicates}")
    require(set(uuid_names) == set(lifecycle_names),
            "UUID/lifecycle component names differ: " + str(sorted(set(uuid_names) ^ set(lifecycle_names))))

    declarations: dict[str, tuple[str, str, Path]] = {}
    pattern = re.compile(r"\b(?:class|struct)\s+((?:\[\[[\s\S]*?\]\]\s*)*)([A-Za-z_]\w*)\s*(?:final\s*)?:\s*([^;{}]+)\{")
    for path in (engine / "Engine").rglob("*"):
        if path.suffix not in (".h", ".ixx"):
            continue
        # Legacy native headers can contain CP949 comments. Declaration tokens
        # are ASCII; Latin-1 preserves them byte-for-byte without ignoring data.
        source = without_comments(path.read_bytes().decode("latin-1"))
        for match in pattern.finditer(source):
            attributes, name, bases = match.groups()
            identity = re.search(r"meta::identity\s*<\s*" + re.escape(name) + r"\s*,\s*(\w+)\s*>", bases)
            if identity:
                require(name not in declarations, f"ambiguous component declaration {name}")
                declarations[name] = (identity.group(1), attributes, path)
    descendants = {"Component"}
    while True:
        added = {name for name, (base, _, _) in declarations.items() if base in descendants}
        if added <= descendants:
            break
        descendants |= added
    require(set(uuid_names) == descendants - {"Component"},
            "component sources/UUID registration differ: " + str(sorted(set(uuid_names) ^ (descendants - {"Component"}))))
    for name in sorted(descendants):
        require(name in declarations, f"cannot locate native declaration of {name}")
        _, attributes, path = declarations[name]
        require("reflgen::reflect" in attributes, f"{name} lacks reflection metadata in {path}")
        require("reflgen::public_methods" in attributes, f"{name} lacks complete public method selection in {path}")
    print(f"NATIVE_REFLECTION_SOURCE_INVENTORY_OK components={len(uuid_names)} descriptors={len(descendants)}")
    return descendants


def verify_catalog(path: Path, expected: set[str]) -> None:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if isinstance(value, dict) and "ok" in value:
        require(value["ok"] is True, "native catalog request failed")
        value = value["value"]
    require(isinstance(value, list), "runtime catalog must be an array")
    names = [item["name"] for item in value]
    require(len(names) == len(set(names)), "runtime catalog contains duplicate names")
    all_names = set(names)
    component_names = {item["name"] for item in value if item.get("component")}
    require(expected <= component_names, "runtime catalog omitted native components: " + str(sorted(expected - component_names)))
    for item in value:
        reachable = set(item.get("children", []))
        reachable.update(field["type"] for field in item.get("fields", []))
        for method in item.get("methods", []):
            reachable.add(method["returnType"])
            reachable.update(parameter["type"] for parameter in method.get("parameters", []))
            require(method.get("callable") or bool(method.get("reason")),
                    f"{item['name']}.{method['name']} is unavailable without an explanation")
        require(reachable <= all_names, f"{item['name']} has omitted recursive descriptors: {sorted(reachable - all_names)}")
    print(f"NATIVE_REFLECTION_RUNTIME_INVENTORY_OK components={len(component_names)} types={len(names)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--catalog", type=Path, help="Catalog response captured from the actual engine")
    args = parser.parse_args()
    expected = inventory(args.engine.resolve())
    if args.catalog:
        verify_catalog(args.catalog, expected)


if __name__ == "__main__":
    main()
