"""Scale X positions of a disposable GLB while retaining mesh/material names."""
import json
import pathlib
import struct
import sys

source, destination, factor = sys.argv[1], sys.argv[2], float(sys.argv[3])
raw = pathlib.Path(source).read_bytes()
length, kind = struct.unpack_from('<II', raw, 12)
assert kind == 0x4e4f534a
document = json.loads(raw[20:20 + length])
for index, mesh in enumerate(document['meshes']):
    mesh.setdefault('name', f'FoliageProbeMesh{index}')
offset = 20 + length
binary_length, kind = struct.unpack_from('<II', raw, offset)
assert kind == 0x004e4942
binary = bytearray(raw[offset + 8:offset + 8 + binary_length])
indices = {p['attributes']['POSITION'] for m in document['meshes'] for p in m['primitives']}
for index in indices:
    accessor = document['accessors'][index]
    assert accessor['type'] == 'VEC3' and accessor['componentType'] == 5126
    view = document['bufferViews'][accessor['bufferView']]
    start = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    stride = view.get('byteStride', 12)
    for item in range(accessor['count']):
        at = start + item * stride
        struct.pack_into('<f', binary, at, struct.unpack_from('<f', binary, at)[0] * factor)
    for bound in ('min', 'max'):
        if bound in accessor:
            accessor[bound][0] *= factor
text = json.dumps(document, separators=(',', ':')).encode()
text += b' ' * (-len(text) % 4)
output = struct.pack('<III', 0x46546c67, 2, 28 + len(text) + len(binary))
output += struct.pack('<II', len(text), 0x4e4f534a) + text
output += struct.pack('<II', len(binary), 0x004e4942) + binary
pathlib.Path(destination).write_bytes(output)
