"""One-time authoring migration. Validate every input before writing; keep exact original bytes in ZIP."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile
import uuid
import zipfile


def catalog(data):
    if not 1071 <= len(data) <= 65536 or data[:4] != b"CLYR":
        raise ValueError("Invalid CLYR size/magic")
    value = 14695981039346656037
    for byte in data[:-8]:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    if struct.unpack_from("<Q", data, len(data) - 8)[0] != value:
        raise ValueError("Invalid CLYR checksum")
    version, next_id, count = struct.unpack_from("<IQI", data, 4)
    if version != 1 or next_id < 2 or not 1 <= count <= 32:
        raise ValueError("Unsupported CLYR schema")
    offset, names, ids, slots = 20, {}, set(), set()
    default = False
    for _ in range(count):
        ident, slot, retired, length = struct.unpack_from("<QBBH", data, offset)
        offset += 12
        if not 0 < ident < next_id or ident in ids or slot >= 32 or slot in slots or retired > 1:
            raise ValueError("Invalid CLYR identity")
        if not 1 <= length <= 1024 or offset + length > len(data) - 1032:
            raise ValueError("Invalid CLYR name length")
        name = data[offset:offset + length].decode("utf-8")
        if any(ord(c) < 32 or ord(c) == 127 for c in name):
            raise ValueError("Invalid CLYR name")
        offset += length
        ids.add(ident)
        slots.add(slot)
        if not retired:
            if name in names:
                raise ValueError("Duplicate active layer name")
            names[name] = ident
        if slot == 0:
            default = ident == 1 and name == "Default" and not retired
    matrix = data[offset:-8]
    if not default or len(matrix) != 1024 or any(x > 1 for x in matrix):
        raise ValueError("Invalid CLYR matrix/default")
    if any(matrix[a * 32 + b] != matrix[b * 32 + a] for a in range(32) for b in range(32)):
        raise ValueError("Asymmetric CLYR matrix")
    return names


def scalar(value):
    value = value.strip()
    if value.startswith('"'):
        # JSON quoted names are a strict, unambiguous subset of YAML quoted scalars.
        parsed, end = json.JSONDecoder().raw_decode(value)
        if not isinstance(parsed, str) or value[end:].strip() and not value[end:].lstrip().startswith('#'):
            raise ValueError("Unsupported quoted layer name")
        return parsed
    if value.startswith("'"):
        match = re.fullmatch(r"'((?:[^']|'')*)'\s*(?:#.*)?", value)
        if not match:
            raise ValueError("Invalid single-quoted layer name")
        return match[1].replace("''", "'")
    if not value or value[0] in "|>[{&*!":
        raise ValueError("Unsupported layer scalar; author an explicit scalar first")
    return re.split(r"\s+#", value, maxsplit=1)[0].rstrip()


def convert(data, names):
    text = data.decode("utf-8-sig")
    legacy = re.compile(r"(?m)^([ \t]*)m_layer:([ \t]*)([^\r\n]*)(\r?\n|$)")
    ids = re.findall(r"(?m)^[ \t]*m_layerId:[ \t]*([^\r\n]+)", text)
    active = set(names.values())
    for value in ids:
        if not value.strip().isdigit() or int(value.strip()) not in active:
            raise ValueError("Unknown stable layer ID")
    if ids and legacy.search(text):
        raise ValueError("Mixed legacy/stable authoring schema; migrate a complete document")
    obsolete = re.compile(r"(?m)^[ \t]*m_collisionType:([ \t]*)([^\r\n]*)(\r?\n|$)")
    removed = 0
    def remove(match):
        nonlocal removed
        value = match[2].strip()
        if not value.isdigit() or int(value) > 0xffffffff:
            raise ValueError("Invalid legacy collision type")
        removed += 1
        return ""
    text = obsolete.sub(remove, text)
    if removed and not ids and not legacy.search(text):
        raise ValueError("Legacy collision type has no authoritative layer")
    changes = 0
    def replace(match):
        nonlocal changes
        name = scalar(match[3])
        if name not in names:
            raise ValueError(f"Unknown legacy layer: {name}")
        changes += 1
        return match[1] + "m_layerId:" + match[2] + str(names[name]) + match[4]
    result = legacy.sub(replace, text)
    changes = max(changes, removed)
    if not changes:
        return data, 0
    prefix = b"\xef\xbb\xbf" if data.startswith(b"\xef\xbb\xbf") else b""
    return prefix + result.encode("utf-8"), changes


def atomic_write(path, data):
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
        temp = Path(output.name)
        try:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        except BaseException:
            temp.unlink(missing_ok=True)
            raise
    try:
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)


def migrate(project, backup_dir, apply=False):
    project = Path(project).resolve()
    asset = project / "ProjectSetting/Layers.celayers"
    names = catalog(asset.read_bytes())
    prepared = []
    for path in sorted((project / "Assets").rglob("*")):
        if not path.is_file() or path.suffix.lower() not in {".creator", ".prefab"}:
            continue
        before = path.read_bytes()
        try:
            after, count = convert(before, names)
        except Exception as error:
            raise ValueError(f"{path}: {error}") from error
        if count:
            prepared.append((path, before, after, count))
    report = {"files": len(prepared), "entities": sum(item[3] for item in prepared), "applied": apply, "backup": None}
    if not apply or not prepared:
        return report
    backup_dir = Path(backup_dir).resolve()
    backup_dir.mkdir(parents=True, exist_ok=True)
    archive = backup_dir / ("entity-layer-migration-" + uuid.uuid4().hex + ".zip")
    manifest = []
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED) as backup:
        backup.write(asset, "ProjectSetting/Layers.celayers")
        for path, before, after, count in prepared:
            relative = path.relative_to(project).as_posix()
            backup.writestr(relative, before)
            manifest.append({"path": relative, "entities": count, "before": hashlib.sha256(before).hexdigest(),
                             "after": hashlib.sha256(after).hexdigest()})
        backup.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))
    written = []
    try:
        for path, before, after, _ in prepared:
            if path.read_bytes() != before:
                raise ValueError("Authoring source changed after preflight")
            atomic_write(path, after)
            written.append((path, before))
    except BaseException:
        for path, before in reversed(written):
            atomic_write(path, before)
        raise
    report["backup"] = str(archive)
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True)
    parser.add_argument("--backup-dir", required=True)
    parser.add_argument("--apply", action="store_true")
    options = parser.parse_args()
    print(json.dumps(migrate(options.project, options.backup_dir, options.apply), ensure_ascii=False))
