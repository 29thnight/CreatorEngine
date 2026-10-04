"""Verify static GLB positions match the portable convex input exactly."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def verify(source, geometry):
    geometry_bytes = geometry.read_bytes()
    if len(geometry_bytes) < 52 or geometry_bytes[:8] != b"CECG\x01\x00\x00\x00" or struct.unpack_from("<I",geometry_bytes,32)[0] != 0:
        raise ValueError("Convex CECG v1 required")
    count = struct.unpack_from("<I",geometry_bytes,36)[0]
    if not 4 <= count <= 1024*1024 or len(geometry_bytes) != 48+count*12:
        raise ValueError("Invalid convex source extent")
    checksum = 14695981039346656037
    for byte in geometry_bytes[:-8]:
        checksum = ((checksum ^ byte) * 1099511628211) & ((1<<64)-1)
    if checksum != struct.unpack_from("<Q",geometry_bytes,len(geometry_bytes)-8)[0]:
        raise ValueError("Invalid convex checksum")
    extractor = Path(__file__).with_name("prepare-physics-static-glb-convex.py")
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory() as folder:
        subprocess.run([sys.executable,str(extractor),str(source),folder],check=True,capture_output=True,text=True)
        lines = (Path(folder)/"convex.txt").read_text().splitlines()
        expected = sorted(struct.pack("<fff",*(float(value) for value in line.split())) for line in lines[1:])
        actual = sorted(geometry_bytes[40+index*12:52+index*12] for index in range(count))
        if len(expected) != int(lines[0]) or expected != actual:
            raise ValueError("GLB left-handed metre positions differ from convex source")
        if source_hash != hashlib.sha256(source.read_bytes()).hexdigest() or geometry_bytes != geometry.read_bytes():
            raise ValueError("Input changed during source verification")
    return dict(result="PHYSICS_GLB_GEOMETRY_SOURCE_OK",sourceHash=source_hash,geometryHash=hashlib.sha256(geometry_bytes).hexdigest(),
                extractorHash=hashlib.sha256(extractor.read_bytes()).hexdigest(),points=count,selection="all primitives, static identity node, Z reflection, metres")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--geometry",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    options = parser.parse_args()
    result = verify(options.source,options.geometry)
    options.output.parent.mkdir(parents=True,exist_ok=True)
    options.output.write_text(json.dumps(result,indent=2),encoding="utf-8")
    print(json.dumps(result))
