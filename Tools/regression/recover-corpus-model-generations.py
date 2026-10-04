"""Rebuild model generations only in an isolated project and audit authored identity."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import yaml

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-project", required=True, type=Path)
parser.add_argument("--copy-project", required=True, type=Path)
parser.add_argument("--cooker", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
source = args.source_project.resolve()
copy = args.copy_project.resolve()
if source == copy or source in copy.parents or copy in source.parents:
    raise ValueError("Recovery requires independent source and copy projects")
records = []
blocked = []
for model in sorted((copy / "Assets").rglob("*")):
    if not model.is_file() or model.suffix.lower() not in {".fbx", ".glb", ".gltf"}:
        continue
    relative = model.relative_to(copy)
    original_meta = Path(str(source / relative) + ".meta")
    original_bytes = original_meta.read_bytes()
    before = yaml.safe_load(original_bytes)
    if not before.get("assetId") or not isinstance(before.get("subAssets"), list):
        Path(str(model) + ".meta").write_bytes(original_bytes)
        blocked.append(dict(path=relative.as_posix(), reason="Legacy model identity requires explicit reference migration", originalMetaSha256=hashlib.sha256(original_bytes).hexdigest()))
        continue
    digest = hashlib.sha256(model.read_bytes()).hexdigest()
    if digest != hashlib.sha256((source / relative).read_bytes()).hexdigest():
        raise ValueError("Copied model source changed")
    process = subprocess.run([str(args.cooker.resolve()), "--author-model-asset",
                              "--asset-root", str(copy / "Assets"), "--output",
                              str(copy / "Library/ModelAssetGenerations"), "--model", str(model)],
                             capture_output=True, text=True, encoding="utf-8", errors="replace")
    if process.returncode:
        raise ValueError(process.stdout + process.stderr)
    after = yaml.safe_load(Path(str(model) + ".meta").read_bytes())
    for field in ("assetId", "authoringKey", "identityProfile", "identityEpoch", "sourceFingerprint"):
        if before.get(field) != after.get(field):
            raise ValueError(f"Authored {field} changed: {relative}")
    identities = lambda meta: sorted((item["kind"], item["stableKey"], item["assetId"])
                                     for item in meta.get("subAssets", []))
    if identities(before) != identities(after):
        raise ValueError(f"Subasset identity changed: {relative}")
    if original_meta.read_bytes() != original_bytes:
        raise ValueError("Original metadata changed")
    records.append(dict(path=relative.as_posix(), sourceSha256=digest, assetId=after["assetId"],
                        oldGeneration=before["generation"], generation=after["generation"],
                        identitiesPreserved=True, cookerOutput=process.stdout.strip()))
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(dict(result="CORPUS_MODEL_GENERATIONS_BLOCKED" if blocked else "CORPUS_MODEL_GENERATIONS_OK", models=records, blocked=blocked,
                                      originalImmutable=True), indent=2), encoding="utf-8")
print(f"CORPUS_MODEL_GENERATIONS_AUDITED models={len(records)} blocked={len(blocked)} originalImmutable=true")
