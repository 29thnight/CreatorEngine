import importlib.util
import json
from pathlib import Path
import tempfile
import zipfile

spec = importlib.util.spec_from_file_location("migration", Path(__file__).with_name("migrate-entity-layer-ids.py"))
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)
checks = 0

def check(condition, label):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(label)

def rejects(callback):
    try:
        callback()
    except (ValueError, UnicodeError, Exception):
        return True
    return False

repo = Path(__file__).resolve().parents[2]
source = repo / "Dynamic_CPP/ProjectSetting/Layers.celayers"
asset = source.read_bytes()
names = migration.catalog(asset)
check(names["Default"] == 1, "native default ID")
for index in range(len(asset)):
    altered = bytearray(asset)
    altered[index] ^= 1
    check(rejects(lambda: migration.catalog(altered)), "every corrupt CLYR byte rejected")
for size in (0, 4, 20, 1000, len(asset) - 1):
    check(rejects(lambda: migration.catalog(asset[:size])), "truncated CLYR rejected")
for text in (b"m_layer: Default\n", b"  m_layer: 'Default'\r\n", b'  m_layer: "Default"\n', b'\xef\xbb\xbfm_layer: Default\r\n'):
    converted, count = migration.convert(text, names)
    check(count == 1 and b"m_layerId:" in converted, "legacy scalar converted")
    check(migration.convert(converted, names) == (converted, 0), "second migration is byte-idempotent")
for text in (b"m_layer: Unknown\n", b"m_layer: |\n", b"m_layerId: 0\n", b"m_layerId: 999\n", b"m_layerId: 1\nm_layer: Default\n"):
    check(rejects(lambda: migration.convert(text, names)), "ambiguous or unknown schema rejected")
check(migration.convert(b"name: Foo\n", names) == (b"name: Foo\n", 0), "non-entity data unchanged")
check(migration.convert(b"m_layer: Renamed\n", {"Renamed": 91})[0] == b"m_layerId: 91\n", "migration uses stable ID instead of slot")

check(migration.convert(b"m_collisionType: 0\nm_layerId: 1\n", names) == (b"m_layerId: 1\n", 1), "obsolete collision field removed")
check(migration.convert(b"m_collisionType: 0\nm_layer: Default\n", names) == (b"m_layerId: 1\n", 1), "one-step schema migration")
check(rejects(lambda: migration.convert(b"m_collisionType: broken\nm_layer: Default\n", names)), "corrupt obsolete scalar rejected")
with tempfile.TemporaryDirectory() as root:
    root = Path(root)
    project = root / "Project"
    (project / "ProjectSetting").mkdir(parents=True)
    (project / "Assets").mkdir()
    (project / "ProjectSetting/Layers.celayers").write_bytes(asset)
    a, b = project / "Assets/A.creator", project / "Assets/B.prefab"
    before = b"m_layer: Default\r\nname: Keep\r\n"
    a.write_bytes(before)
    b.write_bytes(b"m_layer: Unknown\n")
    check(rejects(lambda: migration.migrate(project, root / "Backup", True)), "whole project preflight failure")
    check(a.read_bytes() == before and not (root / "Backup").exists(), "preflight failure writes nothing")
    b.write_bytes(before)
    check(migration.migrate(project, root / "Backup")["entities"] == 2 and a.read_bytes() == before, "dry run preserves source")
    original = migration.atomic_write
    calls = 0
    def fail_second(path, data):
        global calls
        calls += 1
        if calls == 2:
            raise OSError("Injected publication failure")
        return original(path, data)
    migration.atomic_write = fail_second
    check(rejects(lambda: migration.migrate(project, root / "Backup", True)), "injected second file write failure")
    check(a.read_bytes() == before and b.read_bytes() == before, "partial publication rolls back exact original bytes")
    migration.atomic_write = original
    result = migration.migrate(project, root / "Backup", True)
    check(result["files"] == 2 and result["entities"] == 2, "full project migration")
    with zipfile.ZipFile(result["backup"]) as archive:
        check(archive.read("Assets/A.creator") == before and archive.read("Assets/B.prefab") == before, "recoverable exact backup")
        check(len(json.loads(archive.read("manifest.json"))) == 2, "hash manifest covers every changed source")
    check(migration.migrate(project, root / "Backup", True)["files"] == 0, "project rerun is idempotent")
print(f"ENTITY_LAYER_MIGRATION_OK checks={checks}")
