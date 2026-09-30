"""Export the MAT-5 Volume input subset from Blender 5.1.1.

blender --background --factory-startup --python export-lx-volume-defaults.py -- OUTPUT.json
"""

import json
from pathlib import Path
import sys

import bpy


def main():
    if tuple(bpy.app.version) != (5, 1, 1):
        raise RuntimeError("Volume fixture requires Blender 5.1.1")
    arguments = sys.argv[sys.argv.index("--") + 1 :]
    if len(arguments) != 1:
        raise RuntimeError("Expected one output JSON path")

    material = bpy.data.materials.new("lx-special-schema")
    material.use_nodes = True
    node = material.node_tree.nodes.new("ShaderNodeVolumePrincipled")
    names = (
        "Color", "Density", "Anisotropy", "Absorption Color",
        "Emission Strength", "Emission Color",
    )
    data = {
        "blender_version": bpy.app.version_string,
        "blender_build_hash": bpy.app.build_hash.decode(),
        "type": node.bl_idname,
        "inputs": [],
    }
    for name in names:
        socket = node.inputs[name]
        value = socket.default_value
        data["inputs"].append({
            "identifier": socket.identifier,
            "default": list(value) if hasattr(value, "__len__") else value,
        })
    output = Path(arguments[0])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("SPECIAL_VOLUME_SCHEMA_EXPORTED sockets=6")


if __name__ == "__main__":
    main()
