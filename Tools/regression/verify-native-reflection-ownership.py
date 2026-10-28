#!/usr/bin/env python3
"""Require one generator owner for each annotated native reflection header.

The extractor processes declarations only from its explicit header arguments.
An include reached transitively is insufficient. Conversely, giving an annotated
header to two reflection projects emits duplicate runtime schemas because engine
translation units receive every common reflection injection.

This source check is an input-ownership guard, not a C++ semantic coverage test.
The production Windows gate must also generate and compile the actual registry.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


MODULES = (
    "Engine/RenderEngine/RenderEngine.vcxproj",
    "Engine/SceneRuntime/SceneRuntime.vcxproj",
    "Editor/Editor.vcxproj",
)
DECLARATION = re.compile(
    r"\b(?:class|struct|enum(?:\s+class)?)\s+"
    r"(?:alignas\s*\([^)]*\)\s*)?"
    r"((?:\[\[[^\]]*\]\]\s*)+)"
)


def inventory(engine: Path) -> list[dict[str, object]]:
    owners: dict[Path, list[str]] = {}
    for relative in MODULES:
        project = engine / relative
        if not project.is_file():
            raise RuntimeError(f"missing reflection owner project: {relative}")
        document = ET.parse(project)
        unique: set[Path] = set()
        for item in document.iter():
            if item.tag.rsplit("}", 1)[-1] != "ClInclude":
                continue
            include = item.attrib.get("Include", "")
            if not include or "$" in include or "*" in include:
                continue
            header = (project.parent / include.replace("\\", "/")).resolve()
            if header in unique:
                raise RuntimeError(f"duplicate ClInclude in {relative}: {include}")
            unique.add(header)
            owners.setdefault(header, []).append(relative)

    result: list[dict[str, object]] = []
    for directory in (engine / "Engine", engine / "Editor"):
        for header in sorted(directory.rglob("*.h")):
            # Historical CP949 comments do not affect ASCII declaration tokens.
            source = header.read_bytes().decode("latin-1")
            source = re.sub(r"//[^\n]*|/\*[\s\S]*?\*/", "", source)
            matches = [match for match in DECLARATION.finditer(source)
                       if re.search(r"\breflgen::(?:reflect|runtime)\b", match[1])]
            if not matches:
                continue
            result.append({
                "header": header.relative_to(engine).as_posix(),
                "declarations": len(matches),
                "owners": owners.get(header.resolve(), []),
            })
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--json", type=Path, help="Write the complete ownership inventory")
    args = parser.parse_args()
    records = inventory(args.engine.resolve())
    if args.json:
        args.json.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    failures = [record for record in records if len(record["owners"]) != 1]
    if failures:
        for record in failures:
            print(f"NATIVE_REFLECTION_OWNERSHIP_FAILED {record['header']}: owners={record['owners']}")
        raise SystemExit(1)
    print("NATIVE_REFLECTION_OWNERSHIP_OK "
          f"headers={len(records)} declarations={sum(record['declarations'] for record in records)}")


if __name__ == "__main__":
    main()
