"""Offline primitive physics migration. No runtime compatibility; dry-run is the default."""
import argparse
import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import uuid
import zipfile

import yaml
from yaml.events import AliasEvent

spec = importlib.util.spec_from_file_location("layer_migration", Path(__file__).with_name("migrate-entity-layer-ids.py"))
layer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layer)
character_spec = importlib.util.spec_from_file_location("character_units", Path(__file__).with_name("report-character-unit-migration.py"))
character_units = importlib.util.module_from_spec(character_spec)
character_spec.loader.exec_module(character_units)
LEGACY = json.loads((Path(__file__).parent / "fixtures/physics-p0/legacy-type-uuids.json").read_text())
BODY_UUID = "1bbfb80a-8bd9-440a-8557-537a0ff60cd8"
BODY_KEY = 6005066272611484821
CHARACTER_UUID = "aeb7397b-e6d4-46b8-b830-1b1e9a50a467"
CHARACTER_KEY = 17579205194823809952
META = {"m_typeUUID", "m_name", "m_instanceID", "m_isEnabled", "m_FileID"}
LIMITS = {"maxLinearVelocity": 1e16, "maxAngularVelocity": 100, "maxContactImpulse": 1e32, "maxDepenetrationVelocity": 1e32}


class StrictLoader(yaml.SafeLoader):
    def construct_mapping(self, node, deep=False):
        result = {}
        for key_node, value_node in node.value:
            key = self.construct_object(key_node, deep=deep)
            if not isinstance(key, str) or key in result:
                raise ValueError("Duplicate or non-string YAML key")
            result[key] = self.construct_object(value_node, deep=deep)
        return result


def number(value, minimum=0, positive=False):
    if isinstance(value, bool):
        raise ValueError("Boolean is not a physics number")
    result = float(value)
    if not math.isfinite(result) or result < minimum or positive and result <= minimum or abs(result) > 3.402823466e38:
        raise ValueError("Invalid finite physics value")
    return result


def flag(value):
    if type(value) is not bool:
        raise ValueError("Expected boolean")
    return value


def vector(value, size=3):
    if not isinstance(value, dict) or set(value) != set("xyzw"[:size]):
        raise ValueError("Invalid vector fields")
    return {key: number(value[key], -3.402823466e38) for key in "xyzw"[:size]}


def identity(component):
    value = component.get("m_instanceID")
    if type(value) is not int or not 0 < value < 2**32:
        raise ValueError("Component instance ID must fit a nonzero ShapeId")
    return value


def character_fingerprint(components):
    payload = sorted(components, key=lambda item: item["m_instanceID"])
    return hashlib.sha256(json.dumps(payload, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def validate_character_policy(policy):
    if policy is None:
        return
    if not isinstance(policy, dict) or set(policy) != {"schema", "baselineSeconds", "characters"} or type(policy["schema"]) is not int or policy["schema"] != 1 or not isinstance(policy["characters"], dict):
        raise ValueError("Invalid character policy schema")
    character_units.number(policy["baselineSeconds"], "baselineSeconds", minimum=1e-6, maximum=1)
    for key, value in policy["characters"].items():
        if not isinstance(key, str) or not key.isdecimal() or str(int(key)) != key or not isinstance(value, dict):
            raise ValueError("Invalid character policy identity")


def migrate_overrides(entity, legacy, mappings):
    overrides = entity.get("m_prefabOverrides")
    if not overrides:
        return
    if not isinstance(overrides, list):
        raise ValueError("Invalid prefab override list")
    rewritten, audit = [], []
    character = "characterId" in mappings[0]
    fields = {"RigidBodyComponent": {"m_mass": "m_mass", "LinearDamping": "m_linearDamping", "m_useGravity": "m_gravityEnabled"},
              "CharacterControllerComponent": {"m_radius": "m_radius", "m_height": "m_cylinderHeight"}}
    for override in overrides:
        if not isinstance(override, dict) or set(override) - {"m_componentType", "m_componentSlot", "m_propertyName", "m_valueYaml"}:
            raise ValueError("Unsupported prefab override schema")
        kind = override.get("m_componentType", "")
        property_name = override.get("m_propertyName")
        if not isinstance(kind, str) or not isinstance(property_name, str) or not isinstance(override.get("m_valueYaml"), str):
            raise ValueError("Invalid prefab override fields")
        if kind not in LEGACY:
            if not kind and property_name in ("m_layer", "m_collisionType"):
                raise ValueError("Legacy layer override requires stable-ID remapping")
            rewritten.append(override)
            continue
        target = "CharacterMovementComponent" if character else "PhysicsBodyComponent"
        required_kind = "CharacterControllerComponent" if character else "RigidBodyComponent"
        slot = override.get("m_componentSlot", -1)
        if kind != required_kind or property_name not in fields[kind] or type(slot) is not int or slot not in (-1, 0):
            raise ValueError("Physics override requires unsupported typed field/slot remapping")
        source = next(item for item in legacy if item["m_typeUUID"] == LEGACY[kind])
        payload = override["m_valueYaml"]
        if any(isinstance(event, AliasEvent) or getattr(event, "anchor", None) for event in yaml.parse(payload)):
            raise ValueError("Override aliases are unsupported")
        value = yaml.load(payload, Loader=StrictLoader)
        if property_name == "m_useGravity":
            same = flag(value) == flag(source[property_name])
        else:
            same = number(value) == number(source[property_name])
        if not same:
            raise ValueError("Override value differs from reviewed effective component value")
        replacement = dict(override, m_componentType=target, m_componentSlot=0, m_propertyName=fields[kind][property_name])
        rewritten.append(replacement)
        audit.append(dict(sourceType=kind, sourceProperty=property_name, targetType=target, targetProperty=replacement["m_propertyName"], targetSlot=0))
    entity["m_prefabOverrides"] = rewritten
    mappings[0]["overrideMappings"] = audit


def migrate_character(entity, legacy, policy):
    characters = [item for item in legacy if item["m_typeUUID"] == LEGACY["CharacterControllerComponent"]]
    companions = [item for item in legacy if item["m_typeUUID"] == LEGACY["RigidBodyComponent"]]
    if len(characters) != 1 or len(companions) != 1 or len(legacy) != 2:
        raise ValueError("CCT migration requires one controller and one collider-free velocity carrier")
    if any(item.get("m_typeUUID") in (BODY_UUID, CHARACTER_UUID) for item in entity["m_components"]):
        raise ValueError("Mixed old/new character ownership")
    character, companion = characters[0], companions[0]
    ident = identity(character)
    reviewed = policy.get("characters", {}).get(str(ident)) if policy else None
    if not reviewed:
        raise ValueError("CCT requires a source-hashed --character-policy; automatic conversion remains blocked")
    required = {"sourceSha256", "speedSource", "inputPolicy", "companionBody", "braking", "dynamicDamping", "automaticRotation", "maxFallSpeed"}
    if set(reviewed) != required or reviewed["sourceSha256"] != character_fingerprint(legacy):
        raise ValueError("CCT policy fields/hash do not match reviewed source")
    choices = {"inputPolicy": "external_desired_velocity", "companionBody": "remove_velocity_carrier",
               "braking": "static_decay", "dynamicDamping": "discard", "automaticRotation": "external"}
    if any(reviewed[key] != value for key, value in choices.items()) or reviewed["speedSource"] not in ("steady", "serialized"):
        raise ValueError("Unsupported CCT policy selection")
    allowed = META | {"CharacterControllerComponent", "m_posOffset", "m_rotOffset", "m_radius", "m_height", "maxSpeed", "acceleration", "staticFriction", "dynamicFriction", "jumpSpeed", "gravityWeight", "m_fBaseSpeed", "m_fFinalMultiplierSpeed", "m_rotationSpeed"}
    if set(character) - allowed or uuid.UUID(character["m_FileID"]).int:
        raise ValueError("CCT extra fields/asset references require remapping")
    if vector(character["m_posOffset"]) != dict(x=0,y=0,z=0) or vector(character["m_rotOffset"],4) != dict(x=0,y=0,z=0,w=1):
        raise ValueError("CCT offset composition requires explicit migration")
    allowed_body = META | {"RigidBodyComponent", "m_bodyType", "LinearDamping", "AngularDamping", "m_mass", "m_useGravity", "m_setTrigger", "m_setKinematic", "m_collisionEnabled"} | LIMITS.keys()
    if set(companion) - allowed_body or type(companion["m_bodyType"]) is not int or companion["m_bodyType"] != 1 or not flag(companion["m_isEnabled"]) or flag(companion["m_setTrigger"]) or flag(companion["m_setKinematic"]) or not flag(companion["m_collisionEnabled"]) or uuid.UUID(companion["m_FileID"]).int:
        raise ValueError("Companion Rigidbody has independent/unsupported ownership")
    proposal = character_units.convert(character, policy["baselineSeconds"])
    fields = proposal["authoringProposal"]
    if fields["m_brakingDecay"] is None:
        raise ValueError("Instant legacy braking has no finite decay equivalent")
    fields["m_maxFallSpeed"] = number(reviewed["maxFallSpeed"], positive=True)
    number(fields["m_jumpSpeed"], positive=True)
    number(character["m_rotationSpeed"])
    output = {"CharacterMovementComponent": CHARACTER_KEY, "m_typeUUID": CHARACTER_UUID, "m_name": "CharacterMovementComponent",
              "m_instanceID": ident, "m_isEnabled": flag(character["m_isEnabled"]), "m_FileID": character["m_FileID"], **fields}
    # Current SDK-free capsule contract requires step offset within radius + half cylinder height.
    if fields["m_stepOffset"] > fields["m_radius"] + fields["m_cylinderHeight"]*.5:
        raise ValueError("Converted controller step offset exceeds capsule dimensions")
    replacement = []
    inserted = False
    for item in entity["m_components"]:
        if item in legacy:
            if not inserted:
                replacement.append(output)
                inserted = True
        else:
            replacement.append(item)
    entity["m_components"] = replacement
    speed_field = "steadyMaxSpeedMetresPerSecond" if reviewed["speedSource"] == "steady" else "serializedMaxSpeedMetresPerSecond"
    return [dict(entityId=entity.get("m_instanceID"), characterId=ident, shapes=[], retiredComponentIds=[identity(companion)],
                 sourceSha256=reviewed["sourceSha256"], characterPolicy=reviewed, baselineSeconds=policy["baselineSeconds"],
                 externalInputSpeedMetresPerSecond=proposal["convertedMovement"][speed_field],
                 externalInputFollowUpRequired=True, discardedCompanion=companion,
                 discardedDynamicDamping=character["dynamicFriction"], discardedAutomaticRotation=character["m_rotationSpeed"])]


def migrate_entity(entity, reset_limits, character_policy=None, geometry_bindings=None):
    components = entity["m_components"]
    if not isinstance(components, list):
        raise ValueError("Invalid component list")
    legacy = [item for item in components if isinstance(item, dict) and item.get("m_typeUUID") in LEGACY.values()]
    if not legacy:
        return []
    for item in legacy:
        kind = next(name for name, ident in LEGACY.items() if ident == item["m_typeUUID"])
        if kind not in {"RigidBodyComponent", "BoxColliderComponent", "SphereColliderComponent", "CapsuleColliderComponent", "CharacterControllerComponent", "MeshColliderComponent", "TerrainColliderComponent"}:
            raise ValueError(f"Unsupported legacy component: {kind}; geometry/C1 migration required")
        if item.get("m_name") != kind or kind not in item:
            raise ValueError("Legacy type UUID/name/key mismatch")
    if any(item["m_typeUUID"] == LEGACY["CharacterControllerComponent"] for item in legacy):
        mappings = migrate_character(entity, legacy, character_policy)
        migrate_overrides(entity, legacy, mappings)
        return mappings
    bodies = [item for item in legacy if item["m_typeUUID"] == LEGACY["RigidBodyComponent"]]
    colliders = [item for item in legacy if item not in bodies]
    if len(bodies) > 1 or not colliders or any(item.get("m_typeUUID") == BODY_UUID for item in components):
        raise ValueError("Ambiguous body ownership or missing shape")
    rigid = bodies[0] if bodies else None
    owner = rigid or colliders[0]
    ident = identity(owner)
    sensor = False
    motion = 0
    limits = {}
    if rigid:
        allowed = META | {"RigidBodyComponent", "m_bodyType", "LinearDamping", "AngularDamping", "m_mass", "m_useGravity", "m_setTrigger", "m_setKinematic", "m_collisionEnabled"} | LIMITS.keys()
        if set(rigid) - allowed:
            raise ValueError(f"Unsupported body fields: {sorted(set(rigid)-allowed)}")
        old_motion = rigid["m_bodyType"]
        if type(old_motion) is not int or old_motion not in (0, 1, 2):
            raise ValueError("Unsupported disabled/invalid body kind")
        motion = {0: 0, 1: 2, 2: 1}[old_motion]
        if flag(rigid["m_setKinematic"]):
            if old_motion == 0:
                raise ValueError("Conflicting static/kinematic definition")
            motion = 1
        if not flag(rigid["m_collisionEnabled"]):
            raise ValueError("Collision-disabled solid has no equivalent authored field")
        sensor = flag(rigid["m_setTrigger"])
        for field in LIMITS:
            if field in rigid:
                limits[field] = number(rigid[field], positive=True)
        if limits and not reset_limits:
            raise ValueError("Legacy solver limits need explicit --reset-legacy-limits acceptance")
        if limits and any(value != LIMITS[key] for key, value in limits.items()):
            raise ValueError("Custom legacy solver limits cannot be reset by the default-limits policy")
    if sensor and motion != 0:
        raise ValueError("Moving sensor-only body requires a solid mass shape")
    shapes = []
    mappings = []
    for collider in colliders:
        kind = next(name for name, ident in LEGACY.items() if ident == collider["m_typeUUID"])
        if kind in {"MeshColliderComponent", "TerrainColliderComponent"}:
            binding = (geometry_bindings or {}).get(identity(collider))
            if not binding or binding["componentSha256"] != hashlib.sha256(json.dumps(collider,sort_keys=True,separators=(",", ":"),allow_nan=False).encode()).hexdigest():
                raise ValueError("Geometry requires matching reviewed binding and native receipt")
            allowed = META | {kind, "m_posOffset"} | ({"m_rotOffset"} if kind == "MeshColliderComponent" else set())
            if set(collider) - allowed or not flag(collider["m_isEnabled"]) or uuid.UUID(collider["m_FileID"]).int:
                raise ValueError("Unsupported geometry component fields, disabled shape or asset link")
            if kind == "TerrainColliderComponent" and (rigid or sensor):
                raise ValueError("Legacy Terrain conversion requires standalone static solid")
            policy = binding["shapePolicy"]
            if vector(collider["m_posOffset"]) != policy["localPosition"] or (kind == "MeshColliderComponent" and vector(collider["m_rotOffset"],4) != policy["localRotation"]):
                raise ValueError("Reviewed geometry pose differs from source")
            if kind == "TerrainColliderComponent" and policy["localRotation"] != dict(x=0,y=0,z=0,w=1):
                raise ValueError("Legacy Terrain has no authored rotation; identity required")
            shape_id = identity(collider)
            if any(item["shapeId"] == shape_id for item in shapes):
                raise ValueError("Duplicate geometry shape identity")
            shape = dict(shapeId=shape_id, kind=3 if kind == "MeshColliderComponent" else 5,
                         halfExtent=dict(x=.5,y=.5,z=.5),radius=.5,halfHeight=.5,
                         geometryAsset=binding["geometryUUID"],geometryRevision=binding["geometryRevision"],
                         geometryScale=policy["geometryScale"],localPosition=policy["localPosition"],localRotation=policy["localRotation"],
                         staticFriction=policy["staticFriction"],dynamicFriction=policy["dynamicFriction"],restitution=policy["restitution"],
                         sensor=sensor,queryEnabled=True,layerOverride=0)
            shapes.append(shape)
            mappings.append(dict(sourceTypeUUID=collider["m_typeUUID"],sourceComponentId=shape_id,targetBodyId=ident,shapeId=shape_id,
                                 geometryBinding=binding,materialPolicy="explicit reviewed values; legacy runtime-only material unavailable"))
            continue
        dimensions = {"m_boxExtent"} if kind.startswith("Box") else {"m_radius", "m_height"} if kind.startswith("Capsule") else {"m_radius"}
        allowed = META | {kind, "m_posOffset", "m_rotOffset", "staticFriction", "dynamicFriction", "restitution", "density"} | dimensions
        if set(collider) - allowed or not flag(collider["m_isEnabled"]):
            raise ValueError("Unsupported collider fields or independent disabled shape")
        if uuid.UUID(collider["m_FileID"]).int:
            raise ValueError("Collider asset identity/reference remapping is required")
        shape_id = identity(collider)
        if any(item["shapeId"] == shape_id for item in shapes):
            raise ValueError("Duplicate shape identity")
        rotation = vector(collider["m_rotOffset"], 4)
        if abs(sum(value * value for value in rotation.values()) - 1) > 0.001:
            raise ValueError("Non-unit local rotation")
        position = vector(collider["m_posOffset"])
        capsule = kind.startswith("Capsule")
        if capsule:
            if any(position.values()) or rotation != dict(x=0, y=0, z=0, w=1):
                raise ValueError("Capsule legacy offset composition requires explicit migration")
            # The old static path was X-axis; the dynamic/kinematic path already used Y-axis.
            if motion == 0:
                rotation = dict(x=0, y=0, z=-math.sqrt(.5), w=math.sqrt(.5))
        extent = vector(collider["m_boxExtent"]) if kind.startswith("Box") else dict(x=.5, y=.5, z=.5)
        if any(value <= 0 for value in extent.values()):
            raise ValueError("Non-positive box extent")
        density = number(collider["density"], positive=True)
        shape = dict(shapeId=shape_id, kind=0 if kind.startswith("Box") else 2 if capsule else 1, halfExtent=extent,
                     radius=number(collider["m_radius"], positive=True) if not kind.startswith("Box") else .5,
                     halfHeight=number(collider["m_height"]) / (1 if motion == 0 else 2) if capsule else .5,
                     geometryAsset="", geometryRevision=0, geometryScale=dict(x=1, y=1, z=1),
                     localPosition=position, localRotation=rotation,
                     staticFriction=number(collider["staticFriction"]), dynamicFriction=number(collider["dynamicFriction"]),
                     restitution=number(collider["restitution"]), sensor=sensor, queryEnabled=True, layerOverride=0)
        if shape["restitution"] > 1:
            raise ValueError("Restitution above one")
        shapes.append(shape)
        mapping = dict(sourceTypeUUID=collider["m_typeUUID"], sourceComponentId=shape_id, targetBodyId=ident, shapeId=shape_id, legacyDensity=density)
        if capsule:
            mapping["capsulePolicy"] = "static X-axis/full stored half-height" if motion == 0 else "dynamic/kinematic Y-axis/half stored height"
        mappings.append(mapping)
    body = {"PhysicsBodyComponent": BODY_KEY, "m_typeUUID": BODY_UUID, "m_name": "PhysicsBodyComponent",
            "m_instanceID": ident, "m_isEnabled": flag(owner["m_isEnabled"]), "m_FileID": owner["m_FileID"],
            "m_motion": motion, "m_shapeSchema": 1, "m_shapes": shapes,
            "m_mass": number(rigid["m_mass"], positive=True) if rigid else 1,
            "m_gravityEnabled": flag(rigid["m_useGravity"]) if rigid else False,
            "m_translationLocks": 0, "m_rotationLocks": 0,
            "m_linearDamping": number(rigid["LinearDamping"]) if rigid else 0,
            "m_angularDamping": number(rigid.get("AngularDamping", .05)) if rigid else .05,
            "m_initialLinearVelocity": dict(x=0, y=0, z=0), "m_initialAngularVelocity": dict(x=0, y=0, z=0)}
    replacement = []
    inserted = False
    for item in components:
        if item in legacy:
            if not inserted:
                replacement.append(body)
                inserted = True
        else:
            replacement.append(item)
    entity["m_components"] = replacement
    result = [dict(entityId=entity.get("m_instanceID"), bodyId=ident, shapes=mappings, resetSolverLimits=limits,
                   massPolicy="explicit body mass; compound inertia recomputed by new API", runtimeOnlyDefaults="locks/initial velocities absent from legacy serialization")]
    migrate_overrides(entity, legacy, result)
    return result


def convert(data, names, reset_limits=False, character_policy=None, geometry_bindings=None):
    validate_character_policy(character_policy)
    if len(data) > 32 * 1024 * 1024:
        raise ValueError("Authoring document too large")
    text = data.decode("utf-8-sig")
    if not any(ident in text.lower() for ident in LEGACY.values()):
        return data, []
    if any(isinstance(event, AliasEvent) or getattr(event, "anchor", None) for event in yaml.parse(text)):
        raise ValueError("YAML aliases/anchors are unsupported for ownership migration")
    normalized, _ = layer.convert(data, names)
    document = yaml.load(normalized.decode("utf-8-sig"), Loader=StrictLoader)
    original_document = copy.deepcopy(document)
    if geometry_bindings or LEGACY["CapsuleColliderComponent"] in text.lower() or LEGACY["CharacterControllerComponent"] in text.lower():
        # Old dynamic capsules pre-scaled dimensions, while the new API applies owner scale once.
        # Unit scales throughout the asset avoid guessing ancestor scale/offset composition.
        def validate_scales(node):
            if isinstance(node, dict):
                if "Transform" in node:
                    scale = node.get("scale")
                    if not isinstance(scale, dict) or any(number(scale[key]) != 1 for key in "xyz"):
                        raise ValueError("Capsule/CCT non-unit owner/ancestor scale requires explicit migration")
                for value in node.values():
                    validate_scales(value)
            elif isinstance(node, list):
                for value in node:
                    validate_scales(value)
        validate_scales(document)
    mappings = []
    def visit(node):
        if isinstance(node, dict):
            if "m_components" in node:
                mappings.extend(migrate_entity(node, reset_limits, character_policy, geometry_bindings))
            for value in node.values():
                visit(value)
        elif isinstance(node, list):
            for value in node:
                visit(value)
    visit(document)
    if not mappings:
        raise ValueError("Legacy UUID outside supported entity ownership")
    removed_ids = {shape["sourceComponentId"] for body in mappings for shape in body["shapes"] if shape["sourceComponentId"] != body["bodyId"]}
    removed_ids.update(ident for body in mappings for ident in body.get("retiredComponentIds", []))

    def check_references(node):
        if isinstance(node, dict):
            if node.get("m_typeUUID") in LEGACY.values():
                return
            for key, value in node.items():
                if key == "m_componentType" and value in LEGACY:
                    # migrate_overrides has already validated and remapped this typed field.
                    continue
                check_references(value)
        elif isinstance(node, list):
            for value in node:
                check_references(value)
        elif type(node) is int and node in removed_ids or isinstance(node, str) and any(node in (str(ident), "#" + str(ident)) for ident in removed_ids):
            raise ValueError("Retired component reference needs explicit typed remapping")
        elif isinstance(node, str) and node in LEGACY:
            raise ValueError("Legacy component type reference needs explicit typed remapping")

    check_references(original_document)
    output = yaml.safe_dump(document, allow_unicode=True, sort_keys=False).encode("utf-8")
    if any(ident.encode() in output.lower() for ident in LEGACY.values()):
        raise ValueError("Unmapped legacy reference remains")
    return output, mappings


def geometry_recovery_requirements(data):
    """Report missing authored inputs; never infer cooked assets from runtime pointers."""
    text = data.decode("utf-8-sig")
    kinds = ("MeshColliderComponent", "TerrainColliderComponent", "RagdollComponent")
    if not any(LEGACY[kind] in text.lower() for kind in kinds):
        return []
    if len(data) > 32 * 1024 * 1024 or any(isinstance(event, AliasEvent) or getattr(event, "anchor", None) for event in yaml.parse(text)):
        raise ValueError("Geometry recovery requires bounded alias-free authoring YAML")
    document = yaml.load(text, Loader=StrictLoader)
    result = []

    def visit(node):
        if isinstance(node, dict):
            components = node.get("m_components", [])
            if not isinstance(components, list):
                raise ValueError("Invalid geometry recovery component list")
            for component in components:
                if not isinstance(component, dict):
                    continue
                kind = next((kind for kind in kinds if component.get("m_typeUUID") == LEGACY[kind]), None)
                if not kind:
                    continue
                if component.get("m_name") != kind or kind not in component:
                    raise ValueError("Geometry recovery type UUID/name/key mismatch")
                supplier = "MeshRenderer" if kind == "MeshColliderComponent" else "TerrainComponent" if kind == "TerrainColliderComponent" else None
                owners = [copy.deepcopy(item) for item in components if isinstance(item, dict) and item.get("m_name") == supplier] if supplier else []
                required = {
                    "MeshColliderComponent": ["reviewed model/submesh vertex source and hash", "explicit material and convex cook policy", "local pose and scale acceptance", "cooked geometry UUID/revision and native validation"],
                    "TerrainColliderComponent": ["Terrain height source and hash", "rows/columns and sample order", "height quantization and axis scales", "cooked heightfield UUID/revision and native validation"],
                    "RagdollComponent": ["skeleton and joint/body ownership", "per-body shape and mass definitions", "joint limits and collision policy"],
                }[kind]
                result.append(dict(entityId=node.get("m_instanceID"), componentId=identity(component), legacyType=kind,
                                   sourceSha256=hashlib.sha256(json.dumps(component,sort_keys=True,separators=(",", ":"),allow_nan=False).encode()).hexdigest(),
                                   sourceComponent=copy.deepcopy(component), supplierType=supplier, supplierCandidates=owners,
                                   status="source_recovery_required", missingInputs=required,
                                   legacySourceRevision="12f970c7ed3a4408d268479f5d5acb5f80372b8c"))
            for value in node.values():
                visit(value)
        elif isinstance(node, list):
            for value in node:
                visit(value)

    visit(document)
    return result


def migrate(project, backup_dir, apply=False, reset_limits=False, baseline_seconds=None, character_policy=None, geometry_policy=None, native_receipts=None):
    validate_character_policy(character_policy)
    project = Path(project).resolve()
    if not (project / "Assets").is_dir():
        raise ValueError("Project Assets directory is missing")
    geometry_by_path = {}
    if geometry_policy is not None:
        policy_spec = importlib.util.spec_from_file_location("geometry_policy", Path(__file__).with_name("validate-physics-geometry-migration-policy.py"))
        policy_module = importlib.util.module_from_spec(policy_spec)
        policy_spec.loader.exec_module(policy_module)
        if not native_receipts:
            raise ValueError("Geometry conversion requires native receipts")
        policy_module.validate(project, geometry_policy, native_receipts, require_shape_policy=True)
        for binding in geometry_policy["bindings"]:
            geometry_by_path.setdefault((project / binding["authoring"]["path"]).resolve(), {})[binding["componentId"]] = binding
    layer_path = project / "ProjectSetting/Layers.celayers"
    layer_bytes = layer_path.read_bytes()
    names = layer.catalog(layer_bytes)
    if baseline_seconds is not None:
        character_units.number(baseline_seconds, "baseline_seconds", minimum=1e-6, maximum=1)
    prepared, diagnostics, character_proposals, inspected = [], [], [], []
    geometry_requirements = []
    for path in sorted((project / "Assets").rglob("*")):
        if not path.is_file() or path.suffix.lower() not in {".creator", ".prefab"}:
            continue
        if path.is_symlink() or not path.resolve().is_relative_to(project):
            diagnostics.append(dict(path=path.relative_to(project).as_posix(), reason="Authoring path escapes project or is a symlink"))
            continue
        before = path.read_bytes()
        inspected.append((path, before))
        try:
            after, mappings = convert(before, names, reset_limits, character_policy, geometry_by_path.get(path.resolve()))
            if mappings:
                prepared.append((path, before, after, mappings))
        except Exception as error:
            diagnostics.append(dict(path=path.relative_to(project).as_posix(), reason=str(error)))
            try:
                requirements = geometry_recovery_requirements(before)
                if requirements:
                    geometry_requirements.append(dict(path=path.relative_to(project).as_posix(), sha256=hashlib.sha256(before).hexdigest(), components=requirements))
            except Exception as recovery_error:
                diagnostics.append(dict(path=path.relative_to(project).as_posix(), reason="Geometry recovery: " + str(recovery_error)))
            if baseline_seconds is not None and LEGACY["CharacterControllerComponent"].encode() in before.lower():
                try:
                    text = before.decode("utf-8-sig")
                    if any(isinstance(event, AliasEvent) or getattr(event, "anchor", None) for event in yaml.parse(text)):
                        raise ValueError("Character report requires unaliased input")
                    proposals = character_units.collect(yaml.load(text, Loader=StrictLoader), baseline_seconds)
                    character_proposals.append(dict(path=path.relative_to(project).as_posix(), sha256=hashlib.sha256(before).hexdigest(), status="review_required", characters=proposals))
                except Exception as report_error:
                    diagnostics.append(dict(path=path.relative_to(project).as_posix(), reason="Character unit report: " + str(report_error)))
    retired = {ident for item in prepared for mapping in item[3] for ident in mapping.get("retiredComponentIds", [])}
    retired.update(shape["sourceComponentId"] for item in prepared for mapping in item[3] for shape in mapping["shapes"] if shape["sourceComponentId"] != mapping["bodyId"])
    converted = {item[0]: item[2] for item in prepared}
    legacy_tokens = tuple(LEGACY) + tuple(LEGACY.values())

    def audit_reference(node):
        if isinstance(node, dict):
            for key, value in node.items():
                if key in LEGACY or key.lower() in LEGACY.values():
                    raise ValueError("Cross-asset legacy type key requires explicit typed remapping")
                if key == "shapeId" and {"kind", "localPosition", "localRotation"}.issubset(node):
                    continue
                if key == "m_valueYaml" and isinstance(value, str) and any(str(ident) in value for ident in retired):
                    raise ValueError("Cross-asset override reference payload requires explicit remapping")
                audit_reference(value)
        elif isinstance(node, list):
            for value in node:
                audit_reference(value)
        elif type(node) is int and node in retired or isinstance(node, str) and any(node in (str(ident), "#" + str(ident)) for ident in retired):
            raise ValueError("Cross-asset retired component reference requires explicit typed remapping")
        elif isinstance(node, str) and any(token.lower() in node.lower() for token in legacy_tokens):
            raise ValueError("Cross-asset legacy type reference requires explicit typed remapping")

    failed_paths = {entry["path"] for entry in diagnostics}
    for path, before in inspected:
        if path.relative_to(project).as_posix() in failed_paths:
            continue
        candidate = converted.get(path, before)
        if not any(str(ident).encode() in candidate for ident in retired) and not any(token.lower().encode() in candidate.lower() for token in legacy_tokens):
            continue
        try:
            audit_reference(yaml.load(candidate.decode("utf-8-sig"), Loader=StrictLoader))
        except Exception as error:
            diagnostics.append(dict(path=path.relative_to(project).as_posix(), reason="Reference closure: " + str(error)))
    inventory = [dict(path=path.relative_to(project).as_posix(), sha256=hashlib.sha256(before).hexdigest(),
                      outcome="blocked" if path.relative_to(project).as_posix() in {entry["path"] for entry in diagnostics}
                      else "converted" if path in converted else "unchanged") for path, before in inspected]
    report = dict(files=len(prepared), bodies=sum("bodyId" in entry for item in prepared for entry in item[3]), characters=sum("characterId" in entry for item in prepared for entry in item[3]), applied=False, backup=None, diagnostics=diagnostics,
                  mappings={item[0].relative_to(project).as_posix(): item[3] for item in prepared}, recookRequired=bool(prepared), characterUnitProposals=character_proposals, geometryRecoveryRequirements=geometry_requirements, inspectedFiles=len(inspected), corpus=inventory)
    if diagnostics or not apply or not prepared:
        return report
    if geometry_policy is not None:
        policy_module.validate(project, geometry_policy, native_receipts, require_shape_policy=True)
    if layer_path.read_bytes() != layer_bytes:
        raise ValueError("Layer catalog changed after preflight")
    if any(path.read_bytes() != before for path, before in inspected):
        raise ValueError("Project reference corpus changed after preflight")
    backup_dir = Path(backup_dir).resolve()
    backup_dir.mkdir(parents=True, exist_ok=True)
    archive = backup_dir / ("physics-migration-" + uuid.uuid4().hex + ".zip")
    manifest = []
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED) as backup:
        backup.writestr("ProjectSetting/Layers.celayers", layer_bytes)
        for path, before, after, mappings in prepared:
            relative = path.relative_to(project).as_posix()
            backup.writestr(relative, before)
            manifest.append(dict(path=relative, before=hashlib.sha256(before).hexdigest(), after=hashlib.sha256(after).hexdigest(), mappings=mappings))
        backup.writestr("manifest.json", json.dumps(manifest, indent=2))
    written = []
    try:
        for path, before, after, _ in prepared:
            if path.read_bytes() != before:
                raise ValueError("Source changed after preflight")
            layer.atomic_write(path, after)
            written.append((path, before))
    except BaseException:
        for path, before in reversed(written):
            layer.atomic_write(path, before)
        raise
    report.update(applied=True, backup=str(archive))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True)
    parser.add_argument("--backup-dir", required=True)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--character-policy", type=Path, help="Explicit source-hashed CCT migration policy; external input wiring remains a product acceptance requirement")
    parser.add_argument("--baseline-seconds", type=float, help="Explicit measured legacy tick for CCT unit proposals only; never enables CCT publication")
    parser.add_argument("--reset-legacy-limits", action="store_true", help="Explicitly accept removal of exact legacy default limits; custom values are rejected")
    parser.add_argument("--geometry-policy", type=Path)
    parser.add_argument("--native-receipt", type=Path, action="append")
    options = parser.parse_args()
    def unique_policy_fields(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate character policy field")
            result[key] = value
        return result
    policy = json.loads(options.character_policy.read_text(encoding="utf-8"), object_pairs_hook=unique_policy_fields) if options.character_policy else None
    validate_character_policy(policy)
    report = migrate(options.project, options.backup_dir, options.apply, options.reset_legacy_limits, options.baseline_seconds, policy, json.loads(options.geometry_policy.read_text(encoding="utf-8"),object_pairs_hook=unique_policy_fields) if options.geometry_policy else None, [json.loads(path.read_text(encoding="utf-8-sig"),object_pairs_hook=unique_policy_fields) for path in options.native_receipt] if options.native_receipt else None)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    raise SystemExit(2 if report["diagnostics"] else 0)
