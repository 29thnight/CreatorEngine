"""Unrun opt-in CECT export, generation fingerprint, and texture identity regression."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import struct
import yaml

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--project", required=True, type=Path)
p.add_argument("--cooker", required=True, type=Path)
p.add_argument("--work", required=True, type=Path)
args = p.parse_args()
project = args.project.resolve()
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=False)
generations = project / "Library/ModelAssetGenerations"
cases = {}
for model in sorted((project / "Assets").rglob("*")):
    if model.suffix.lower() not in {".glb", ".fbx", ".gltf"}:
        continue
    meta = yaml.safe_load(Path(str(model) + ".meta").read_bytes())
    if not meta.get("assetId"):
        continue
    folder = generations / meta["assetId"] / str(meta["generation"])
    for extension in (".jpg", ".png"):
        if extension not in cases and list((folder / "textures").glob("*" + extension)):
            cases[extension] = (model, folder, meta)
if len(cases) != 2:
    raise ValueError("JPG and PNG generation fixtures required")

def run(model, root, output):
    result = subprocess.run([str(args.cooker.resolve()), "--asset-root", str(project / "Assets"),
                             "--output", str(output), "--generation-root", str(root), "--model", str(model)],
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    output.with_suffix(".log").write_text(result.stdout + result.stderr, encoding="utf-8")
    return result

def read_cooked_document(path):
    """Read the bounded CEDO1 tree emitted by AuthoringCookedDocument.cpp."""
    data = path.read_bytes()
    if not 16 <= len(data) <= 64 * 1024 * 1024:
        raise ValueError("Invalid CEDO document size")
    magic, version, reserved, expected_nodes, payload_size = struct.unpack_from("<4sHHII", data)
    if (magic, version, reserved, payload_size) != (b"CEDO", 1, 0, len(data) - 16):
        raise ValueError("Exported generation record is not canonical CEDO1")
    if not 0 < expected_nodes <= 1_000_000:
        raise ValueError("Invalid CEDO node count")
    offset, nodes = 16, 0

    def node(depth, map_child=False):
        nonlocal offset, nodes
        if depth > 512 or offset + 16 > len(data) or nodes >= expected_nodes:
            raise ValueError("Truncated or excessive CEDO tree")
        kind, flags, reserved, key_size, value_size, children = struct.unpack_from("<BBHIII", data, offset)
        offset += 16
        nodes += 1
        if (flags or reserved or key_size > 16 * 1024 * 1024 or value_size > 16 * 1024 * 1024
                or children > expected_nodes or offset + key_size + value_size > len(data)):
            raise ValueError("Invalid CEDO node header")
        key = data[offset:offset + key_size].decode("utf-8")
        offset += key_size
        value = data[offset:offset + value_size].decode("utf-8")
        offset += value_size
        if bool(key_size) != map_child:
            raise ValueError("Invalid CEDO child key")
        if kind in (1, 2):
            if children or (kind == 1 and value_size):
                raise ValueError("Invalid CEDO scalar/null")
            return key, None if kind == 1 else value
        if kind not in (3, 4) or value_size:
            raise ValueError("Invalid CEDO container")
        result = {} if kind == 3 else []
        for _ in range(children):
            child_key, child = node(depth + 1, kind == 3)
            if kind == 3:
                if child_key in result:
                    raise ValueError("Duplicate CEDO map key")
                result[child_key] = child
            else:
                result.append(child)
        return key, result

    _, result = node(0)
    if offset != len(data) or nodes != expected_nodes or not isinstance(result, dict):
        raise ValueError("CEDO tree length/count/root mismatch")
    return result


def validate_cooked_texture(data):
    """Check the fixed CECT2 wire schema and every exact subresource range."""
    if not 64 <= len(data) <= 1024 * 1024 * 1024:
        raise ValueError("Invalid CECT artifact size")
    (magic, schema, header_size, representation, format_id, flags, width, height,
     mips, arrays, count, entry_size, payload_offset, total_size) = struct.unpack_from("<4s11I2Q", data)
    if (magic, schema, header_size, representation, entry_size) != (b"CECT", 2, 64, 2, 40):
        raise ValueError("Expected GPU-ready CECT representation/schema 2")
    if (flags & ~7 or not 1 <= width <= 16384 or not 1 <= height <= 16384
            or not 1 <= arrays <= 2048 or not 1 <= mips <= max(width, height).bit_length()
            or count != arrays * mips or (flags & 1 and (width != height or arrays % 6))):
        raise ValueError("Invalid CECT texture shape or flags")
    # Stable wire formats, deliberately independent of the RHI enum ordinals.
    layouts = {1: (1, 4), 2: (1, 4), 3: (1, 4), 4: (1, 4),
               5: (1, 8), 6: (1, 16), 7: (4, 8), 8: (4, 8),
               9: (4, 16), 10: (4, 16), 11: (4, 16), 12: (4, 16), 13: (4, 16)}
    if format_id not in layouts:
        raise ValueError("Unknown CECT neutral pixel format")
    block, block_bytes = layouts[format_id]
    if block == 4 and (width % 4 or height % 4):
        raise ValueError("Unsupported block-compressed CECT base extent")
    if payload_offset != 64 + count * 40 or payload_offset > len(data) or total_size != len(data):
        raise ValueError("Invalid CECT table or total byte count")
    expected_offset = payload_offset
    for index in range(count):
        sub_width, sub_height, row, size, offset, payload_size = struct.unpack_from("<II4Q", data, 64 + index * 40)
        mip = index % mips
        expected_width, expected_height = max(1, width >> mip), max(1, height >> mip)
        expected_row = ((expected_width + block - 1) // block) * block_bytes
        expected_size = expected_row * ((expected_height + block - 1) // block)
        if ((sub_width, sub_height, row, size, offset, payload_size)
                != (expected_width, expected_height, expected_row, expected_size, expected_offset, expected_size)
                or offset + payload_size > len(data)):
            raise ValueError("Invalid CECT subresource pitch, extent or payload range")
        expected_offset += expected_size
    if expected_offset != len(data):
        raise ValueError("Trailing CECT bytes")


payloads = 0
for extension, (model, folder, meta) in cases.items():
    output = work / extension[1:]
    result = run(model, generations, output)
    if result.returncode:
        raise ValueError(result.stdout + result.stderr)
    exported_root = output / "Derived/Models" / meta["assetId"][:2] / meta["assetId"] / str(meta["generation"])
    original_record = yaml.safe_load((folder / "generation.asset").read_bytes())
    record = read_cooked_document(exported_root / "generation.asset")
    for field in ("assetId", "generation", "identityProfile", "identityEpoch", "sourceFingerprint"):
        if str(record[field]) != str(original_record[field]):
            raise ValueError("Export changed generation identity field: " + field)
    if record["assetId"] != meta["assetId"] or int(record["generation"]) != meta["generation"]:
        raise ValueError("Exported identity/generation does not match the authored sidecar")
    identity_fields = lambda entry: (entry["kind"], entry["stableKey"], entry["assetId"])
    if ([identity_fields(entry) for entry in record["subAssets"]]
            != [identity_fields(entry) for entry in original_record["subAssets"]]):
        raise ValueError("Export changed logical subasset identities or their order")
    sidecar_bytes = (exported_root / "sidecar.meta").read_bytes()
    if record["sidecarFingerprint"] != "sha256:" + hashlib.sha256(sidecar_bytes).hexdigest():
        raise ValueError("Exported CEDO sidecar fingerprint is stale")
    expected_files = set()
    for entry in record["subAssets"]:
        if entry["kind"] != "texture":
            continue
        relative = "textures/" + entry["assetId"] + ".cetex"
        if entry["artifactPath"] != relative or relative in expected_files:
            raise ValueError("Cooked texture path lost independent logical identity")
        expected_files.add(relative)
        data = (exported_root / relative).read_bytes()
        validate_cooked_texture(data)
        if entry["artifactFingerprint"] != "sha256:" + hashlib.sha256(data).hexdigest():
            raise ValueError("Generation record did not update the cooked texture fingerprint")
        damaged = bytearray(data)
        struct.pack_into("<I", damaged, 4, 1)
        try:
            validate_cooked_texture(damaged)
        except ValueError:
            pass
        else:
            raise ValueError("Legacy source-image schema mutation was accepted")
        payloads += 1
    actual_files = {path.relative_to(exported_root).as_posix()
                    for path in (exported_root / "textures").iterdir() if path.is_file()}
    if actual_files != expected_files or not expected_files:
        raise ValueError("Export retained source images or omitted/duplicated cooked texture payloads")

model, folder, meta = cases[".jpg"]
negative_root = work / "ambiguous-generations"
negative = negative_root / meta["assetId"] / str(meta["generation"])
shutil.copytree(folder, negative)
original = next((negative / "textures").glob("*.jpg"))
shutil.copyfile(original, original.with_suffix(".png"))
result = run(model, negative_root, work / "ambiguous")
if result.returncode == 0 or "ambiguous payload files" not in result.stdout + result.stderr:
    raise ValueError("Ambiguous texture identity was accepted")
receipt = dict(result="MODEL_TEXTURE_EXPORT_OK", payloads=payloads, jpg=True, png=True,
               cookedSchema=2, cookedRepresentation=2, sourceBytesPreserved=False,
               identitiesPreserved=True, generationFingerprintsVerified=True,
               legacySchemaRejected=True, ambiguousRejected=True)
(work / "result.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
print(json.dumps(receipt))
