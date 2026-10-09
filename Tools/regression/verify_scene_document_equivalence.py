"""Compare saved authoring values while resolving serialized entity indices."""
import json
import sys
from pathlib import Path

import yaml


def canonical(path):
    document = yaml.safe_load(Path(path).read_text(encoding="utf-8-sig"))
    entities = document["m_Entities"]
    identities = {entity["m_index"]: entity["m_instanceID"] for entity in entities}

    for entity in entities:
        del entity["m_index"]
        for field in ("m_parentIndex", "m_rootIndex"):
            index = entity[field]
            entity[field] = identities[index] if index >= 0 else None
        entity["m_childrenIndices"] = [identities[index] for index in entity["m_childrenIndices"]]

    document["m_Entities"] = sorted(entities, key=lambda entity: entity["m_instanceID"])
    return document


before, after = (canonical(path) for path in sys.argv[1:])
if before != after:
    print(json.dumps({"result": "SCENE_DOCUMENT_EQUIVALENCE_FAILED"}))
    sys.exit(1)

print("SCENE_DOCUMENT_EQUIVALENCE_OK")
