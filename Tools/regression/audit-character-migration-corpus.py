"""Inventory current character authoring; recognize P0 only by component fingerprints."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

import yaml

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--project", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
project = args.project.resolve()
if args.output.resolve().is_relative_to(project / "Assets"):
    parser.error("Audit output must stay outside authoring Assets")
spec = importlib.util.spec_from_file_location("migration", Path(__file__).with_name("migrate-physics-schema.py"))
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)


def characters(node):
    if isinstance(node, dict):
        components = node.get("m_components") or []
        legacy = [item for item in components if "CharacterControllerComponent" in item]
        if legacy:
            carriers = [item for item in components if "RigidBodyComponent" in item]
            yield dict(entity=node.get("m_name"), componentIds=[item["m_instanceID"] for item in legacy],
                       fingerprint=migration.character_fingerprint(legacy + carriers))
        for value in node.values():
            yield from characters(value)
    elif isinstance(node, list):
        for value in node:
            yield from characters(value)


fixture = Path(__file__).parent / "fixtures/physics-p0/PhysicsP0Baseline.creator"
known = {item["fingerprint"] for item in characters(yaml.safe_load(fixture.read_bytes()))}
entries = []
legacy = []
for path in sorted((project / "Assets").rglob("*")):
    if not path.is_file() or path.suffix.lower() not in {".creator", ".prefab"}:
        continue
    if path.is_symlink() or not path.resolve().is_relative_to(project):
        raise ValueError("Asset escapes project")
    raw = path.read_bytes()
    found = list(characters(yaml.safe_load(raw)))
    for item in found:
        item.update(path=path.relative_to(project).as_posix(), sha256=hashlib.sha256(raw).hexdigest(),
                    classification="p0_fixture_components" if item["fingerprint"] in known else "requires_content_review")
        legacy.append(item)
    entries.append(dict(path=path.relative_to(project).as_posix(), sha256=hashlib.sha256(raw).hexdigest()))

for item in entries:
    if hashlib.sha256((project / item["path"]).read_bytes()).hexdigest() != item["sha256"]:
        raise ValueError("Corpus changed while scanning")

review = sum(item["classification"] == "requires_content_review" for item in legacy)
result = dict(result="CHARACTER_CORPUS_AUDIT_OK", project=str(project), inspectedFiles=len(entries),
              legacyCharacters=len(legacy), p0FixtureCharacters=len(legacy) - review,
              contentCharactersRequiringReview=review, sourceImmutable=True, legacy=legacy, inspected=entries,
              scope="Current project Scene/Prefab files only; no external project or movement golden acceptance")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(json.dumps({key: value for key, value in result.items() if key not in {"legacy", "inspected"}}, ensure_ascii=False))
