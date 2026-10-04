"""Explicit legacy model reference preflight/apply on an isolated corpus copy."""
import argparse
import hashlib
import json
from pathlib import Path
import uuid
import zipfile
import yaml

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--source-project", required=True, type=Path)
p.add_argument("--project", required=True, type=Path)
p.add_argument("--receipt", required=True, type=Path)
p.add_argument("--backup", required=True, type=Path)
p.add_argument("--apply", action="store_true")
p.add_argument("--scene-only", action="store_true", help="Limit publication to Scene/Prefab documents; material migration remains pending")
a = p.parse_args()
source, project = a.source_project.resolve(), a.project.resolve()
if source == project or source in project.parents or project in source.parents:
    raise ValueError("Independent copy required")
models, remap, retired, bindings = {}, {}, set(), []
inputs = []
for name in ("DX12ValidationPrimitives.glb", "Phase17_Infinian.glb", "Phase17_Sponza.glb"):
    relative = Path("Assets/Models") / name
    old_path, new_path = Path(str(source / relative) + ".meta"), Path(str(project / relative) + ".meta")
    if old_path.is_symlink() or new_path.is_symlink() or not new_path.resolve().is_relative_to(project):
        raise ValueError("Sidecar path escapes project")
    old_bytes, new_bytes = old_path.read_bytes(), new_path.read_bytes()
    inputs.extend(((old_path, old_bytes), (new_path, new_bytes)))
    old, new = yaml.safe_load(old_bytes), yaml.safe_load(new_bytes)
    digest = hashlib.sha256((project / relative).read_bytes()).hexdigest()
    if digest != hashlib.sha256((source / relative).read_bytes()).hexdigest() or new["sourceFingerprint"] != "sha256:" + digest:
        raise ValueError("Model source fingerprint mismatch")
    if uuid.UUID(new["assetId"]).version != 8:
        raise ValueError("Target model identity is not UUIDv8")
    models[old["guid"]] = new
    remap[old["guid"]] = new["assetId"]
    retired.add(old["guid"])
    for category in ("materials", "embeddedTextures"):
        for item in old.get("subAssets", {}).get(category, []):
            if not item.get("guid"):
                continue
            retired.add(item["guid"])
            matches = [target for target in new["subAssets"]
                       if target.get("binding") == item.get("key") and target.get("name") == item.get("name")]
            if len(matches) == 1:
                remap[item["guid"]] = matches[0]["assetId"]
    bindings.append(dict(path=relative.as_posix(), old=old["guid"], new=new["assetId"], sourceSha256=digest,
                         oldMetaSha256=hashlib.sha256(old_bytes).hexdigest(), newMetaSha256=hashlib.sha256(new_bytes).hexdigest()))

prepared, typed = [], []
def visit(node):
    if isinstance(node, list):
        return [visit(item) for item in node]
    if not isinstance(node, dict):
        if isinstance(node, str):
            if node in retired and node not in remap:
                raise ValueError("Unresolved legacy subasset reference: " + node)
            return remap.get(node, node)
        return node
    node = dict(node)
    legacy_model = node.get("m_Material", {}).get("m_fileGuid") if isinstance(node.get("m_Material"), dict) else None
    if "MeshRenderer" in node and legacy_model in models:
        model = models[legacy_model]
        mesh_name = node.get("m_Mesh", {}).get("m_name")
        matches = [item for item in model["subAssets"] if item["kind"] == "mesh" and item.get("name") == mesh_name]
        if len(matches) != 1:
            raise ValueError("Mesh name is missing or ambiguous: " + str(mesh_name))
        node["m_modelGuid"], node["m_meshAssetId"] = model["assetId"], matches[0]["assetId"]
        typed.append(dict(componentId=node.get("m_instanceID"), model=node["m_modelGuid"], mesh=node["m_meshAssetId"]))
    return {key: visit(value) for key, value in node.items()}

for path in sorted((project / "Assets").rglob("*")):
    if path.suffix.lower() not in {".creator", ".prefab", ".asset", ".shadergraph"} or not path.is_file():
        continue
    if a.scene_only and path.suffix.lower() not in {".creator", ".prefab"}:
        continue
    if path.is_symlink() or not path.resolve().is_relative_to(project):
        raise ValueError("Corpus path escapes project")
    before = path.read_bytes()
    if not any(value.encode() in before for value in retired):
        continue
    if before.startswith(b"CEMA"):
        raise ValueError("Native CEMA reference migration required: " + str(path))
    after = yaml.safe_dump(visit(yaml.safe_load(before)), sort_keys=False, allow_unicode=True).encode()
    if any(value.encode() in after for value in retired):
        raise ValueError("Unresolved embedded legacy reference: " + str(path))
    prepared.append((path, before, after))
receipt = dict(result="CORPUS_MODEL_REFERENCES_PREFLIGHT_OK", bindings=bindings, mappings=remap,
               files=len(prepared), scope="scene_prefab" if a.scene_only else "full_text_corpus", typedMeshBindings=typed, applied=False)
if any(path.read_bytes() != data for path, data in inputs):
    raise ValueError("Identity policy inputs changed during preflight")
if a.apply:
    a.backup.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(a.backup, "x", zipfile.ZIP_DEFLATED) as archive:
        for path, before, after in prepared:
            archive.writestr(path.relative_to(project).as_posix(), before)
        archive.writestr("manifest.json", json.dumps(receipt))
    written = []
    try:
        for path, before, after in prepared:
            if path.read_bytes() != before:
                raise ValueError("Corpus changed after preflight")
            written.append((path, before))
            path.write_bytes(after)
    except BaseException:
        for path, before in written:
            path.write_bytes(before)
        raise
    receipt.update(result="CORPUS_MODEL_REFERENCES_APPLIED", applied=True)
a.receipt.parent.mkdir(parents=True, exist_ok=True)
a.receipt.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
print(json.dumps({key: receipt[key] for key in ("result", "files", "applied")}))
