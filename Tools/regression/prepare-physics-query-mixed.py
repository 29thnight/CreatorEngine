"""Add isolated collision and rotating kinematic actors to the dense query fixture."""
import copy
from pathlib import Path
import yaml

root = Path(__file__).resolve().parent
source = root / "fixtures/PhysicsQueryDynamic.creator"
output = root / "fixtures/PhysicsQueryMixed.creator"
doc = yaml.safe_load(source.read_bytes())
entities = doc["m_Entities"]
template = next(e for e in entities if e["m_name"] == "DenseQuery000")
for ordinal, (name, x, motion, extent) in enumerate((
        ("MixedDynamic", 100, 2, [.5, .5, .5]),
        ("MixedWall", 105, 0, [.5, 3, 3]),
        ("MixedKinematic", 110, 1, [2, .25, .25]))):
    entity = copy.deepcopy(template)
    index = max(e["m_index"] for e in entities) + 1
    entity.update(m_name=name, m_index=index, m_instanceID=3900000000 + ordinal * 10,
                  m_parentIndex=0, m_rootIndex=0)
    for offset, component in enumerate(entity["m_components"]):
        component["m_instanceID"] = 3900000001 + ordinal * 10 + offset
        if "Transform" in component:
            component["position"] = dict(x=x, y=20, z=0, w=1)
            component["m_parentID"] = 0
        if "PhysicsBodyComponent" in component:
            component["m_motion"] = motion
            component["m_gravityEnabled"] = False
            component["m_shapes"][0]["kind"] = 0
            component["m_shapes"][0]["halfExtent"] = dict(zip(("x", "y", "z"), extent))
    entities.append(entity)
    entities[0]["m_childrenIndices"].append(index)
output.write_text(yaml.safe_dump(doc, sort_keys=False), encoding="utf-8")
output.with_suffix(output.suffix + ".meta").write_bytes(source.with_suffix(source.suffix + ".meta").read_bytes())
print(output)
