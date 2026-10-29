#!/usr/bin/env python3
"""Generate typed F# paths from the engine's actual reflgen declaration surface.

The normalized snapshot is checked in so a managed-only build does not need the
Windows SDK. Refresh it from BOTH canonical native modules after native header
changes, then regenerate. --check checks every generated file and rejects drift.
Unsupported callbacks, SDK services, and unsafe pointer calls are explicit in
the manifest and in named unavailable members; they are never fabricated as
successful object/nativeint APIs.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / "Engine/Scripting/Bindings" / "GeneratedNative"
SOURCE_NAME = "native-surface.json"
FORMAT = "creator-native-fsharp-facades"
VERSION = 1


def dump(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2) + "\n"


def quoted(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def identifier(value: str) -> str:
    return "``" + value.replace("`", "_").replace("\n", "_") + "``"


def flatten(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_]", "_", value)
    result = re.sub(r"_+", "_", result).strip("_")
    if not result or result[0].isdigit():
        result = "Native_" + result
    return result


def unqualified(value: str) -> str:
    value = re.sub(r"^(?:class|struct|enum)\s+", "", value.strip())
    value = re.sub(r"(?:\s+(?:const|volatile)|(?<=\*)(?:const|volatile))+$", "", value)
    # A leading const on a pointer belongs to its pointee, not to the pointer
    # value. It is significant when choosing native overloads (const char*).
    if not value.rstrip().endswith("*"):
        value = re.sub(r"^(?:(?:const|volatile)\s+)+", "", value)
    return re.sub(r"\s+", " ", value).strip()


def split_template(value: str) -> tuple[str, list[str]] | None:
    value = unqualified(value)
    start = value.find("<")
    if start < 0 or not value.endswith(">"):
        return None
    parts: list[str] = []
    begin = start + 1
    depth = 0
    for index in range(begin, len(value) - 1):
        token = value[index]
        if token in "<([":
            depth += 1
        elif token in ">)]":
            depth -= 1
        elif token == "," and depth == 0:
            parts.append(value[begin:index].strip())
            begin = index + 1
    parts.append(value[begin:-1].strip())
    return value[:start].strip(), parts


def normalize_paths(value: object) -> object:
    if isinstance(value, list):
        return [normalize_paths(item) for item in value]
    if isinstance(value, dict):
        return {key: normalize_paths(item) for key, item in value.items()}
    if isinstance(value, str):
        normalized = value.replace("\\", "/")
        marker = "/CreatorEngine/CreatorEngine/"
        if marker in normalized:
            return normalized.split(marker, 1)[1]
        for marker in ("/Engine/", "/Lattice/", "/ThirdParty/"):
            if re.match(r"^(?:[A-Za-z]:/|/)", normalized) and marker in normalized:
                return marker[1:] + normalized.split(marker, 1)[1]
    return value


def normalize_type_layouts(value: object) -> object:
    """Keep scalar ABI widths while discarding unused object sizeof metadata.

    These facades exchange JSON and resolve native paths; they do not copy record
    memory. MSVC Debug iterator bookkeeping changes STL and containing-record
    sizes without changing this API, and clang also reports the referred-to size
    for reference types. Record/reference sizes therefore have no typed-facade
    meaning. Preserve primitive/enum/pointer widths, array extents, all nested
    types, and every declaration/signature so genuine API drift still fails.
    """
    if isinstance(value, list):
        return [normalize_type_layouts(item) for item in value]
    if isinstance(value, dict):
        normalized = {key: normalize_type_layouts(item) for key, item in value.items()}
        if (normalized.get("kind") in ("record", "lvalue_reference", "rvalue_reference") and
                "canonical_spelling" in normalized and "size_bytes" in normalized):
            normalized["size_bytes"] = -1
        return normalized
    return value


def component_names(engine: Path) -> list[str]:
    source = (engine / "Engine/SceneRuntime/ComponentTypeUUID.h").read_text(encoding="utf-8-sig")
    return sorted(re.findall(r'\{\s*"(\w+)"\s*,\s*"[0-9a-fA-F-]{36}"\s*\}', source))


def import_surface(paths: list[Path], engine: Path) -> dict:
    classes: dict[str, dict] = {}
    modules = []
    for path in paths:
        document = json.loads(path.read_text(encoding="utf-8-sig"))
        modules.append({"module": document["module"], "format": document["format"],
                        "version": document["version"], "target": document["target"]})
        for header in document["headers"]:
            for original in header["classes"]:
                record = normalize_type_layouts(normalize_paths(original))
                name = record["qualified_name"]
                if name in classes:
                    raise ValueError("duplicate native schema owner: " + name)
                record["module"] = document["module"]
                record["header"] = normalize_paths(header["path"])
                classes[name] = record
    if {module["module"] for module in modules} != {"RenderEngine", "SceneRuntime"}:
        raise ValueError("refresh requires actual RenderEngine and SceneRuntime declaration JSON")
    components = component_names(engine)
    missing = set(components) - classes.keys()
    if missing:
        raise ValueError("native declarations omit components: " + str(sorted(missing)))
    return {"format": "creator-native-declaration-snapshot", "version": VERSION,
            "modules": sorted(modules, key=lambda item: item["module"]),
            "components": components, "classes": [classes[name] for name in sorted(classes)]}


def scalar(name: str, kind: str, size: int = -1) -> dict:
    return {"spelling": name, "canonical_spelling": name, "kind": kind,
            "declaration": name if kind in ("record", "enum") else "",
            "is_const": False, "is_volatile": False, "size_bytes": size,
            "pointee": None, "element_type": None, "array_size": -1}


PRIMITIVES = {
    "void": ("void", -1), "std::monostate": ("void", 1), "bool": ("bool", 1),
    "char": ("signed_integer", 1), "signed char": ("signed_integer", 1),
    "unsigned char": ("unsigned_integer", 1), "std::int8_t": ("signed_integer", 1),
    "std::uint8_t": ("unsigned_integer", 1), "short": ("signed_integer", 2),
    "unsigned short": ("unsigned_integer", 2), "std::int16_t": ("signed_integer", 2),
    "std::uint16_t": ("unsigned_integer", 2), "int": ("signed_integer", 4),
    "unsigned int": ("unsigned_integer", 4), "std::int32_t": ("signed_integer", 4),
    "std::uint32_t": ("unsigned_integer", 4), "long": ("signed_integer", 4),
    "unsigned long": ("unsigned_integer", 4), "long long": ("signed_integer", 8),
    "unsigned long long": ("unsigned_integer", 8), "std::int64_t": ("signed_integer", 8),
    "std::uint64_t": ("unsigned_integer", 8), "std::size_t": ("unsigned_integer", 8),
    "size_t": ("unsigned_integer", 8), "float": ("floating_point", 4),
    "double": ("floating_point", 8), "std::byte": ("unsigned_integer", 1),
}


def synthetic_type(name: str) -> dict:
    name = name.strip()
    # Template element spellings can join the pointer qualifier (`T *const`).
    # That is a const pointer to T, not an undeclared record named "T *const".
    name = re.sub(r"(?<=\*)(?:const|volatile)\b", "", name).strip()
    const = name.startswith("const ")
    if name.endswith("&&") or name.endswith("&") or name.endswith("*"):
        suffix = "&&" if name.endswith("&&") else name[-1]
        kind = {"&&": "rvalue_reference", "&": "lvalue_reference", "*": "pointer"}[suffix]
        result = scalar(name, kind, 8)
        result["pointee"] = synthetic_type(name[:-len(suffix)].strip())
        return result
    plain = unqualified(name)
    kind, size = PRIMITIVES.get(plain, ("record", -1))
    result = scalar(name, kind, size)
    result["is_const"] = const
    if kind == "record":
        result["declaration"] = plain
    return result


def manual_record(name: str, fields: list[tuple[str, str]], source: str,
                  methods: list[dict] | None = None, snapshot_only: bool = False,
                  native_view_only: bool = False) -> dict:
    return {"name": name.rsplit("::", 1)[-1], "qualified_name": name,
            "schema_name": quoted(name), "class_key": "struct", "bases": [],
            "source": {"file": source, "line": 1, "column": 1}, "header": source,
            "module": "manual-runtime", "runtime_only": True, "public_fields": True,
            "manual": True, "snapshot_only": snapshot_only, "native_view_only": native_view_only, "attributes": [],
            "fields": [{"name": field, "owner": name, "access": "public",
                        "type": synthetic_type(kind), "attributes": [], "accessor": False}
                       for field, kind in fields], "methods": methods or []}


def manual_schemas() -> list[dict]:
    math_source = "Engine/Utility_Framework/Reflection/ScriptReflectionMath.h"
    result = []
    for kind, fields in {
        "vector2": "x y", "vector3": "x y z", "vector4": "x y z w",
        "quaternion": "x y z w", "color": "r g b a", "rect": "x y width height",
        "plane": "a b c d",
    }.items():
        result.append(manual_record("math::" + kind, [(field, "float") for field in fields.split()], math_source))
    result.extend([
        manual_record("Core::Sizef", [("width", "float"), ("height", "float")],
                      "Engine/RenderEngine/ScriptReflectionValueTypes.h"),
        manual_record("ce::layers::layer_id", [("value", "std::uint64_t")],
                      "Engine/RenderEngine/ScriptReflectionValueTypes.h"),
        manual_record("math::matrix4x4", [("m", "ce::fixed_array<ce::fixed_array<float, 4>, 4>")], math_source),
        manual_record("math::aabb", [("center", "math::vector3"), ("extents", "math::vector3")], math_source),
        manual_record("math::sphere", [("center", "math::vector3"), ("radius", "float")], math_source),
        manual_record("math::ray", [("origin", "math::vector3"), ("direction", "math::vector3")], math_source),
        manual_record("math::bounding_frustum", [("origin", "math::vector3"), ("orientation", "math::quaternion")]
                      + [(field, "float") for field in "right_slope left_slope top_slope bottom_slope near_plane far_plane".split()], math_source),
        manual_record("std::source_location", [("file", "std::string"), ("function", "std::string"),
                      ("line", "std::uint32_t"), ("column", "std::uint32_t")],
                      "Engine/Utility_Framework/Reflection/ScriptReflectionPrimitives.h", snapshot_only=True),
    ])
    lattice_source = "Engine/RenderEngine/ScriptReflectionLattice.h"
    variant = "std::variant<std::monostate, bool, std::int64_t, double, ce::fixed_array<double, 3>, ce::fixed_array<double, 4>, std::string>"
    lattice = {
        "LXMaterialSource": [("scope", "std::uint64_t"), ("node", "std::uint64_t"), ("pin", "std::uint64_t"),
                             ("instances", "ce::dynamic_array<std::uint64_t>"), ("property", "std::string")],
        "LXMaterialParameter": [("id", "std::uint64_t"), ("identifier", "std::string"), ("name", "std::string"),
                                ("type", "LX::PinType"), ("value", variant), ("colorSpace", "LX::LXColorSpace"), ("exposed", "bool")],
        "LXMaterialResource": [("kind", "LX::LXMaterialResourceKind"), ("slot", "std::uint32_t"),
                               ("parameter", "std::uint64_t"), ("reference", "std::string"),
                               ("colorSpace", "LX::LXColorSpace"), ("source", "LX::LXMaterialSource")],
        "LXMaterialSourceRange": [("firstLine", "std::uint32_t"), ("lastLine", "std::uint32_t"), ("source", "LX::LXMaterialSource")],
        "LXMaterialExecutionRequirements": [("features", "std::uint32_t"), ("forward", "bool"),
                                            ("refraction", "bool"), ("subsurface", "bool"), ("volume", "bool"),
                                            ("error", "LX::LXMaterialExecutionError")],
        "LXMaterialProgram": [("slang", "std::string"), ("semanticKey", "std::string"), ("features", "std::uint32_t"),
                              ("surface", "bool"), ("volume", "bool"), ("textureSamples", "std::uint32_t"),
                              ("parameters", "ce::dynamic_array<LX::LXMaterialParameter>"),
                              ("resources", "ce::dynamic_array<LX::LXMaterialResource>"),
                              ("sourceMap", "ce::dynamic_array<LX::LXMaterialSourceRange>"),
                              ("execution", "LX::LXMaterialExecutionRequirements")],
        "LXMaterialShaderArtifact": [("backend", "std::string"), ("entryPoint", "std::string"),
                                     ("bytecode", "ce::dynamic_array<std::uint8_t>")],
    }
    for name, fields in lattice.items():
        record = manual_record("LX::" + name, fields, lattice_source)
        for field in record["fields"]:
            if field["type"]["canonical_spelling"] in {"LX::PinType", "LX::LXColorSpace", "LX::LXMaterialResourceKind", "LX::LXMaterialExecutionError"}:
                field["type"]["kind"] = "enum"
                field["type"]["size_bytes"] = 1
        result.append(record)
    methods = {
        "math::aabb": [("is_empty", "bool", []), ("min", "math::vector3", []),
                       ("max", "math::vector3", []), ("corner", "math::vector3", [("index", "int")])],
        "math::ray": [("point_at", "math::vector3", [("distance", "float")])],
        "math::plane": [("normal", "math::vector3", [])],
        "math::bounding_frustum": [("corner", "math::vector3", [("index", "std::size_t")]),
                                   ("corners", "std::array<math::vector3, 8>", [])],
    }
    service_source = "Engine/SceneRuntime/ScriptReflectionSceneViews.h"
    service_methods = {
        "CameraSystem": [("GetPrimaryCamera", "CameraComponent *", []),
                         ("GetRegisteredCameras", "const ce::dynamic_array<CameraComponent *> &", []),
                         ("GetCount", "std::size_t", [])],
        "SoundSystem": [("ClipKeys", "ce::dynamic_array<std::string>", []),
                        ("ResolveAsset", "wave::ClipKey", [("text", "std::string_view"), ("kind", "wave::SoundSourceKind")]),
                        ("WorldScope", "wave::PlaybackScope", []), ("PreviewScope", "wave::PlaybackScope", []),
                        ("GetCount", "std::size_t", [])],
        "TransformStore": [("Size", "std::size_t", [])],
        "HierarchyStore": [("Size", "std::size_t", []), ("Revision", "std::uint64_t", []),
                           ("IsOccupied", "bool", [("index", "std::size_t")]),
                           ("ParentOf", "int", [("index", "std::size_t")]),
                           ("RootOf", "int", [("index", "std::size_t")]),
                           ("ChildrenOf", "const ce::dynamic_array<int> &", [("index", "std::size_t")])],
        "SystemSchedule": [("GetSubscriptionCounts", "SystemSchedule::SubscriptionCounts", [])],
        "ScenePhysicsSimulation": [
            ("LayerRevision", "std::uint64_t", []),
            ("ReadCharacter", "std::expected<ScenePhysicsSimulation::character_motion_state, ce::physics::error>", [("binding", "std::uint64_t")]),
            ("RenderCharacterPosition", "std::expected<math::vector3, ce::physics::error>", [("binding", "std::uint64_t")]),
            ("CharacterHandle", "ce::physics::handle<ce::physics::character_tag>", [("binding", "std::uint64_t")]),
            ("Read", "std::expected<ce::physics::body_state, ce::physics::error>", [("binding", "std::uint64_t")]),
            ("Handle", "ce::physics::handle<ce::physics::body_tag>", [("binding", "std::uint64_t")]),
            ("Binding", "std::expected<std::uint64_t, ce::physics::error>", [("body", "ce::physics::handle<ce::physics::body_tag>")]),
            ("RenderPose", "std::expected<ce::physics::pose, ce::physics::error>", [("binding", "std::uint64_t")]),
            ("InterpolationAlpha", "double", []), ("IsRunning", "bool", []),
            ("CompletedSteps", "std::uint64_t", []), ("DroppedSeconds", "double", [])],
        "BasicTweenManager<Scene>": [("ActiveCount", "std::size_t", [])],
        "TextureFramePins": [("Size", "std::size_t", []), ("Borrow", "const Texture *", [("index", "std::size_t")]),
                             ("Owners", "ce::dynamic_array<own::shared_owner<const Texture>>", [])],
    }
    for name, entries in service_methods.items():
        fields = []
        if name == "TransformStore":
            fields = [("localMatrix", "ce::dynamic_array<math::matrix4x4>"),
                      ("worldMatrix", "ce::dynamic_array<math::matrix4x4>"),
                      ("dirty", "ce::dynamic_array<unsigned char>"),
                      ("worldScale", "ce::dynamic_array<math::vector4>"),
                      ("worldQuaternion", "ce::dynamic_array<math::vector4>"),
                      ("worldPosition", "ce::dynamic_array<math::vector4>"),
                      ("worldChanged", "ce::dynamic_array<unsigned char>")]
        record = manual_record(name, fields, service_source, native_view_only=True)
        for field in record["fields"]:
            field["attributes"].append({"scope": "reflgen", "name": "readonly", "argument_tokens": []})
        result.append(record)
        methods[name] = entries
    for record in result:
        for name, returns, parameters in methods.get(record["qualified_name"], []):
            record["methods"].append({"name": name, "owner": record["qualified_name"], "access": "public",
                "is_static": False, "is_const": True, "is_volatile": False, "is_variadic": False,
                "is_deleted": False, "ref_qualifier": "none", "exception_specification": "basic_noexcept",
                "signature": returns + " (" + ", ".join(kind for _, kind in parameters) + ") const",
                "return_type": synthetic_type(returns), "attributes": [],
                "parameters": [{"name": parameter, "type": synthetic_type(kind)} for parameter, kind in parameters]})
            for parameter in record["methods"][-1]["parameters"]:
                if parameter["type"]["canonical_spelling"] == "wave::SoundSourceKind":
                    parameter["type"]["kind"] = "enum"
                    parameter["type"]["size_bytes"] = 1
    return result


@dataclass(frozen=True)
class Mapping:
    fs_type: str
    codec: str
    shape: str
    reason: str = ""
    writable: bool = False
    argument_safe: bool = True

    @property
    def supported(self) -> bool:
        return not self.reason


class Generator:
    def __init__(self, snapshot: dict, namespace: str = "CreatorEngine.NativeTypes"):
        self.namespace = namespace
        self.snapshot = snapshot
        self.components = set(snapshot["components"])
        self.classes = {item["qualified_name"]: item for item in snapshot["classes"]}
        # These types carry native identity or private state that their public
        # field schema cannot reconstruct from a detached JSON value.
        for name in ("RHITextureHandle", "RHIBufferHandle", "FSM::FSMState", "FSM::Transition", "BlackBoard"):
            if name in self.classes:
                self.classes[name] = dict(self.classes[name], native_view_only=True)
        for record in manual_schemas():
            if record["qualified_name"] in self.classes:
                raise ValueError("manual schema duplicates extracted owner: " + record["qualified_name"])
            self.classes[record["qualified_name"]] = record
        self.types: dict[str, dict] = {}
        for record in self.classes.values():
            for field in record["fields"]:
                self.remember(field["type"])
            for method in record["methods"]:
                self.remember(method["return_type"])
                for parameter in method["parameters"]:
                    self.remember(parameter["type"])
        # This generic production specialization is outside extracted class schemas.
        for name in list(self.types):
            template = split_template(name)
            if template and template[0] == "ce::physics::handle":
                record = manual_record(unqualified(name), [("scene", "ce::physics::scene_id"),
                       ("slot", "std::uint32_t"), ("generation", "std::uint32_t")],
                       "Engine/RenderEngine/ScriptReflectionValueTypes.h")
                self.classes.setdefault(record["qualified_name"], record)
            elif template and template[0] == "AssetDepot::AssetLink":
                record = manual_record(unqualified(name), [("identity", "experiment::cooked::AssetIdentity")],
                       "Engine/RenderEngine/ScriptReflectionValueTypes.h")
                for method_name, returns in (("IsValid", "bool"),
                                             ("ToReference", "experiment::cooked::TypedAssetReference")):
                    record["methods"].append({
                        "name": method_name, "owner": record["qualified_name"], "access": "public",
                        "is_static": False, "is_const": True, "is_volatile": False,
                        "is_variadic": False, "is_deleted": False, "ref_qualifier": "none",
                        "exception_specification": "basic_noexcept", "attributes": [],
                        "signature": returns + " () const", "parameters": [],
                        "return_type": synthetic_type(returns)})
                self.classes.setdefault(record["qualified_name"], record)
        self.names = {name: flatten(name) + "View" for name in self.classes}
        duplicate = [name for name, count in Counter(self.names.values()).items() if count > 1]
        if duplicate:
            raise ValueError("ambiguous F# view names: " + str(duplicate))
        self.enums: dict[str, str] = {}
        self.context = ""
        self.manifest: list[dict] = []
        self.files: dict[str, str] = {}

    def remember(self, value: dict) -> None:
        self.types.setdefault(unqualified(value["canonical_spelling"]), value)
        if value.get("pointee"):
            self.remember(value["pointee"])
        if value.get("element_type"):
            self.remember(value["element_type"])

    def qualify(self, name: str) -> str:
        if "::" in name or name in PRIMITIVES or "<" in name or any(token in name for token in "*&"):
            return name
        scopes = self.context.split("::")[:-1]
        while scopes:
            candidate = "::".join(scopes + [name])
            if candidate in self.classes or candidate in self.types:
                return candidate
            scopes.pop()
        return name

    def resolve(self, name: str) -> dict:
        plain = unqualified(name)
        plain = self.qualify(plain)
        if plain in self.types:
            value = dict(self.types[plain])
            value["is_const"] = name.strip().startswith("const ")
            return value
        return synthetic_type(("const " if name.strip().startswith("const ") else "") + plain)

    def unsafe_record_input(self, name: str, active: frozenset[str] = frozenset()) -> bool:
        """Reject proven address/ownership members without guessing other native capabilities.

        A live record view is still useful for navigation. Constructing that same
        record as an invocation-local JSON argument cannot manufacture its native
        pointers, retained owners, callbacks, or reference members.
        """
        if name in active:
            return False
        record = self.classes[name]
        if record.get("native_view_only"):
            return True
        previous_context = self.context
        self.context = name
        active = active | {name}
        try:
            for base in record["bases"]:
                base_name = self.qualify(unqualified(base))
                if base_name in self.classes and self.unsafe_record_input(base_name, active):
                    return True
            return any(self.unsafe_input_member(field["type"], active) for field in record["fields"])
        finally:
            self.context = previous_context

    def unsafe_input_member(self, value: dict, active: frozenset[str]) -> bool:
        kind = value["kind"]
        if kind in ("pointer", "member_pointer", "function", "lvalue_reference", "rvalue_reference"):
            return True
        if kind == "array":
            return self.unsafe_input_member(value["element_type"], active)
        name = self.qualify(unqualified(value["canonical_spelling"]))
        if name in self.classes:
            return self.unsafe_record_input(name, active)
        template = split_template(name)
        if not template:
            return False
        owner, arguments = template
        if owner in ("own::shared_owner", "own::unique_owner", "std::shared_ptr", "std::unique_ptr",
                     "gc::root_ref", "gc::trace_ref") or "func" in owner:
            return True
        if owner in ("ce::dynamic_array", "ce::fixed_array", "std::vector", "std::array", "std::span",
                     "std::set", "std::unordered_set", "std::optional"):
            children = arguments[:1]
        elif owner in ("ce::engine_node_map", "ce::basic_node_hash_map", "ce::basic_flat_hash_map",
                       "std::map", "std::unordered_map", "std::expected"):
            children = arguments[:2]
        elif owner in ("std::variant", "std::tuple", "ce::tuple", "std::pair", "ce::pair"):
            children = arguments
        else:
            return False
        return any(self.unsafe_input_member(self.resolve(child), active) for child in children)

    def mapping(self, value: dict) -> Mapping:
        spelling = value["canonical_spelling"]
        name = self.qualify(unqualified(spelling))
        kind = value["kind"]
        if value.get("is_volatile"):
            return Mapping("", "", "unsupported", "volatile native values cannot cross the typed reflection boundary")
        if kind in ("lvalue_reference", "rvalue_reference"):
            return self.mapping(value["pointee"])
        if kind == "void" or name == "std::monostate":
            return Mapping("unit", "NativeCodecs.Unit", "void")
        if kind == "bool":
            return Mapping("bool", "NativeCodecs.Bool", "scalar", writable=True)
        if kind in ("signed_integer", "unsigned_integer"):
            bits = value["size_bytes"] * 8
            signed = kind == "signed_integer"
            fs = {(True, 8): "sbyte", (False, 8): "byte", (True, 16): "int16", (False, 16): "uint16",
                  (True, 32): "int", (False, 32): "uint32", (True, 64): "int64", (False, 64): "uint64"}.get((signed, bits))
            if fs:
                return Mapping(fs, "NativeCodecs." + ("Int" if signed else "UInt") + str(bits), "scalar", writable=True)
        if kind == "floating_point" and value["size_bytes"] in (4, 8):
            return Mapping("single" if value["size_bytes"] == 4 else "double",
                           "NativeCodecs.Single" if value["size_bytes"] == 4 else "NativeCodecs.Double", "scalar", writable=True)
        if kind == "enum" or name == "ce::layers::error":
            tag = self.enums.setdefault(name, flatten(name) + "Tag")
            return Mapping(f"NativeEnumValue<{tag}>", f"NativeCodecs.Enum<{tag}>({quoted(name)})", "enum", writable=True)
        if name in ("FileGuid", "uuid::uuid16"):
            return Mapping("System.Guid", "NativeCodecs.Guid", "scalar", writable=True)
        if name == "HashedGuid":
            return Mapping("uint64", "NativeCodecs.UInt64", "scalar", writable=True)
        if name == "ce::delegate_handle":
            return Mapping("uint32", "NativeCodecs.UInt32", "scalar", argument_safe=False)
        if name == "std::strong_ordering":
            return Mapping("int", "NativeCodecs.Int32", "scalar", argument_safe=False)
        if name == "std::filesystem::file_time_type":
            return Mapping("int64", "NativeCodecs.Int64", "scalar", writable=True)
        if name == "std::filesystem::path":
            return Mapping("string", "NativeCodecs.String", "scalar", writable=True)
        if name in ("std::string", "std::string_view", "ce::string", "ce::utf8_string", "ce::string_view", "ce::utf8_string_view"):
            return Mapping("string", "NativeCodecs.String", "scalar", writable="view" not in name,
                           argument_safe=name not in ("ce::string_view", "ce::utf8_string_view"))
        if kind == "pointer":
            pointee = value.get("pointee") or self.resolve(name[:-1])
            if pointee["kind"] == "signed_integer" and unqualified(pointee["canonical_spelling"]) == "char" and pointee.get("is_const"):
                return Mapping("string", "NativeCodecs.String", "scalar", writable=False)
            child = self.mapping(pointee)
            if child.supported and child.shape == "record":
                return Mapping(f"NativeReference<{child.fs_type}>", f"NativeCodecs.Reference({child.codec})", "reference", argument_safe=False)
            return Mapping("", "", "unsupported", "raw pointer has no bounded, retained record borrow contract", argument_safe=False)
        if kind == "array":
            child = self.mapping(value["element_type"])
            if child.supported:
                return Mapping(f"NativeArray<{child.fs_type}>", f"NativeCodecs.Array({child.codec})", "sequence",
                               argument_safe=child.argument_safe)
            return Mapping("", "", "unsupported", "array element: " + child.reason)
        if name in self.classes:
            factory = f"(fun value -> {self.names[name]}(value))"
            codec = "NativeCodecs.SnapshotView" if self.classes[name].get("snapshot_only") else "NativeCodecs.View"
            return Mapping(self.names[name], f"{codec}({factory})", "record",
                           argument_safe=not self.unsafe_record_input(name))
        template = split_template(name)
        if template:
            owner, arguments = template
            if (owner == "std::chrono::time_point" and len(arguments) == 1 and
                    arguments[0] in ("std::filesystem::_File_time_clock", "std::filesystem::__file_clock",
                                     "std::chrono::__file_clock")):
                return Mapping("int64", "NativeCodecs.Int64", "scalar", writable=True)
            if owner == "std::atomic" and arguments == ["bool"]:
                return Mapping("bool", "NativeCodecs.Bool", "scalar", argument_safe=False)
            if owner in ("std::basic_string", "std::basic_string_view", "ce::basic_utf8_string", "ce::basic_utf16_string"):
                return Mapping("string", "NativeCodecs.String", "scalar", writable="view" not in owner)
            if owner == "experiment::Index":
                return Mapping("uint32", "NativeCodecs.UInt32", "scalar", writable=True)
            if owner in ("ce::dynamic_array", "ce::fixed_array", "std::vector", "std::array", "std::span",
                         "std::set", "std::unordered_set"):
                child = self.mapping(self.resolve(arguments[0]))
                if child.supported:
                    return Mapping(f"NativeArray<{child.fs_type}>", f"NativeCodecs.Array({child.codec})", "sequence",
                                   argument_safe=child.argument_safe)
                return Mapping("", "", "unsupported", "sequence element: " + child.reason)
            if owner in ("own::shared_owner", "own::unique_owner", "std::shared_ptr", "std::unique_ptr", "gc::root_ref", "gc::trace_ref"):
                child = self.mapping(self.resolve(arguments[0]))
                if child.supported:
                    return Mapping(f"NativeReference<{child.fs_type}>", f"NativeCodecs.Reference({child.codec})", "reference", argument_safe=False)
                return Mapping("", "", "unsupported", "owner pointee: " + child.reason, argument_safe=False)
            if owner in ("ce::engine_node_map", "ce::basic_node_hash_map", "ce::basic_flat_hash_map", "std::map", "std::unordered_map"):
                key, child = (self.mapping(self.resolve(argument)) for argument in arguments[:2])
                if key.supported and child.supported:
                    return Mapping(f"NativeMap<{key.fs_type}, {child.fs_type}>", f"NativeCodecs.Map({key.codec}, {child.codec})", "map", argument_safe=False)
                return Mapping("", "", "unsupported", "map key/value: " + (key.reason or child.reason))
            if owner == "std::optional":
                child = self.mapping(self.resolve(arguments[0]))
                if child.supported:
                    return Mapping(f"NativeOptional<{child.fs_type}>", f"NativeCodecs.Optional({child.codec})", "optional", argument_safe=child.argument_safe)
                return Mapping("", "", "unsupported", "optional value: " + child.reason)
            if owner == "std::expected":
                success, error = (self.mapping(self.resolve(argument)) for argument in arguments[:2])
                if success.supported and error.supported:
                    return Mapping(f"NativeExpected<{success.fs_type}, {error.fs_type}>",
                                   f"NativeCodecs.Expected({success.codec}, {error.codec})", "expected",
                                   argument_safe=success.argument_safe and error.argument_safe)
                return Mapping("", "", "unsupported", "expected value/error: " + (success.reason or error.reason))
            if owner == "std::variant":
                children = [self.mapping(self.resolve(argument)) for argument in arguments]
                if all(child.supported for child in children) and 2 <= len(children) <= 9:
                    return Mapping(f"NativeVariant{len(children)}<" + ", ".join(child.fs_type for child in children) + ">",
                                   "NativeCodecs.Variant(" + ", ".join(child.codec for child in children) + ")", "variant",
                                   argument_safe=all(child.argument_safe for child in children))
                reason = next((child.reason for child in children if not child.supported), "supported arity is 2 through 9")
                return Mapping("", "", "unsupported", "variant alternative: " + reason)
            if owner in ("std::tuple", "ce::tuple", "std::pair", "ce::pair"):
                children = [self.mapping(self.resolve(argument)) for argument in arguments]
                if all(child.supported for child in children) and 2 <= len(children) <= 9:
                    return Mapping(f"NativeTuple{len(children)}<" + ", ".join(child.fs_type for child in children) + ">",
                                   "NativeCodecs.Tuple(" + ", ".join(child.codec for child in children) + ")", "tuple",
                                   argument_safe=all(child.argument_safe for child in children))
                return Mapping("", "", "unsupported", "tuple has an unsupported element or arity")
            if "func" in owner:
                return Mapping("", "", "unsupported", "native callbacks require an explicit rooted callback ABI", argument_safe=False)
        return Mapping("", "", "unsupported", "native type has no declared runtime value schema or explicit typed codec: " + name)

    @staticmethod
    def attributes(value: dict) -> set[str]:
        return {attribute["scope"] + "::" + attribute["name"] for attribute in value.get("attributes", [])}

    @staticmethod
    def serialized_name(field: dict) -> str:
        attributes = sorted(field.get("attributes", []), key=lambda item: item["name"] != "serialized_name")
        for attribute in attributes:
            if attribute["scope"] == "reflgen" and attribute["name"] in ("serialized_name", "name"):
                tokens = "".join(attribute["argument_tokens"])
                try:
                    return json.loads(tokens)
                except (ValueError, TypeError):
                    raise ValueError("cannot evaluate field wire name: " + tokens)
        return field["name"]

    @staticmethod
    def passing(value: dict) -> str:
        if value["kind"] == "lvalue_reference":
            return "const_reference" if value["pointee"].get("is_const") else "mutable_reference"
        if value["kind"] == "rvalue_reference":
            return "rvalue_reference"
        return "value"

    @classmethod
    def output_parameter(cls, value: dict) -> bool:
        if cls.passing(value) == "mutable_reference":
            return True
        while value["kind"] in ("lvalue_reference", "rvalue_reference"):
            value = value["pointee"]
        template = split_template(value["canonical_spelling"])
        # A span is passed by value but its elements are an output buffer. The
        # native invocation owns temporary storage and returns that storage in
        # arguments[], so expose explicit F# byref writeback instead of losing it.
        return bool(template and template[0] == "std::span" and
                    not re.match(r"^const\b", template[1][0].strip()))

    def public_type(self, value: dict) -> str:
        if value["kind"] in ("lvalue_reference", "rvalue_reference"):
            return self.public_type(value["pointee"])
        name = self.qualify(unqualified(value["canonical_spelling"]))
        if name in self.classes:
            return json.loads(self.classes[name]["schema_name"])
        return name

    def method_spec(self, record: dict, method: dict) -> str:
        types = "; ".join(quoted(self.public_type(parameter["type"])) for parameter in method["parameters"])
        passing = "; ".join(quoted(self.passing(parameter["type"])) for parameter in method["parameters"])
        return "NativeMethodSpec(" + ", ".join([
            quoted(json.loads(record["schema_name"])), quoted(method["name"]),
            "true" if method["is_const"] else "false", "[| " + types + " |]", "[| " + passing + " |]",
        ]) + ")"

    def add_entry(self, record: dict, kind: str, value: dict, fs_name: str, mapping: Mapping,
                  reason: str = "", **extra: object) -> None:
        self.manifest.append({"declaringType": record["qualified_name"], "schemaName": json.loads(record["schema_name"]),
                              "kind": kind, "nativeName": value["name"], "fsharpName": fs_name,
                              "fsharpType": mapping.fs_type if not reason else "NativeUnavailableMember",
                              "status": "unavailable" if reason else "typed",
                              "reason": reason, "source": value.get("source", record["source"]), **extra})

    def unavailable(self, record: dict, kind: str, value: dict, name: str, reason: str) -> list[str]:
        signature = value.get("signature", value.get("type", {}).get("canonical_spelling", ""))
        return [f"        /// Unavailable native {kind}: {reason.replace(chr(10), ' ')}",
                f"        member _.{identifier(name)} =",
                "            NativeUnavailableMember(" + ", ".join(quoted(text) for text in
                    (kind, json.loads(record["schema_name"]), value["name"], signature, reason)) + ")", ""]

    def emit_field(self, record: dict, field: dict) -> list[str]:
        mapping = self.mapping(field["type"])
        name = field["name"]
        attributes = self.attributes(field)
        reason = mapping.reason
        if "reflgen::script_internal" in attributes:
            reason = "native field is reserved for engine lifecycle or identity management"
        if reason:
            fs_name = name + "_Unavailable"
            self.add_entry(record, "field", field, fs_name, mapping, reason,
                           nativeType=field["type"]["canonical_spelling"], serializedName=self.serialized_name(field))
            return self.unavailable(record, "field", field, fs_name, reason)
        reserved = {"Field", "At", "Key", "Dereference", "Read", "ReadJson", "Write", "WriteJson", "Invoke", "Borrow", "Describe", "IsLive", "Codec"}
        fs_name = "Native_" + name if name in reserved else name
        expression = "this.Field(" + quoted(name) + ", " + quoted(self.serialized_name(field)) + ")"
        field_type = field["type"]
        if field_type["kind"] in ("lvalue_reference", "rvalue_reference"):
            field_type = field_type["pointee"]
        writable = (mapping.writable and not field_type.get("is_const") and
                    "reflgen::readonly" not in attributes and not record.get("snapshot_only"))
        result = [f"        /// Native field {record['qualified_name']}::{name}.",
                  f"        member this.{identifier(fs_name)}"]
        if writable:
            result += [f"            with get () : {mapping.fs_type} = {expression}.Read({mapping.codec})",
                       f"            and set (value: {mapping.fs_type}) = {expression}.Write({mapping.codec}, value)"]
        else:
            result[-1] += f" : {mapping.fs_type} = {expression}.Read({mapping.codec})"
        result.append("")
        if writable and record["qualified_name"] not in self.components:
            result += ["        /// Return a new client snapshot; this does not mutate this view or a native object.",
                       f"        member this.{identifier('With_' + name)}(value: {mapping.fs_type}) =",
                       "            let fields = System.Collections.Generic.Dictionary<string, JsonElement>(System.StringComparer.Ordinal)",
                       "            for property in this.ReadJson().EnumerateObject() do fields.[property.Name] <- property.Value.Clone()",
                       f"            fields.[{quoted(self.serialized_name(field))}] <- ({mapping.codec}).Encode(value)",
                       f"            {self.names[record['qualified_name']]}(NativeTypeView.FromSnapshot(JsonSerializer.SerializeToElement(fields)))", ""]
        self.add_entry(record, "field", field, fs_name, mapping, nativeType=field["type"]["canonical_spelling"],
                       serializedName=self.serialized_name(field), scalarSetter=writable,
                       navigation=mapping.shape in ("record", "sequence", "map", "reference", "optional", "variant", "expected", "tuple"))
        return result

    def parameter_mapping(self, value: dict) -> Mapping:
        """Rooted callbacks are inputs only, never serializable field values."""
        native = value
        while native["kind"] in ("lvalue_reference", "rvalue_reference"):
            native = native["pointee"]
        template = split_template(native["canonical_spelling"])
        if template and template[0] == "ce::func_detail::owning_func":
            arguments = template[1]
            # The registry constructs exactly ce::owned_func<Signature> with
            # the engine's default policy. Other owning/noexcept/move-only
            # function families retain an explicit unavailable result.
            if len(arguments) >= 7 and arguments[1:7] == ["false", "true", "false", "48", "8", "ce::heap_allocator"]:
                signature = (arguments[0], tuple(re.sub(r"\s+", "", item) for item in arguments[7:]))
                managed = {
                    ("void", ()): "unit",
                    ("void", ("float",)): "single",
                    ("void", ("math::vector2",)): "Float2",
                    ("void", ("conststd::basic_string<char>&",)): "string",
                    ("bool", ("constBlackBoard&",)): "NativeBlackBoardSnapshot",
                }.get(signature)
                if managed:
                    if self.passing(value) not in ("value", "const_reference"):
                        return Mapping("", "", "unsupported", "rooted callbacks accept value or const-reference parameters only")
                    return Mapping(f"NativeCallback<{managed}>", "", "callback", argument_safe=False)
        return self.mapping(value)

    def method_parameter_mapping(self, method: dict, parameter: dict) -> Mapping:
        # The host opts these exact diagnostics into request-owned scalar
        # storage. No pointer address is supplied by F# or kept after the call.
        diagnostic_methods = {
            "GetClipCount", "GetClipName", "GetClipFrameCount", "GetSkeletonSerial",
            "GetClipDuration", "GetBoneCount", "GetBoneName", "ResolveBoneIndex", "BuildAvatarBoneMasks",
        }
        diagnostic_types = {"outViaExperiment": "bool*", "outPath": "AnimatorDataPath*"}
        value = parameter["type"]
        if (method["name"] in diagnostic_methods and value["kind"] == "pointer"
                and re.sub(r"\s+", "", value["canonical_spelling"])
                    == diagnostic_types.get(parameter.get("name"))):
            child = self.mapping(value["pointee"])
            if child.supported:
                return Mapping(child.fs_type, child.codec, "output", argument_safe=False)
        return self.parameter_mapping(value)

    def method_analysis(self, method: dict) -> tuple[Mapping, list[Mapping], str, str]:
        result = self.mapping(method["return_type"])
        parameters = [self.method_parameter_mapping(method, parameter) for parameter in method["parameters"]]
        attributes = self.attributes(method)
        reason = ""
        if "reflgen::script_internal" in attributes:
            reason = "method is reserved for engine lifecycle, scheduling, or identity management"
        elif method.get("is_volatile") or method.get("is_variadic") or method.get("is_static") or method.get("is_deleted"):
            reason = "method has a native signature not supported by instance reflection invocation"
        elif not result.supported:
            reason = "return value: " + result.reason
        else:
            for index, parameter in enumerate(parameters):
                if not parameter.supported:
                    reason = f"argument {index}: " + parameter.reason
                    break
                if not parameter.argument_safe and parameter.shape not in {
                        "record", "sequence", "map", "reference", "optional", "variant", "expected", "tuple", "callback", "output"}:
                    reason = f"argument {index}: native input requires a live typed view; its scalar mapping has no native identity"
                    break
                if self.passing(method["parameters"][index]["type"]) == "rvalue_reference":
                    reason = f"argument {index}: native ownership transfer requires an explicit consuming API"
                    break
        is_reference_result = method["return_type"]["kind"] == "lvalue_reference" or result.shape == "reference"
        safe_borrow_arguments = all(self.passing(parameter["type"]) in ("value", "const_reference") and
                                   not self.output_parameter(parameter["type"]) for parameter in method["parameters"])
        native_arguments = any(not parameter.argument_safe for parameter in parameters)
        # Terrain samples can be large. Re-resolve the bounded read-only span
        # through its component path instead of serializing the whole buffer.
        height_samples = (method["name"] == "GetHeightSamples" and method["is_const"]
                          and not parameters and result.shape == "sequence")
        borrow = ((is_reference_result or height_samples) and not native_arguments and "reflgen::script_once" not in attributes
                  and (not parameters or (method["is_const"] and safe_borrow_arguments)))
        operation = "borrow" if borrow else "invoke"
        if native_arguments or (result.shape == "reference" and not borrow):
            # Paths provide already-existing native objects/owners. One-shot
            # results are copied to immutable snapshots inside that request;
            # mutating methods are never replayed as a getter path.
            operation = "invoke_bound"
        native_return = method["return_type"]
        if (native_return["kind"] == "pointer" and native_return.get("pointee")
                and self.qualify(unqualified(native_return["pointee"]["canonical_spelling"]))
                    in {"FSM::FSMState", "FSM::Transition"}):
            # These exact types have a native component-owned identity adapter.
            # Names and pointer addresses never serve as stable identities.
            operation = "invoke_identity"
        return result, parameters, operation, reason

    def emit_method(self, record: dict, method: dict, fs_name: str, spec_name: str,
                    analysis: tuple[Mapping, list[Mapping], str, str]) -> list[str]:
        result, parameters, operation, reason = analysis
        if reason:
            unavailable_name = fs_name + "_Unavailable"
            self.add_entry(record, "method", method, unavailable_name, result, reason,
                           nativeSignature=method["signature"], nativeReturn=method["return_type"]["canonical_spelling"])
            return self.unavailable(record, "method", method, unavailable_name, reason)
        declarations = []
        names = []
        used: set[str] = set()
        mutable = []
        for index, (parameter, mapping) in enumerate(zip(method["parameters"], parameters)):
            name = parameter.get("name") or "argument" + str(index)
            if name in used:
                name += "_" + str(index)
            used.add(name)
            names.append(identifier(name))
            is_mutable = mapping.shape == "output" or (self.output_parameter(parameter["type"]) and mapping.argument_safe)
            fs_type = f"byref<{mapping.fs_type}>" if is_mutable else mapping.fs_type
            declarations.append(identifier(name) + ": " + fs_type)
            if is_mutable:
                mutable.append((index, identifier(name), mapping))
        if operation in ("invoke_bound", "invoke_identity"):
            bound_arguments = []
            for parameter, mapping, name in zip(method["parameters"], parameters, names):
                if mapping.argument_safe:
                    bound_arguments.append(f"NativeCallArgument.FromJson(({mapping.codec}).Encode({name}))")
                elif mapping.shape == "output":
                    bound_arguments.append(f"NativeCallArgument.FromOutput(({mapping.codec}).Encode({name}))")
                elif mapping.shape == "callback":
                    bound_arguments.append(f"NativeCallArgument.FromCallback({name}.Token)")
                else:
                    bound_arguments.append(f"{name}.AsNativeArgument()")
            arguments = "; ".join(bound_arguments)
        else:
            arguments = "; ".join(f"({mapping.codec}).Encode({name})" for mapping, name in zip(parameters, names))
        lines = [f"        /// Native {method['signature']}; {operation} uses the runtime's exact overload metadata.",
                 f"        member this.{identifier(fs_name)}({', '.join(declarations)}) : {result.fs_type} =",
                 f"            let arguments = [| {arguments} |]"]
        if operation == "borrow":
            lines += [f"            let value = this.Borrow({spec_name}, arguments)",
                      f"            ({result.codec}).Decode(value)"]
        elif operation == "invoke_identity":
            lines.append(f"            let response, value = this.InvokeBoundReference({spec_name}, arguments)")
            for index, name, mapping in mutable:
                lines.append(f"            {name} <- ({mapping.codec}).Decode(NativeTypeView.FromSnapshot(response.Arguments.[{index}]))")
            lines.append(f"            ({result.codec}).Decode(value)")
        else:
            operation_name = "InvokeBound" if operation == "invoke_bound" else "Invoke"
            discard = ", discardResult = true" if operation == "invoke_bound" and result.shape == "void" else ""
            lines.append(f"            let response = this.{operation_name}({spec_name}, arguments{discard})")
            for index, name, mapping in mutable:
                lines.append(f"            {name} <- ({mapping.codec}).Decode(NativeTypeView.FromSnapshot(response.Arguments.[{index}]))")
            if result.shape == "void":
                lines.append("            ()")
            else:
                lines.append(f"            ({result.codec}).Decode(NativeTypeView.FromSnapshot(response.ReturnValue))")
        lines.append("")
        self.add_entry(record, "method", method, fs_name, result, nativeSignature=method["signature"],
                       nativeReturn=method["return_type"]["canonical_spelling"], operation=operation,
                       resultLifetime=("owned_identity" if operation == "invoke_identity" else
                                       "live_path" if operation == "borrow" else "snapshot"),
                       parameters=[{"nativeName": parameter.get("name", ""), "nativeType": parameter["type"]["canonical_spelling"],
                                    "fsharpType": mapping.fs_type, "passing": self.passing(parameter["type"]),
                                    "nativeBinding": not mapping.argument_safe}
                                   for parameter, mapping in zip(method["parameters"], parameters)],
                       refWriteback=[index for index, _, _ in mutable])
        return lines

    def emit_record(self, record: dict) -> str:
        self.context = record["qualified_name"]
        view = self.names[record["qualified_name"]]
        methods = sorted(record["methods"], key=lambda method: (method["name"], method["is_const"], method["signature"]))
        analyses = [self.method_analysis(method) for method in methods]
        counts = Counter((method["name"], tuple((mapping.fs_type, mapping.shape == "output" or self.output_parameter(parameter["type"]))
                         for parameter, mapping in zip(method["parameters"], analysis[1]))) for method, analysis in zip(methods, analyses))
        native_name_counts = Counter(method["name"] for method in methods)
        const_counts = Counter((method["name"], method["is_const"], tuple((mapping.fs_type, mapping.shape == "output" or self.output_parameter(parameter["type"]))
                               for parameter, mapping in zip(method["parameters"], analysis[1]))) for method, analysis in zip(methods, analyses))
        specs = []
        bodies = []
        names: set[str] = set(field["name"] for field in record["fields"])
        for method, analysis in zip(methods, analyses):
            digest = hashlib.sha256((record["qualified_name"] + "::" + method["name"] + method["signature"]).encode()).hexdigest()[:10]
            key = (method["name"], tuple((mapping.fs_type, mapping.shape == "output" or self.output_parameter(parameter["type"]))
                                       for parameter, mapping in zip(method["parameters"], analysis[1])))
            fs_name = method["name"]
            if counts[key] > 1:
                if method["is_const"]:
                    fs_name += "_Const"
                if const_counts[(method["name"], method["is_const"], key[1])] > 1:
                    fs_name += "_" + digest[:6]
            if analysis[3] and native_name_counts[method["name"]] > 1:
                fs_name += "_" + digest[:6]
            if fs_name in names or fs_name in {"Field", "At", "Key", "Dereference", "Read", "ReadJson", "Write", "WriteJson", "Invoke", "Borrow", "Describe", "FromJson", "From", "Codec"}:
                fs_name = "Native_" + fs_name
            spec = "method_" + digest
            if not analysis[3]:
                specs.append(f"    let private {spec} = {self.method_spec(record, method)}")
            bodies.extend(self.emit_method(record, method, fs_name, spec, analysis))
        fields = []
        for field in record["fields"]:
            fields.extend(self.emit_field(record, field))
        lines = ["// <auto-generated />", "// Source: " + record["header"], "namespace " + self.namespace, "",
                 "open System.Text.Json", "open CreatorEngine", "", "[<AutoOpen>]", "module " + view + "Members ="]
        lines.extend(specs)
        if specs:
            lines.append("")
        codec = "NativeCodecs.SnapshotView" if record.get("snapshot_only") else "NativeCodecs.View"
        lines += [f"    type {view} with", "        /// Typed native value codec for collection construction and explicit value arguments.",
                  f"        static member Codec : NativeCodec<{view}> = {codec}(fun source -> {view}(source))", ""]
        if not record.get("native_view_only"):
            lines += ["        /// Construct an explicit read-only native wire snapshot for value arguments.",
                      f"        static member FromJson(value: JsonElement) = {view}(NativeTypeView.FromSnapshot(value))", ""]
        if record["qualified_name"] in self.components:
            lines += ["        /// Capture this component instance. Future accesses reject a removed or replaced instance.",
                      f"        static member From(entity: Entity) = {view}(NativeTypeView.FromNative(entity.GetNativeComponent({record['schema_name']})))", ""]
        elif (not record["bases"] and 0 < len(record["fields"]) <= 20
              and not record.get("snapshot_only") and not record.get("native_view_only")):
            candidates = [(field, self.mapping(field["type"])) for field in record["fields"]]
            if all(mapping.supported and mapping.writable and not field["type"].get("is_const") and
                   "reflgen::readonly" not in self.attributes(field) for field, mapping in candidates):
                declarations = ", ".join(identifier(field["name"]) + ": " + mapping.fs_type for field, mapping in candidates)
                lines += ["        /// Create a typed client value snapshot for native value arguments; no native object is allocated.",
                          f"        static member Create({declarations}) =",
                          "            let fields = System.Collections.Generic.Dictionary<string, JsonElement>(System.StringComparer.Ordinal)"]
                for field, mapping in candidates:
                    lines.append(f"            fields.Add({quoted(self.serialized_name(field))}, ({mapping.codec}).Encode({identifier(field['name'])}))")
                lines += [f"            {view}(NativeTypeView.FromSnapshot(JsonSerializer.SerializeToElement(fields)))", ""]
        if record["qualified_name"] == "wave::ClipKey":
            lines += ["        /// Preserve a legacy clip name even when its text happens to be a GUID.",
                      "        static member CreateText(text: string) =",
                      "            if isNull text then nullArg (nameof text)",
                      "            let fields = System.Collections.Generic.Dictionary<string, JsonElement>(System.StringComparer.Ordinal)",
                      "            fields.Add(\"text\", JsonSerializer.SerializeToElement(text))",
                      "            fields.Add(\"isGuid\", JsonSerializer.SerializeToElement(false))",
                      f"            {view}(NativeTypeView.FromSnapshot(JsonSerializer.SerializeToElement(fields)))", "",
                      "        /// Construct an asset identity without conflating it with a legacy clip name.",
                      "        static member CreateGuid(guid: System.Guid) =",
                      "            if guid = System.Guid.Empty then invalidArg (nameof guid) \"A clip asset GUID must be nonzero.\"",
                      "            let fields = System.Collections.Generic.Dictionary<string, JsonElement>(System.StringComparer.Ordinal)",
                      "            fields.Add(\"text\", JsonSerializer.SerializeToElement(guid.ToString(\"D\")))",
                      "            fields.Add(\"isGuid\", JsonSerializer.SerializeToElement(true))",
                      f"            {view}(NativeTypeView.FromSnapshot(JsonSerializer.SerializeToElement(fields)))", ""]
        if record["qualified_name"] == "RHIShaderBlob":
            lines += ["        /// Read-only indexed bytes from the explicit RHIShaderBlob runtime adapter.",
                      "        member this.Bytes : NativeArray<byte> = this.Read(NativeCodecs.Array(NativeCodecs.UInt8))", ""]
        lines += fields + bodies
        return "\n".join(lines).rstrip() + "\n"

    def generate(self) -> dict[str, str]:
        component_mapping = []
        value_files = []
        component_files = []
        for name, record in sorted(self.classes.items()):
            directory = "Components" if name in self.components else "Values"
            filename = directory + "/" + self.names[name] + ".fs"
            self.files[filename] = self.emit_record(record)
            (component_files if name in self.components else value_files).append(filename)
            if name in self.components:
                component_mapping.append({"cppName": name, "schemaName": json.loads(record["schema_name"]),
                    "viewType": self.namespace + "." + self.names[name], "viewName": self.names[name], "file": filename})
        tags = ["// <auto-generated />", "namespace " + self.namespace, "", "open CreatorEngine", ""]
        for name, tag in sorted(self.enums.items()):
            tags += [f"/// Nominal tag for native enumeration {name}; the provider validates names and integer values.",
                     "[<Sealed>]", f"type {tag} private () = class end", "", "[<RequireQualifiedAccess>]",
                     f"module {flatten(name)}Values =",
                     f"    let FromName(value: string) = NativeEnumValue<{tag}>.FromName({quoted(name)}, value)",
                     f"    let FromInt64(value: int64) = NativeEnumValue<{tag}>.FromInt64({quoted(name)}, value)",
                     f"    let FromUInt64(value: uint64) = NativeEnumValue<{tag}>.FromUInt64({quoted(name)}, value)", ""]
        self.files["EnumTags.fs"] = "\n".join(tags)
        ordered: list[str] = []
        visited: set[str] = set()
        def visit(name: str) -> None:
            if name in visited:
                return
            visited.add(name)
            for base in self.classes[name]["bases"]:
                if base in self.classes:
                    visit(base)
            ordered.append(name)
        for name in sorted(self.classes):
            visit(name)
        shells = ["// <auto-generated />", "// Declare all record views before member extensions to support recursive native graphs.",
                  "namespace " + self.namespace, "", "open CreatorEngine", ""]
        for name in ordered:
            record = self.classes[name]
            parent = next((self.names[base] for base in record["bases"] if base in self.names), "NativeTypeView")
            shells += [f"/// Typed live path or read-only snapshot of native {name}.",
                       f"type {self.names[name]} internal (source: NativeTypeView) =", f"    inherit {parent}(source)", ""]
        self.files["Types.fs"] = "\n".join(shells)
        compile_order = ["EnumTags.fs", "Types.fs"] + value_files + component_files
        self.files["components.json"] = dump(component_mapping)
        self.files["compile-order.json"] = dump(compile_order)
        statuses = Counter(entry["status"] for entry in self.manifest)
        kinds = Counter(entry["kind"] for entry in self.manifest)
        manifest = {"format": FORMAT, "version": VERSION, "namespace": self.namespace,
                    "sourceSha256": hashlib.sha256(dump(self.snapshot).encode()).hexdigest(),
                    "counts": {"components": len(component_mapping), "nativeDeclarations": len(self.snapshot["classes"]),
                               "manualRuntimeSchemas": len(self.classes) - len(self.snapshot["classes"]),
                               "recordViews": len(self.classes), "enumTypes": len(self.enums),
                               "fields": kinds["field"], "methods": kinds["method"],
                               "typedMembers": statuses["typed"], "unavailableMembers": statuses["unavailable"]},
                    "policy": {"unsupported": "named NativeUnavailableMember plus explicit manifest reason",
                               "ownership": "captured component instance and paths; no persistent native addresses",
                               "snapshots": "read-only explicit native wire values",
                               "runtimeCapabilities": "native descriptor capability checks remain authoritative for serialization, assignment and invocation"},
                    "components": component_mapping, "members": self.manifest}
        self.files["coverage.json"] = dump(manifest)
        return self.files


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--surface", type=Path, help="Use a normalized surface snapshot (also used by portable fixture generation)")
    parser.add_argument("--namespace", default="CreatorEngine.NativeTypes", help="F# namespace for generated views")
    parser.add_argument("--declarations", type=Path, action="append", help="Refresh from actual module JSON (repeat for both modules)")
    parser.add_argument("--check", action="store_true", help="Fail if checked-in generated output differs")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*", args.namespace):
        raise SystemExit("--namespace must be a dotted F# identifier")
    source = args.output / SOURCE_NAME
    if args.declarations and args.surface:
        raise SystemExit("--surface and --declarations are mutually exclusive")
    if args.declarations:
        snapshot = import_surface(args.declarations, args.engine)
    else:
        snapshot = normalize_type_layouts(json.loads((args.surface or source).read_text(encoding="utf-8")))
    generated = Generator(snapshot, args.namespace).generate()
    generated[SOURCE_NAME] = dump(snapshot)
    previous = {path.relative_to(args.output).as_posix() for path in args.output.rglob("*") if path.is_file()} if args.output.exists() else set()
    changed = [name for name, content in generated.items()
               if not (args.output / name).is_file() or (args.output / name).read_text(encoding="utf-8") != content]
    stale = previous - generated.keys()
    if args.check:
        if changed or stale:
            raise SystemExit("NATIVE_TYPED_FACADES_STALE changed=" + repr(changed) + " stale=" + repr(sorted(stale)))
    else:
        for name, content in generated.items():
            destination = args.output / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            if name in changed:
                destination.write_text(content, encoding="utf-8", newline="\n")
        for name in stale:
            (args.output / name).unlink()
    print(f"NATIVE_TYPED_FACADES_OK files={len(generated)} components={len(snapshot['components'])} declarations={len(snapshot['classes'])}")


if __name__ == "__main__":
    main()
