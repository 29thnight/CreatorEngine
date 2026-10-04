"""Validate explicit geometry recovery bindings; read-only, not migration authorization."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import uuid
import yaml

spec = importlib.util.spec_from_file_location("migration", Path(__file__).with_name("migrate-physics-schema.py"))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def read_bound(project, record):
    if set(record) != {"path", "sha256"} or not isinstance(record["path"], str):
        raise ValueError("Dependency requires path and SHA-256")
    relative = Path(record["path"])
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("Dependency must stay inside project")
    path = project / relative
    if not path.resolve().is_relative_to(project) or path.is_symlink():
        raise ValueError("Dependency escapes project")
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != record["sha256"]:
        raise ValueError("Dependency source hash changed")
    return data


def validate(project, policy, native_receipts=None, require_shape_policy=False):
    project = Path(project).resolve()
    if set(policy) != {"schema", "bindings"} or type(policy["schema"]) is not int or policy["schema"] != 1 or not isinstance(policy["bindings"], list):
        raise ValueError("Invalid geometry migration policy")
    results, seen = [], set()
    for binding in policy["bindings"]:
        if set(binding) - {"shapePolicy"} != {"authoring", "componentId", "componentSha256", "supplierSha256", "source", "geometry", "geometryMeta", "geometryUUID", "geometryRevision", "geometryKind"}:
            raise ValueError("Unknown or missing binding fields")
        if require_shape_policy or "shapePolicy" in binding:
            shape = binding.get("shapePolicy")
            if not isinstance(shape, dict) or set(shape) != {"localPosition", "localRotation", "geometryScale", "staticFriction", "dynamicFriction", "restitution", "sourceSelection"}:
                raise ValueError("Explicit geometry shape policy required")
            if not isinstance(shape["sourceSelection"], str) or not shape["sourceSelection"].strip():
                raise ValueError("Reviewed source/submesh or height sampling choice required")
            m.vector(shape["localPosition"])
            rotation = m.vector(shape["localRotation"],4)
            if abs(sum(value*value for value in rotation.values())-1) > .001:
                raise ValueError("Non-unit reviewed geometry rotation")
            if any(value <= 0 for value in m.vector(shape["geometryScale"]).values()):
                raise ValueError("Positive geometry scale required")
            m.number(shape["staticFriction"])
            m.number(shape["dynamicFriction"])
            if m.number(shape["restitution"]) > 1:
                raise ValueError("Restitution above one")
        authoring = read_bound(project, binding["authoring"])
        key = (binding["authoring"]["path"], binding["componentId"])
        if type(binding["componentId"]) is not int or key in seen:
            raise ValueError("Invalid or duplicate component binding")
        seen.add(key)
        candidates = [item for item in m.geometry_recovery_requirements(authoring) if item["componentId"] == binding["componentId"]]
        if len(candidates) != 1 or candidates[0]["sourceSha256"] != binding["componentSha256"]:
            raise ValueError("Reviewed legacy component changed or ambiguous")
        requirement = candidates[0]
        if requirement["legacyType"] == "RagdollComponent":
            raise ValueError("Ragdoll body/joint policy is separate")
        suppliers = requirement["supplierCandidates"]
        if len(suppliers) != 1 or hashlib.sha256(json.dumps(suppliers[0],sort_keys=True,separators=(",", ":"),allow_nan=False).encode()).hexdigest() != binding["supplierSha256"]:
            raise ValueError("Reviewed geometry supplier changed or ambiguous")
        read_bound(project, binding["source"])
        geometry = read_bound(project, binding["geometry"])
        meta = yaml.load(read_bound(project, binding["geometryMeta"]).decode("utf-8-sig"), Loader=m.StrictLoader)
        if len(geometry) < 52 or len(geometry) > 64*1024*1024 or geometry[:8] != b"CECG\x01\x00\x00\x00":
            raise ValueError("Expected portable CECG v1 source")
        checksum = 14695981039346656037
        for byte in geometry[:-8]:
            checksum = ((checksum ^ byte) * 1099511628211) & ((1 << 64)-1)
        if checksum != struct.unpack_from("<Q", geometry, len(geometry)-8)[0]:
            raise ValueError("Geometry checksum mismatch")
        asset = uuid.UUID(bytes=geometry[8:24])
        revision, kind = struct.unpack_from("<QI", geometry,24)
        expected_kind = 0 if requirement["legacyType"] == "MeshColliderComponent" else 2
        if type(binding["geometryKind"]) is not int or binding["geometryKind"] != expected_kind or kind != expected_kind:
            raise ValueError("Legacy mesh requires convex; Terrain requires heightfield")
        if type(binding["geometryRevision"]) is not int or not revision or revision != binding["geometryRevision"] or not asset.int or str(asset) != binding["geometryUUID"]:
            raise ValueError("Geometry UUID/revision mismatch")
        if not isinstance(meta, dict) or meta.get("guid") != str(asset) or type(meta.get("geometryRevision")) is not int or meta["geometryRevision"] != revision:
            raise ValueError("Geometry meta identity mismatch")
        status = "identity_bound_native_validation_pending"
        remaining = ["reviewed material, pose, scale and cook policy", "native source decode and SDK cook acceptance", "shape conversion and cooked Player acceptance"]
        if native_receipts is not None:
            receipts = [receipt for receipt in native_receipts if receipt.get("sourceHash", "").lower() == binding["geometry"]["sha256"]]
            if not receipts or any(receipt.get("result") != "PHYSICS_GEOMETRY_MIGRATION_NATIVE_OK" or receipt.get("revision") != revision or receipt.get("kind") != kind or not receipt.get("cookedBytes", 0) or receipt.get("configuration") not in {"Debug", "Release", "Shipping", "ASan"} or len(receipt.get("executableHash", "")) != 64 for receipt in receipts):
                raise ValueError("Matching native decode/cook/import receipt required")
            status = "native_source_cook_import_verified_shape_conversion_pending"
            remaining.remove("native source decode and SDK cook acceptance")
        results.append(dict(componentId=binding["componentId"],geometryUUID=str(asset),geometryRevision=revision,
                            status=status, remaining=remaining))
    return dict(result="PHYSICS_GEOMETRY_BINDINGS_OK",bindings=results,applied=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--native-receipt", type=Path, action="append", help="Source-hashed native validation receipt; shape conversion remains separate")
    options = parser.parse_args()
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate policy field")
            result[key] = value
        return result
    print(json.dumps(validate(options.project, json.loads(options.policy.read_text(encoding="utf-8"),object_pairs_hook=unique), [json.loads(path.read_text(encoding="utf-8-sig"),object_pairs_hook=unique) for path in options.native_receipt] if options.native_receipt else None),indent=2))
