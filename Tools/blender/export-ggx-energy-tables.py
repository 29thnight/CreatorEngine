"""Export the pinned Cycles 5.1.1 reflection and dielectric layering tables."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
SOURCE_SHA = "fbde5fea5750eb32e2a638b74b97ef6a39eed29283b2f46eb139893ca3af5175"
TABLES = (("ggx_E", "kGgxEnergy", 1024), ("ggx_Eavg", "kGgxAverageEnergy", 32),
          ("ggx_gen_schlick_ior_s", "kGgxDielectricAlbedo", 4096))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    raw = args.source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA:
        raise ValueError("Source must be the unmodified Blender v5.1.1 shader.tables")
    text = raw.decode("utf-8")
    notice = ["// SPDX-FileCopyrightText: 2011-2022 Blender Foundation",
              "// SPDX-License-Identifier: Apache-2.0",
              "// Generated from Blender v5.1.1 intern/cycles/scene/shader.tables.",
              "// Reproduce with Tools/blender/export-ggx-energy-tables.py.", "#pragma once", ""]
    shader = list(notice)
    header = notice + ["#include <array>", "namespace MaterialProbe::Reference {", ""]
    for source, name, size in TABLES:
        match = re.search(r"static const float table_" + source + r"\[" + str(size) + r"\] = \{(.*?)\};", text, re.S)
        if not match:
            raise ValueError(f"Missing table: {source}")
        values = re.findall(r"[-+]?\d+\.\d+(?:e[-+]?\d+)?f", match[1])
        if len(values) != size:
            raise ValueError(f"Wrong table size: {source}")
        shader.append(f"static const float {name}[{size}] = {{")
        header.append(f"inline constexpr std::array<double, {size}> {name}{{{{")
        for index in range(0, size, 8):
            row = "    " + ", ".join(v[:-1] for v in values[index:index + 8]) + ","
            shader.append(row)
            header.append(row)
        shader.extend(["};", ""])
        header.extend(["}};", ""])
    header.append("} // namespace MaterialProbe::Reference")
    outputs = {
        "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/GgxEnergyTables.slang": "\n".join(shader) + "\n",
        "Tools/regression/principled_ggx_energy_tables.h": "\n".join(header) + "\n",
    }
    hashes = {}
    for name, data in outputs.items():
        (ROOT / name).write_text(data, encoding="utf-8", newline="\n")
        hashes[name] = hashlib.sha256(data.encode()).hexdigest()
    manifest = {"schema_version": 1, "blender_version": "5.1.1", "source_sha256": SOURCE_SHA,
                "source_url": "https://github.com/blender/blender/blob/v5.1.1/intern/cycles/scene/shader.tables",
                "tables": {source: size for source, _, size in TABLES}, "generated_sha256": hashes}
    (ROOT / "Tools/blender/fixtures/principled-layered-5.1.1/ggx-energy-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("GGX_ENERGY_TABLE_EXPORT_OK entries=5152 dataBytes=20608")


if __name__ == "__main__":
    main()
