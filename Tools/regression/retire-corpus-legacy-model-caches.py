"""Back up and retire unreferenced CEMA model caches from an isolated project copy."""
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
p.add_argument("--backup", required=True, type=Path)
p.add_argument("--receipt", required=True, type=Path)
p.add_argument("--apply", action="store_true")
a = p.parse_args()
source, project = a.source_project.resolve(), a.project.resolve()
if source == project or source in project.parents or project in source.parents:
    raise ValueError("Independent project copy required")
prepared, patterns = {}, []
for name in ("Phase17_Infinian.asset", "Phase17_Sponza.asset"):
    cache = project / "Assets/Models" / name
    meta = Path(str(cache) + ".meta")
    for path in (cache, meta):
        if path.is_symlink() or not path.resolve().is_relative_to(project):
            raise ValueError("Cache path escapes copy")
        data = path.read_bytes()
        if data != (source / path.relative_to(project)).read_bytes():
            raise ValueError("Cache differs from pinned source")
        prepared[path] = data
    if not prepared[cache].startswith(b"CEMA"):
        raise ValueError("Expected legacy model cache")
    identity = uuid.UUID(yaml.safe_load(prepared[meta])["guid"])
    patterns.extend((str(identity).encode(), str(identity).upper().encode(), identity.bytes, identity.bytes_le))
for path in (project / "Assets").rglob("*"):
    if not path.is_file() or path in prepared:
        continue
    if path.is_symlink() or not path.resolve().is_relative_to(project):
        raise ValueError("Corpus path escapes copy")
    data = path.read_bytes()
    if any(pattern in data for pattern in patterns):
        raise ValueError("Legacy cache is still referenced: " + str(path))
records = [dict(path=path.relative_to(project).as_posix(), sha256=hashlib.sha256(data).hexdigest())
           for path, data in prepared.items()]
if a.apply:
    a.backup.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(a.backup, "x", zipfile.ZIP_DEFLATED) as archive:
        for path, data in prepared.items():
            archive.writestr(path.relative_to(project).as_posix(), data)
    with zipfile.ZipFile(a.backup) as archive:
        for path, data in prepared.items():
            if archive.read(path.relative_to(project).as_posix()) != data or path.read_bytes() != data:
                raise ValueError("Exact backup/prepublication verification failed")
    removed = []
    try:
        for path in prepared:
            path.unlink()
            removed.append(path)
    except BaseException:
        for path in removed:
            path.write_bytes(prepared[path])
        raise
receipt = dict(result="LEGACY_MODEL_CACHE_RETIREMENT_OK", files=records, applied=a.apply,
               referenceOccurrences=0, originalImmutable=all((source / path.relative_to(project)).read_bytes() == data
                                                            for path, data in prepared.items()))
a.receipt.parent.mkdir(parents=True, exist_ok=True)
a.receipt.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
print(json.dumps({key: value for key, value in receipt.items() if key != "files"}))
