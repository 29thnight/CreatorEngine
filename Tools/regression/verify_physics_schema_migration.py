import copy
import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import zipfile
import yaml

spec = importlib.util.spec_from_file_location("migration", Path(__file__).with_name("migrate-physics-schema.py"))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
repo = Path(__file__).resolve().parents[2]
source = (repo / "Tools/regression/fixtures/physics-p0/PhysicsP0Drop.prefab").read_bytes()
names = m.layer.catalog((repo / "Dynamic_CPP/ProjectSetting/Layers.celayers").read_bytes())
checks = 0
parser = argparse.ArgumentParser()
parser.add_argument("--native-receipt", type=Path)
options = parser.parse_args()

def check(value, message):
    global checks
    checks += 1
    if not value:
        raise AssertionError(message)

def rejects(data, reset=True):
    try:
        m.convert(data, names, reset)
    except Exception:
        return True
    return False

def encode(document):
    return yaml.safe_dump(document, sort_keys=False).encode()

check(rejects(source, False), "lossy legacy limits require explicit policy")
after, mappings = m.convert(source, names, True)
original = yaml.safe_load(source)
converted = yaml.safe_load(after)
entity = converted["PrefabNode"][0]
body = next(item for item in entity["m_components"] if item.get("m_typeUUID") == m.BODY_UUID)
check(body["m_instanceID"] == 395603858, "body identity preserved")
check(body["m_shapes"][0]["shapeId"] == 2522806044, "collider identity becomes stable shape identity")
check(body["m_motion"] == 2 and body["m_mass"] == 70 and body["m_linearDamping"] == .01, "legacy dynamic enum/mass/damping")
check(body["m_shapes"][0]["halfExtent"] == dict(x=.5,y=.5,z=.5), "dimensions preserved")
check(entity["m_layerId"] == 1 and "m_collisionType" not in entity and "m_layer" not in entity, "common layer migration composed")
check(converted["m_fileGuid"] == original["m_fileGuid"] and entity["m_prefabFileGuid"] == original["PrefabNode"][0]["m_prefabFileGuid"], "file UUIDs preserved")
check(entity["m_components"][0] == original["PrefabNode"][0]["m_components"][0], "unrelated transform values preserved")
check(m.convert(after,names,True) == (after,[]), "byte-idempotent second pass")
check(mappings[0]["resetSolverLimits"]["maxAngularVelocity"] == 100, "lossy policy recorded")

for field, value in [("m_mass",0),("m_bodyType",3),("maxAngularVelocity",99),("m_collisionEnabled",False),("m_setTrigger",True),("Unknown",1)]:
    document=copy.deepcopy(original)
    document["PrefabNode"][0]["m_components"][2][field]=value
    check(rejects(encode(document)), f"unsupported body rejected {field}")
for field, value in [("m_boxExtent",dict(x=0,y=1,z=1)),("m_rotOffset",dict(x=0,y=0,z=0,w=2)),("restitution",2),("m_isEnabled",False)]:
    document=copy.deepcopy(original)
    document["PrefabNode"][0]["m_components"][1][field]=value
    check(rejects(encode(document)), f"invalid shape rejected {field}")
check(rejects(source+b"m_name: duplicate\n"), "duplicate keys rejected")
check(rejects(source+b"Alias: &a [1]\nOther: *a\n"), "aliases rejected")
check(rejects((repo/"Tools/regression/fixtures/physics-p0/PhysicsP0Baseline.creator").read_bytes()), "character migration blocks entire scene")
static = copy.deepcopy(original)
static["PrefabNode"][0]["m_components"].pop()
static_after,_=m.convert(encode(static),names)
static_body=yaml.safe_load(static_after)["PrefabNode"][0]["m_components"][1]
check(static_body["m_motion"]==0 and static_body["m_instanceID"]==2522806044, "standalone collider has explicit static-body policy")

compound=copy.deepcopy(original)
sphere=copy.deepcopy(compound["PrefabNode"][0]["m_components"][1])
sphere.pop("BoxColliderComponent")
sphere.pop("m_boxExtent")
sphere.update(SphereColliderComponent=123, m_typeUUID=m.LEGACY["SphereColliderComponent"], m_name="SphereColliderComponent", m_instanceID=200, m_radius=.75)
compound["PrefabNode"][0]["m_components"].insert(2,sphere)
compound_after,compound_map=m.convert(encode(compound),names,True)
compound_body=yaml.safe_load(compound_after)["PrefabNode"][0]["m_components"][1]
check(len(compound_body["m_shapes"])==2 and compound_body["m_shapes"][1]["kind"]==1 and compound_body["m_shapes"][1]["radius"]==.75, "compound box/sphere ownership")
check(compound_map[0]["shapes"][1]["shapeId"]==200, "compound stable shape IDs")
check(rejects(source.upper()), "uppercase legacy UUID cannot silently evade audit")
reference=copy.deepcopy(original)
reference["PrefabNode"][0]["m_components"][1]["m_FileID"]="11111111-1111-1111-1111-111111111111"
check(rejects(encode(reference)), "asset-linked collider cannot silently lose UUID")
reference=copy.deepcopy(original)
reference["ColliderReference"]=2522806044
check(rejects(encode(reference)), "external collider reference requires explicit remapping")

override_source=copy.deepcopy(original)
override_source["PrefabNode"][0]["m_prefabOverrides"]=[dict(m_componentType="RigidBodyComponent",m_componentSlot=0,m_propertyName="m_mass",m_valueYaml="70"),dict(m_componentType="Transform",m_componentSlot=0,m_propertyName="position",m_valueYaml="{x: 0, y: 5, z: 0, w: 0}")]
override_after,override_map=m.convert(encode(override_source),names,True)
new_overrides=yaml.safe_load(override_after)["PrefabNode"][0]["m_prefabOverrides"]
check(new_overrides[0]["m_componentType"]=="PhysicsBodyComponent" and new_overrides[0]["m_propertyName"]=="m_mass", "typed body override mapped")
check(new_overrides[1]==override_source["PrefabNode"][0]["m_prefabOverrides"][1], "unrelated override preserved")
check(override_map[0]["overrideMappings"][0]["targetSlot"]==0, "override mapping audited")
override_source["PrefabNode"][0]["m_prefabOverrides"][0]["m_valueYaml"]="71"
check(rejects(encode(override_source)), "override disagreement blocks publication")

capsules=[]
for dynamic, ident, expected_height in [(False,300,2),(True,400,1)]:
    capsule=copy.deepcopy(original)
    item=capsule["PrefabNode"][0]["m_components"][1]
    item.pop("BoxColliderComponent")
    item.pop("m_boxExtent")
    item.update(CapsuleColliderComponent=123,m_typeUUID=m.LEGACY["CapsuleColliderComponent"],m_name="CapsuleColliderComponent",m_instanceID=ident,m_radius=.55,m_height=2)
    if not dynamic:
        capsule["PrefabNode"][0]["m_components"].pop()
    capsule_after,capsule_map=m.convert(encode(capsule),names,True)
    shape=yaml.safe_load(capsule_after)["PrefabNode"][0]["m_components"][1]["m_shapes"][0]
    capsules.append(shape)
    check(shape["kind"]==2 and shape["halfHeight"]==expected_height and shape["radius"]==.55, "legacy capsule height semantics")
    rotation=shape["localRotation"]
    axis_x=-2*rotation["z"]*rotation["w"]
    axis_y=1-2*rotation["z"]**2
    check(abs(axis_x-(0 if dynamic else 1))<1e-6 and abs(axis_y-(1 if dynamic else 0))<1e-6, "legacy X/Y capsule axis preserved")
    check("capsulePolicy" in capsule_map[0]["shapes"][0], "capsule policy provenance")
    invalid=copy.deepcopy(capsule)
    invalid["PrefabNode"][0]["m_components"][0]["scale"]["y"]=2
    check(rejects(encode(invalid)), "pre-scaled capsule cannot be guessed")
    invalid=copy.deepcopy(capsule)
    invalid["PrefabNode"][0]["m_components"][1]["m_posOffset"]["x"]=1
    check(rejects(encode(invalid)), "legacy capsule offset composition blocked")

with tempfile.TemporaryDirectory() as folder:
    root=Path(folder)
    project=root/"Project"
    (project/"Assets").mkdir(parents=True)
    (project/"ProjectSetting").mkdir()
    (project/"ProjectSetting/Layers.celayers").write_bytes((repo/"Dynamic_CPP/ProjectSetting/Layers.celayers").read_bytes())
    a=project/"Assets/A.prefab"
    b=project/"Assets/B.prefab"
    a.write_bytes(source)
    b.write_bytes(source)
    (project/"Assets/A.prefab.meta").write_bytes(b"guid: preserve\r\n")
    dry=m.migrate(project,root/"Backup",reset_limits=True)
    check(dry["files"]==2 and not dry["applied"] and a.read_bytes()==source and not (root/"Backup").exists(), "dry run has no writes")
    b.write_bytes(source+b"m_name: duplicate\n")
    blocked=m.migrate(project,root/"Backup",True,True)
    check(len(blocked["diagnostics"])==1 and not blocked["applied"] and a.read_bytes()==source and not (root/"Backup").exists(), "whole-project preflight is atomic")
    b.write_bytes(source)
    external=project/"Assets/External.creator"
    external.write_bytes(b"ExternalColliderReference: '#2522806044'\n")
    blocked=m.migrate(project,root/"Backup",True,True)
    check(any("Reference closure" in entry["reason"] for entry in blocked["diagnostics"]) and not blocked["applied"] and a.read_bytes()==source, "cross-asset retired collider reference blocks publication")
    for payload in ["TypeReference: CharacterControllerComponent\n", "m_valueYaml: 'componentType: BoxColliderComponent'\n", "BoxColliderComponent: referenced\n", f"TypeReference: '{m.LEGACY['BoxColliderComponent'].upper()}'\n"]:
        external.write_text(payload,encoding="utf-8")
        blocked=m.migrate(project,root/"Backup",True,True)
        check(bool(blocked["diagnostics"]) and not blocked["applied"] and a.read_bytes()==source and not (root/"Backup").exists(), "cross-asset legacy type closure is atomic")
    external.write_bytes(b"TypeReference: CharacterMovementComponent\n")
    accepted=m.migrate(project,root/"Backup",False,True)
    check(not accepted["diagnostics"] and accepted["files"]==2, "current type references remain valid")
    external.unlink()
    atomic=m.layer.atomic_write
    calls=0
    def fail_second(path,data):
        global calls
        calls+=1
        if calls==2:
            raise OSError("Injected second write failure")
        atomic(path,data)
    m.layer.atomic_write=fail_second
    try:
        m.migrate(project,root/"Backup",True,True)
        raise AssertionError("Expected write failure")
    except OSError:
        pass
    finally:
        m.layer.atomic_write=atomic
    check(a.read_bytes()==source and b.read_bytes()==source, "partial publication rolled back exact bytes")
    result=m.migrate(project,root/"Backup",True,True)
    check(result["applied"] and result["bodies"]==2, "successful project publication")
    with zipfile.ZipFile(result["backup"]) as archive:
        check(archive.read("Assets/A.prefab")==source and archive.read("Assets/B.prefab")==source, "exact original backup")
        check(len(json.loads(archive.read("manifest.json")))==2, "hash/mapping manifest")
    check((project/"Assets/A.prefab.meta").read_bytes()==b"guid: preserve\r\n", "meta bytes preserved")
    check(m.migrate(project,root/"Backup",True,True)["files"]==0, "project rerun is byte-idempotent")
    legacy_scene=(repo/"Tools/regression/fixtures/physics-p0/PhysicsP0Baseline.creator").read_bytes()
    (project/"Assets/C.creator").write_bytes(legacy_scene)
    reviewed=m.migrate(project,root/"Backup",True,True,1/60)
    check(not reviewed["applied"] and reviewed["diagnostics"] and len(reviewed["characterUnitProposals"])==1, "unit report never enables CCT publication")
    proposal=reviewed["characterUnitProposals"][0]["characters"][0]
    check(abs(proposal["convertedMovement"]["serializedMaxSpeedMetresPerSecond"]-61.5)<1e-6 and abs(proposal["convertedMovement"]["steadyMaxSpeedMetresPerSecond"]-1.5)<1e-6, "CCT speed ambiguity retained")
    check(proposal["authoringProposal"]["m_gravity"]==-12 and proposal["reviewRequired"], "CCT units and unresolved ownership recorded")
    check((project/"Assets/C.creator").read_bytes()==legacy_scene, "CCT source remains immutable")

    scene_document=yaml.safe_load(legacy_scene)
    character_entity=next(item for item in scene_document["m_Entities"] if any(component.get("m_typeUUID")==m.LEGACY["CharacterControllerComponent"] for component in item["m_components"]))
    character_components=[item for item in character_entity["m_components"] if item.get("m_typeUUID") in m.LEGACY.values()]
    choice=dict(sourceSha256=m.character_fingerprint(character_components),speedSource="steady",inputPolicy="external_desired_velocity",companionBody="remove_velocity_carrier",braking="static_decay",dynamicDamping="discard",automaticRotation="external",maxFallSpeed=55)
    policy=dict(schema=1,baselineSeconds=1/60,characters={"2169397090":choice})
    character_after,character_map=m.convert(legacy_scene,names,True,policy)
    new_scene=yaml.safe_load(character_after)
    new_character=next(component for item in new_scene["m_Entities"] for component in item["m_components"] if component.get("m_typeUUID")==m.CHARACTER_UUID)
    character_mapping=next(item for item in character_map if "characterId" in item)
    check(new_character["m_instanceID"]==2169397090 and new_character["m_characterSchema"]==1, "CCT instance identity preserved")
    check(new_character["m_gravity"]==-12 and new_character["m_jumpSpeed"]==3 and new_character["m_acceleration"]==60, "reviewed CCT unit conversion")
    check(new_character["m_initialVelocity"]==dict(x=0,y=0,z=0) and new_character["m_cylinderHeight"]==2, "CCT initial state and cylinder height")
    check(character_mapping["retiredComponentIds"]==[4097988415] and character_mapping["discardedCompanion"]["m_instanceID"]==4097988415, "velocity carrier removal audited")
    check(character_mapping["externalInputSpeedMetresPerSecond"]==1.5 and character_mapping["externalInputFollowUpRequired"], "external input speed is explicit product follow-up")
    check(m.convert(character_after,names,True,policy)==(character_after,[]), "CCT migration byte-idempotent")
    def policy_rejects(document, reviewed_policy):
        try:
            m.convert(encode(document),names,True,reviewed_policy)
        except Exception:
            return True
        return False
    changed=copy.deepcopy(scene_document)
    changed["m_Entities"][-1]["m_components"][1]["m_radius"]=.6
    check(policy_rejects(changed,policy), "changed source invalidates reviewed policy")
    for field,value in [("speedSource","guess"),("dynamicDamping","preserve"),("automaticRotation","preserve"),("companionBody","keep"),("maxFallSpeed",0)]:
        invalid_policy=copy.deepcopy(policy)
        invalid_policy["characters"]["2169397090"][field]=value
        check(policy_rejects(scene_document,invalid_policy), f"invalid reviewed choice {field}")
    referenced=copy.deepcopy(scene_document)
    referenced["ExternalCarrierReference"]="#4097988415"
    check(policy_rejects(referenced,policy), "referenced carrier cannot be silently removed")
    overridden=copy.deepcopy(scene_document)
    overridden["m_Entities"][-1]["m_prefabOverrides"]=[dict(m_componentType="CharacterControllerComponent",m_propertyName="maxSpeed",m_valueYaml="2")]
    check(policy_rejects(overridden,policy), "CCT override requires separate typed remapping")
    overridden["m_Entities"][-1]["m_prefabOverrides"]=[dict(m_componentType="CharacterControllerComponent",m_componentSlot=0,m_propertyName="m_radius",m_valueYaml="0.55")]
    override_character,override_character_map=m.convert(encode(overridden),names,True,policy)
    override_node=yaml.safe_load(override_character)["m_Entities"][-1]["m_prefabOverrides"][0]
    check(override_node["m_componentType"]=="CharacterMovementComponent" and override_node["m_propertyName"]=="m_radius", "typed reviewed CCT dimension override mapped")
    reviewed_publication=m.migrate(project,root/"Backup",True,True,1/60,policy)
    check(reviewed_publication["applied"] and reviewed_publication["characters"]==1 and reviewed_publication["bodies"]==2, "explicit policy publication preserves body/character counts")
    with zipfile.ZipFile(reviewed_publication["backup"]) as archive:
        check(archive.read("Assets/C.creator")==legacy_scene, "reviewed CCT original recoverable")

geometry_fixture=yaml.safe_load(source)
geometry_entity=geometry_fixture["PrefabNode"][0]
geometry_component=next(item for item in geometry_entity["m_components"] if "BoxColliderComponent" in item)
geometry_component["m_name"]="MeshColliderComponent"
geometry_component["m_typeUUID"]=m.LEGACY["MeshColliderComponent"]
geometry_component["MeshColliderComponent"]=geometry_component.pop("BoxColliderComponent")
geometry_entity["m_components"].append(dict(m_name="MeshRenderer",m_instanceID=123,m_modelUUID="reviewed-source-needed"))
requirements=m.geometry_recovery_requirements(encode(geometry_fixture))
check(len(requirements)==1 and requirements[0]["supplierType"]=="MeshRenderer" and len(requirements[0]["supplierCandidates"])==1, "mesh supplier recorded without invented geometry")
check(requirements[0]["status"]=="source_recovery_required" and len(requirements[0]["sourceSha256"])==64, "geometry recovery is source hashed and unresolved")
check(rejects(encode(geometry_fixture)), "geometry recovery report does not authorize conversion")
with tempfile.TemporaryDirectory() as folder:
    root=Path(folder)
    project=root/"Project"
    (project/"Assets").mkdir(parents=True)
    (project/"ProjectSetting").mkdir()
    (project/"ProjectSetting/Layers.celayers").write_bytes((repo/"Dynamic_CPP/ProjectSetting/Layers.celayers").read_bytes())
    target=project/"Assets/Mesh.prefab"
    geometry_bytes=encode(geometry_fixture)
    target.write_bytes(geometry_bytes)
    blocked=m.migrate(project,root/"Backup",True,True)
    check(not blocked["applied"] and blocked["geometryRecoveryRequirements"] and target.read_bytes()==geometry_bytes and not (root/"Backup").exists(), "geometry recovery blocks project publication without writes")
for kind,supplier in [("TerrainColliderComponent","TerrainComponent"),("RagdollComponent",None)]:
    component=copy.deepcopy(geometry_component)
    component[kind]=component.pop("MeshColliderComponent")
    component["m_name"]=kind
    component["m_typeUUID"]=m.LEGACY[kind]
    document=dict(m_components=[component])
    report=m.geometry_recovery_requirements(encode(document))
    check(report[0]["supplierType"]==supplier and not report[0]["supplierCandidates"] and report[0]["missingInputs"], "missing terrain/ragdoll source is explicit")

policy_spec=importlib.util.spec_from_file_location("geometry_policy", Path(__file__).with_name("validate-physics-geometry-migration-policy.py"))
gp=importlib.util.module_from_spec(policy_spec)
policy_spec.loader.exec_module(gp)
with tempfile.TemporaryDirectory() as folder:
    root=Path(folder)
    def dependency(name,data):
        (root/name).write_bytes(data)
        return dict(path=name,sha256=m.hashlib.sha256(data).hexdigest())
    geometry=(repo/"Dynamic_CPP/Assets/PhysicsDaggerConvex.cegeometry").read_bytes()
    meta=(repo/"Dynamic_CPP/Assets/PhysicsDaggerConvex.cegeometry.meta").read_bytes()
    parsed_meta=yaml.safe_load(meta)
    binding=dict(authoring=dependency("Legacy.prefab",encode(geometry_fixture)),componentId=requirements[0]["componentId"],componentSha256=requirements[0]["sourceSha256"],
                 supplierSha256=m.hashlib.sha256(json.dumps(requirements[0]["supplierCandidates"][0],sort_keys=True,separators=(",", ":")).encode()).hexdigest(),
                 source=dependency("Model.glb",b"synthetic identity-only source"),geometry=dependency("Convex.cegeometry",geometry),geometryMeta=dependency("Convex.cegeometry.meta",meta),
                 geometryUUID=parsed_meta["guid"],geometryRevision=parsed_meta["geometryRevision"],geometryKind=0)
    policy=dict(schema=1,bindings=[binding])
    result=gp.validate(root,policy)
    check(result["bindings"][0]["status"]=="identity_bound_native_validation_pending" and not result["applied"], "real geometry identity binds without native acceptance claims")
    native_receipt=dict(result="PHYSICS_GEOMETRY_MIGRATION_NATIVE_OK",sourceHash=binding["geometry"]["sha256"],kind=0,revision=1,cookedBytes=1,configuration="Release",executableHash="0"*64)
    if options.native_receipt:
        native_receipt=json.loads(options.native_receipt.read_text(encoding="utf-8-sig"))
    bound=gp.validate(root,policy,[native_receipt])
    check(bound["bindings"][0]["status"]=="native_source_cook_import_verified_shape_conversion_pending" and not bound["applied"], "current native receipt binds exact geometry")
    for field,value in [("sourceHash","changed"),("kind",2),("revision",2),("result","rejected")]:
        invalid_receipt=copy.deepcopy(native_receipt)
        invalid_receipt[field]=value
        try:
            gp.validate(root,policy,[invalid_receipt])
            rejected=False
        except ValueError:
            rejected=True
        check(rejected,"native receipt mismatch rejected "+field)
    for field,value in [("componentSha256","changed"),("supplierSha256","changed"),("geometryRevision",2),("geometryUUID",str(m.uuid.uuid4())),("geometryKind",2)]:
        invalid=copy.deepcopy(policy)
        invalid["bindings"][0][field]=value
        try:
            gp.validate(root,invalid)
            rejected=False
        except ValueError:
            rejected=True
        check(rejected,"geometry binding rejects changed "+field)
    for field in ["source","geometry","geometryMeta","authoring"]:
        invalid=copy.deepcopy(policy)
        invalid["bindings"][0][field]["sha256"]="changed"
        try:
            gp.validate(root,invalid)
            rejected=False
        except ValueError:
            rejected=True
        check(rejected,"geometry dependency hash enforced "+field)
    invalid=copy.deepcopy(policy)
    invalid["bindings"][0]["source"]["path"]="../outside.glb"
    try:
        gp.validate(root,invalid)
        rejected=False
    except ValueError:
        rejected=True
    check(rejected,"geometry dependency path traversal rejected")
    invalid=copy.deepcopy(policy)
    corrupt=bytearray(geometry)
    corrupt[-1]^=1
    invalid["bindings"][0]["geometry"]=dependency("Corrupt.cegeometry",corrupt)
    try:
        gp.validate(root,invalid)
        rejected=False
    except ValueError:
        rejected=True
    check(rejected,"geometry checksum enforced even with matching file hash")
    reviewed=copy.deepcopy(geometry_fixture)
    old=next(item for item in reviewed["PrefabNode"][0]["m_components"] if item.get("m_name")=="MeshColliderComponent")
    for field in list(old):
        if field not in m.META | {"MeshColliderComponent","m_posOffset","m_rotOffset"}:
            del old[field]
    reviewed_bytes=encode(reviewed)
    conversion=copy.deepcopy(policy)
    cb=conversion["bindings"][0]
    cb["authoring"]=dependency("Reviewed.prefab",reviewed_bytes)
    cb["componentSha256"]=m.geometry_recovery_requirements(reviewed_bytes)[0]["sourceSha256"]
    cb["shapePolicy"]=dict(localPosition=old["m_posOffset"],localRotation=old["m_rotOffset"],geometryScale=dict(x=1,y=1,z=1),staticFriction=.4,dynamicFriction=.3,restitution=.1,sourceSelection="explicit synthetic binding for conversion regression")
    (root/"Assets").mkdir()
    (root/"ProjectSetting").mkdir()
    (root/"ProjectSetting/Layers.celayers").write_bytes((repo/"Dynamic_CPP/ProjectSetting/Layers.celayers").read_bytes())
    cb["authoring"]=dependency("Assets/Reviewed.prefab",reviewed_bytes)
    publication=m.migrate(root,root/"Backup",False,True,geometry_policy=conversion,native_receipts=[native_receipt])
    check(not publication["diagnostics"] and publication["bodies"]==1 and not publication["applied"], "reviewed geometry project dry run passes")
    converted,geometry_map=m.convert(reviewed_bytes,names,True,geometry_bindings={cb["componentId"]:cb})
    new_body=next(item for item in yaml.safe_load(converted)["PrefabNode"][0]["m_components"] if item.get("m_name")=="PhysicsBodyComponent")
    new_shape=new_body["m_shapes"][0]
    check(new_shape["kind"]==3 and new_shape["geometryAsset"]==cb["geometryUUID"] and new_shape["geometryRevision"]==1 and new_shape["staticFriction"]==.4, "convex identity and reviewed material converted")
    check(new_shape["shapeId"]==cb["componentId"] and geometry_map[0]["bodyId"]==new_body["m_instanceID"], "geometry body/shape identity preserved")
    invalid=copy.deepcopy(conversion)
    invalid["bindings"][0]["shapePolicy"]["geometryScale"]["x"]=0
    try:
        m.migrate(root,root/"Backup",True,True,geometry_policy=invalid,native_receipts=[native_receipt])
        rejected=False
    except ValueError:
        rejected=True
    check(rejected and (root/"Assets/Reviewed.prefab").read_bytes()==reviewed_bytes, "invalid geometry policy leaves authoring bytes unchanged")
    try:
        m.migrate(root,root/"Backup",True,True,geometry_policy=conversion)
        rejected=False
    except ValueError:
        rejected=True
    check(rejected, "geometry publication requires native receipt")
    applied=m.migrate(root,root/"Backup",True,True,geometry_policy=conversion,native_receipts=[native_receipt])
    check(applied["applied"] and not applied["diagnostics"], "reviewed geometry publication succeeds")
    with zipfile.ZipFile(applied["backup"]) as archive:
        check(archive.read("Assets/Reviewed.prefab")==reviewed_bytes, "geometry original exactly recoverable")
    again=m.migrate(root,root/"SecondBackup",False,True)
    check(again["files"]==0 and not again["diagnostics"], "geometry conversion is idempotent")
    terrain=copy.deepcopy(reviewed)
    entity=terrain["PrefabNode"][0]
    entity["m_components"]=[item for item in entity["m_components"] if item.get("m_name") not in {"RigidBodyComponent","MeshRenderer"}]
    old_terrain=next(item for item in entity["m_components"] if item.get("m_name")=="MeshColliderComponent")
    old_terrain["TerrainColliderComponent"]=old_terrain.pop("MeshColliderComponent")
    old_terrain["m_name"]="TerrainColliderComponent"
    old_terrain["m_typeUUID"]=m.LEGACY["TerrainColliderComponent"]
    old_terrain.pop("m_rotOffset")
    tb=copy.deepcopy(cb)
    tb["componentSha256"]=m.geometry_recovery_requirements(encode(terrain))[0]["sourceSha256"]
    tb["geometryKind"]=2
    terrain_converted,_=m.convert(encode(terrain),names,True,geometry_bindings={tb["componentId"]:tb})
    terrain_body=next(item for item in yaml.safe_load(terrain_converted)["PrefabNode"][0]["m_components"] if item.get("m_name")=="PhysicsBodyComponent")
    check(terrain_body["m_motion"]==0 and terrain_body["m_shapes"][0]["kind"]==5, "standalone terrain converts to static heightfield definition")
    tb["shapePolicy"]["localRotation"]=dict(x=0,y=0,z=1,w=0)
    try:
        m.convert(encode(terrain),names,True,geometry_bindings={tb["componentId"]:tb})
        rejected=False
    except ValueError:
        rejected=True
    check(rejected,"Terrain unrecorded rotation cannot be invented")
    geometry_shapes=new_body["m_shapes"]
    duplicate=copy.deepcopy(policy)
    duplicate["bindings"].append(binding)
    try:
        gp.validate(root,duplicate)
        rejected=False
    except ValueError:
        rejected=True
    check(rejected,"duplicate geometry binding rejected")

out=repo/"Build/Obj/Phase19M1"
out.mkdir(parents=True,exist_ok=True)
(out/"geometry-converted-shapes.yaml").write_text(yaml.safe_dump(geometry_shapes,sort_keys=False),encoding="utf-8")
(out/"geometry-recovery-example.json").write_text(json.dumps(requirements,indent=2))
(out/"primitive-converted.prefab").write_bytes(after)
(out/"primitive-shapes.yaml").write_text(yaml.safe_dump(body["m_shapes"],sort_keys=False),encoding="utf-8")
(out/"capsule-shapes.yaml").write_text(yaml.safe_dump(capsules,sort_keys=False),encoding="utf-8")
(out/"primitive-mappings.json").write_text(json.dumps(mappings,indent=2))
(out/"character-converted.creator").write_bytes(character_after)
(out/"character-policy-example.json").write_text(json.dumps(policy,indent=2))
(out/"character-mappings.json").write_text(json.dumps(character_map,indent=2))
print(json.dumps(dict(result="PHYSICS_SCHEMA_MIGRATION_OK",checks=checks)))
