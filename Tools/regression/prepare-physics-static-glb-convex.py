"""Extract actual static GLB positions in the engine's left-handed metre coordinates.

This regression utility deliberately rejects skins, animations, transformed nodes,
sparse accessors and external buffers rather than silently changing their meaning.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
data = args.source.read_bytes()
magic, version, length = struct.unpack_from("<III", data)
if magic != 0x46546C67 or version != 2 or length != len(data):
    raise ValueError("Expected valid GLB v2")
size, kind = struct.unpack_from("<II", data, 12)
if kind != 0x4E4F534A:
    raise ValueError("Expected JSON chunk")
asset = json.loads(data[20:20+size])
bin_size, bin_kind = struct.unpack_from("<II", data, 20+size)
if bin_kind != 0x004E4942 or 28+size+bin_size != len(data):
    raise ValueError("Expected one embedded binary chunk")
blob = data[28+size:]
if asset.get("skins") or asset.get("animations") or len(asset["nodes"]) != 1 or len(asset["meshes"]) != 1:
    raise ValueError("Regression input must contain one static mesh/node")
if set(asset["nodes"][0]) - {"name", "mesh"} or asset["nodes"][0]["mesh"] != 0:
    raise ValueError("Regression input must have identity node transform")
points = []
for primitive in asset["meshes"][0]["primitives"]:
    accessor = asset["accessors"][primitive["attributes"]["POSITION"]]
    view = asset["bufferViews"][accessor["bufferView"]]
    if accessor["componentType"] != 5126 or accessor["type"] != "VEC3" or "sparse" in accessor or view["buffer"] != 0:
        raise ValueError("Expected embedded float POSITION accessor")
    for index in range(accessor["count"]):
        offset = view.get("byteOffset", 0)+accessor.get("byteOffset", 0)+index*view.get("byteStride", 12)
        if offset+12 > view.get("byteOffset", 0)+view["byteLength"]:
            raise ValueError("POSITION exceeds buffer view")
        x, y, z = struct.unpack_from("<fff", blob, offset)
        points.append((x, y, -z))
unique = sorted(set(points))
args.output.mkdir(parents=True, exist_ok=True)
(args.output/"convex.txt").write_text(str(len(unique))+"\n"+"\n".join(" ".join(format(v, ".9g") for v in p) for p in unique), encoding="utf-8")
report = {"source": str(args.source.resolve()), "sha256": hashlib.sha256(data).hexdigest(),
          "vertices": len(points), "uniquePoints": len(unique), "conversion": "Z reflection, identity transform, metres",
          "min": [min(p[a] for p in unique) for a in range(3)], "max": [max(p[a] for p in unique) for a in range(3)]}
(args.output/"source.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report))
