"""Prove the specific CEGE 9 -> 10 bounds-only migration from captured blobs."""
import hashlib
import math
import struct


def prove_bounds_recook(left, right, root):
    blobs = {}
    for path in root.rglob("*.cege"):
        data = path.read_bytes()
        blobs[hashlib.sha256(data).digest()] = (path, data)

    def artifacts(manifest, version):
        result = {}
        for draw in manifest["draws"]:
            if draw["artifactSchema"] != version:
                raise ValueError("Bounds recook proof requires CEGE 9 -> 10 only")
            digest = bytes(draw["contentSha256"])
            path, data = blobs[digest]
            if path.stem != draw["assetId"] or len(data) != draw["artifactBytes"]:
                raise ValueError("Captured geometry identity does not match its blob")
            if data[:4] != b"CEGE" or struct.unpack_from("<I", data, 4)[0] != version:
                raise ValueError("Unsupported geometry envelope")
            previous = result.setdefault(draw["assetId"], (digest, data, path))
            if previous[0] != digest:
                raise ValueError("Multiple geometry revisions for one asset")
        return result

    old, new = artifacts(left, 9), artifacts(right, 10)
    if old.keys() != new.keys():
        raise ValueError("Recook changed the geometry asset set")
    proofs, replacements = [], {}
    for asset, (old_hash, a, old_path) in old.items():
        new_hash, b, new_path = new[asset]
        a_end = 12 + struct.unpack_from("<I", a, 8)[0]
        b_end = 12 + struct.unpack_from("<I", b, 8)[0]
        if not 100 <= a_end < len(a) or not 100 <= b_end < len(b):
            raise ValueError("Invalid geometry metadata extent")
        bones = struct.unpack_from("<I", a, 96)[0]
        count = bones + 1 if bones else 0
        added = 4 + count * 32
        if bones > 65536 or b_end - a_end != added or a[a_end:] != b[b_end:]:
            raise ValueError("Recook changed compressed geometry pages or metadata size")
        aa, bb = a[12:a_end], b[12:b_end]
        offsets = []
        for offset in range(88, len(aa) - 3, 4):
            if struct.unpack_from("<I", bb, offset)[0] != count:
                continue
            if aa[:offset] == bb[:offset] and aa[offset:] == bb[offset + added:]:
                offsets.append(offset)
        if len(offsets) != 1:
            raise ValueError("Cannot prove a unique bounds-only metadata insertion")
        offset = offsets[0]
        for index in range(count):
            box = struct.unpack_from("<8f", bb, offset + 4 + index * 32)
            if not all(map(math.isfinite, box)) or box[3] not in (0, 1) or box[3] != box[7]:
                raise ValueError("Invalid added bone bounds")
            if any(box[i] > box[i + 4] for i in range(3)):
                raise ValueError("Inverted added bone bounds")
            if box[3] == 0 and any(box):
                raise ValueError("Noncanonical unused bone bounds")
        replacements[new_hash.hex()] = (list(old_hash), len(a))
        proofs.append(dict(assetId=asset, oldBlob=str(old_path), newBlob=str(new_path),
                           oldSha256=old_hash.hex(), newSha256=new_hash.hex(),
                           identicalGeometryPages=True, identicalOriginalMetadata=True,
                           addedBounds=count, addedBytes=added))
    return replacements, proofs
