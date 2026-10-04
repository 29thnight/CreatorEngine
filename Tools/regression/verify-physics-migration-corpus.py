"""Verify migration and exact backup restoration on a copy of the current corpus."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--project", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--character-policy", type=Path)
args = parser.parse_args()
policy_bytes = args.character_policy.read_bytes() if args.character_policy else None
policy = json.loads(policy_bytes.decode("utf-8-sig")) if policy_bytes else None
project = args.project.resolve()
spec = importlib.util.spec_from_file_location("migration", Path(__file__).with_name("migrate-physics-schema.py"))
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)
paths = sorted(path for path in (project / "Assets").rglob("*")
               if path.is_file() and path.suffix.lower() in {".creator", ".prefab"})
paths.append(project / "ProjectSetting/Layers.celayers")
original = {}
for path in paths:
    if path.is_symlink() or not path.resolve().is_relative_to(project):
        raise ValueError("Corpus path escapes project")
    original[path.relative_to(project).as_posix()] = path.read_bytes()

with tempfile.TemporaryDirectory(prefix="physics-corpus-") as directory:
    copy = Path(directory) / "Project"
    for relative, data in original.items():
        target = copy / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    backup = Path(directory) / "Backup"
    # Explicit default-limit acceptance is restricted to this disposable copy.
    preflight = migration.migrate(copy, backup, reset_limits=True, character_policy=policy)
    blocked = bool(preflight["diagnostics"])
    applied = migration.migrate(copy, backup, apply=True, reset_limits=True, character_policy=policy)
    if blocked and (applied["applied"] or applied["backup"]):
        raise ValueError("Blocked corpus published partial output")
    post = migration.migrate(copy, backup, character_policy=policy)
    if not blocked and (post["diagnostics"] or post["files"]):
        raise ValueError("Converted corpus is not migration-idempotent")
    if applied["backup"]:
        with zipfile.ZipFile(applied["backup"]) as archive:
            manifest = json.loads(archive.read("manifest.json"))
            for entry in manifest:
                relative = entry["path"]
                data = archive.read(relative)
                if data != original[relative]:
                    raise ValueError("Backup source mismatch")
                if hashlib.sha256((copy / relative).read_bytes()).hexdigest() != entry["after"]:
                    raise ValueError("Published hash mismatch")
                (copy / relative).write_bytes(data)
    if any((copy / relative).read_bytes() != data for relative, data in original.items()):
        raise ValueError("Exact corpus restore failed")
    if any((project / relative).read_bytes() != data for relative, data in original.items()):
        raise ValueError("Original project changed during verification")
    if args.character_policy and args.character_policy.read_bytes() != policy_bytes:
        raise ValueError("Character policy changed during verification")
    result = dict(result="PHYSICS_MIGRATION_CORPUS_BLOCKED" if blocked else "PHYSICS_MIGRATION_CORPUS_OK", project=str(project),
                  inspectedFiles=len(paths) - 1, convertedFiles=applied["files"] if applied["applied"] else 0, preparedFiles=preflight["files"],
                  sourceImmutable=True, exactRestore=None if blocked else True, corpusBytesUnchanged=True, idempotent=None if blocked else True, allOrNothing=True,
                  appliedTo="temporary_copy", characterPolicySha256=hashlib.sha256(policy_bytes).hexdigest() if policy_bytes else None, preflight=preflight)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({key: value for key, value in result.items() if key != "preflight"}))
