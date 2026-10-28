#include "../Utility_Framework/Foundation/EngineNodeSet.h"
#include "../Utility_Framework/Foundation/EngineNodeMap.h"
#include "../Containers/ContainerAPI.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Layers.h"
#else
import Creator.Core.Layers;
#endif
#include "CookedSceneLoader.h"
#include "Scene.h"
#include "Transform.h"
#include "ScriptComponent.h"
#include "PhysicsBodyComponent.h"
#include "CharacterMovementComponent.h"
#include "../Utility_Framework/Authoring/CookedDocumentTree.h"

namespace
{
    std::mutex readersMutex;
    ce::engine_node_map<std::string, CookedSceneLoader::ComponentReader> componentReaders;

    using V3 = math::vector3;
    struct Q4 { float x, y, z, w; };
        const Cedo::Node& Required(const Cedo::Node& node, std::string_view key)
        {
            const auto* value = node.Find(key);
            if (!value) throw std::runtime_error("Missing cooked scene field: " + std::string(key));
            return *value;
        }
        std::string Text(const Cedo::Node& node)
        {
            if (node.kind != Cedo::Kind::Scalar || node.value.find('\0') != std::string::npos)
                throw std::runtime_error("Expected a cooked scalar without embedded NUL");
            return node.value;
        }
        template<class T> T Number(const Cedo::Node& node)
        {
            const auto text = Text(node);
            T result{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                throw std::runtime_error("Invalid cooked numeric field");
            if constexpr (std::is_floating_point_v<T>)
                if (!std::isfinite(result)) throw std::runtime_error("Nonfinite cooked numeric field");
            return result;
        }
        template<class T> T Number(const Cedo::Node& node, const char* name, T fallback)
        {
            const auto* value = node.Find(name);
            return value ? Number<T>(*value) : fallback;
        }
        bool Boolean(const Cedo::Node& node, const char* name, bool fallback)
        {
            const auto* value = node.Find(name);
            if (!value) return fallback;
            const auto text = Text(*value);
            if (text == "true" || text == "1") return true;
            if (text == "false" || text == "0") return false;
            throw std::runtime_error("Invalid cooked boolean field");
        }
        V3 Vector3(const Cedo::Node& node)
        {
            if (node.kind == Cedo::Kind::Sequence && node.children.size() == 3)
                return {Number<float>(node.children[0]), Number<float>(node.children[1]), Number<float>(node.children[2])};
            return {Number<float>(Required(node, "x")), Number<float>(Required(node, "y")), Number<float>(Required(node, "z"))};
        }
        V3 Vector3(const Cedo::Node& node, const char* name, V3 fallback)
        {
            const auto* value = node.Find(name);
            return value ? Vector3(*value) : fallback;
        }
        Q4 Quaternion(const Cedo::Node& node)
        {
            const auto v = Vector3(node);
            const auto w = Number<float>(Required(node, "w"));
            const float length = v.x*v.x + v.y*v.y + v.z*v.z + w*w;
            if (std::abs(length - 1.f) > .001f) throw std::runtime_error("Cooked rotation is not normalized");
            return {v.x, v.y, v.z, w};
        }
        void Fields(const Cedo::Node& node, std::initializer_list<std::string_view> allowed)
        {
            if (node.kind != Cedo::Kind::Map) throw std::runtime_error("Expected a cooked map");
            for (const auto& child : node.children)
                if (std::find(allowed.begin(), allowed.end(), child.key) == allowed.end())
                    throw std::runtime_error("Unknown cooked field: " + child.key);
        }
        void NoReference(const Cedo::Node& node, const char* name)
        {
            if (const auto* value = node.Find(name); value && Text(*value) != "" && Text(*value) != "00000000-0000-0000-0000-000000000000")
                throw std::runtime_error(std::string("Cooked asset provider is required for: ") + name);
        }

}

void CookedSceneLoader::RegisterComponentReader(std::string typeName, ComponentReader reader)
{
    if (typeName.empty() || !reader || typeName == "Transform" || typeName == "ScriptComponent"
        || typeName == "PhysicsBodyComponent" || typeName == "CharacterMovementComponent")
        throw std::invalid_argument("Invalid or reserved cooked component reader");
    std::lock_guard lock(readersMutex);
    if (!componentReaders.emplace(std::move(typeName), reader).second)
        throw std::logic_error("Cooked component reader is already installed");
}

void CookedSceneLoader::Load(Scene& scene, const std::filesystem::path& path)
{
    if (!scene.m_Entities.empty()) throw std::logic_error("Cooked loading requires a new Scene");
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input ? input.tellg() : std::streampos{-1};
    if (size < 0 || static_cast<std::uint64_t>(size) > Cedo::max_document_bytes)
        throw std::runtime_error("Cooked scene missing or too large");

    ce::dynamic_array<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Cooked scene read failed");
    std::string error;
    const auto document = Cedo::Decode(bytes, error);
    if (!document) throw std::runtime_error(error);

    const auto& root = document->root;
    const auto* source = root.Find("m_Entities");
    if (!source) source = root.Find("m_SceneObjects"); // existing serialized alias
    if (!source || source->kind != Cedo::Kind::Sequence || source->children.empty())
        throw std::runtime_error("Invalid cooked entity inventory");
    if (const auto* bundle = root.Find("m_requiredLoadAssetsBundle"))
        for (const auto& child : bundle->children)
            if ((child.kind == Cedo::Kind::Scalar && !child.value.empty()) || !child.children.empty())
                throw std::runtime_error("Scene asset bundle requires an installed asset provider");

    // File slots can be sparse and parents can appear later in the document.
    // This temporary remap is discarded after admission; Scene owns runtime slots.
    ce::engine_node_map<std::int64_t, std::size_t> positions;
    const auto indexValue = [](const Cedo::Node& node) -> std::int64_t {
        const auto value = Number<std::int64_t>(node);
        if (value < -1 || value > (std::numeric_limits<std::uint32_t>::max)()) throw std::runtime_error("Invalid cooked slot reference");
        return value == (std::numeric_limits<std::uint32_t>::max)() ? -1 : value;
    };
    for (std::size_t i = 0; i < source->children.size(); ++i)
    {
        const auto fileIndex = indexValue(Required(source->children[i], "m_index"));
        if (fileIndex < 0 || (i == 0 && fileIndex != 0) || !positions.emplace(fileIndex, i).second)
            throw std::runtime_error("Duplicate or invalid cooked file slot");
    }
    ce::engine_node_set<std::uint64_t> identities;
    ce::dynamic_array<int> parents(source->children.size(), -1);
    for (std::size_t i = 0; i < source->children.size(); ++i)
    {
        const auto& node = source->children[i];
        const auto id = Number<std::uint64_t>(Required(node, "m_instanceID"));
        if (!id || !identities.insert(id).second) throw std::runtime_error("Duplicate cooked entity identity");
        const auto* parentNode = node.Find("m_parentIndex");
        const auto fileParent = parentNode ? indexValue(*parentNode) : (i ? 0 : -1);
        if (i == 0 && fileParent != -1) throw std::runtime_error("Scene root must have no parent");
        if (i != 0)
        {
            const auto found = positions.find(fileParent);
            if (found == positions.end()) throw std::runtime_error("Missing cooked parent slot");
            parents[i] = static_cast<int>(found->second);
        }
        for (const auto& component : Required(node, "m_components").children)
        {
            const auto componentId = Number<std::uint64_t>(Required(component, "m_instanceID"));
            if (!componentId || !identities.insert(componentId).second)
                throw std::runtime_error("Duplicate cooked component identity");
        }
    }
    for (std::size_t i = 1; i < parents.size(); ++i)
    {
        auto parent = parents[i];
        std::size_t depth = 0;
        while (parent != -1)
        {
            if (parent == static_cast<int>(i) || ++depth > 512)
                throw std::runtime_error("Invalid or cyclic cooked entity hierarchy");
            parent = parents[parent];
        }
    }

    for (std::size_t i = 0; i < source->children.size(); ++i)
    {
        const auto& node = source->children[i];
        const auto name = Text(Required(node, "m_name"));
        const auto id = Number<std::uint64_t>(Required(node, "m_instanceID"));
        Entity* entity;
        if (i == 0)
            entity = scene.AddEntity(Entity::Create(scene.ManagedDomain(), &scene, id, name,
                GameObjectType::Empty, Entity::kSceneRootIndex, Entity::kInvalidIndex));
        else
            entity = scene.LoadEntity(id, name, GameObjectType::Empty, Entity::kSceneRootIndex);
        if (!entity || entity->GetInstanceID() != id || entity->m_index != static_cast<int>(i))
            throw std::runtime_error("Scene admission changed cooked identity");

        entity->m_isEnabled = Boolean(node, "m_isEnabled", true);
        if (!entity->SetLayer(ce::layers::layer_id{Number<std::uint64_t>(node, "m_layerId", 1)}))
            throw std::runtime_error("Unknown entity project layer");
        if (const auto* tag = node.Find("m_tag")) entity->SetTag(Text(*tag));
        if (const auto* prefab = node.Find("m_prefabFileGuid")) entity->m_prefabFileGuid = FileGuid{Text(*prefab)};
        entity->SetStatic(Boolean(node, "m_isStatic", false));
        entity->m_attachedSoketID = HashedGuid{Number<std::uint64_t>(node, "m_attachedSoketID", 0)};
        if (const auto* icon = node.Find("m_editorIcon")) entity->m_editorIcon = Text(*icon);
        entity->m_editorLocked = Boolean(node, "m_editorLocked", false);
        if (const auto* overrides = node.Find("m_prefabOverrides"))
            for (const auto& entry : overrides->children)
            {
                PrefabOverride value;
                if (const auto* type = entry.Find("m_componentType")) value.m_componentType = Text(*type);
                value.m_componentSlot = Number<int>(entry, "m_componentSlot", -1);
                value.m_propertyName = Text(Required(entry, "m_propertyName"));
                value.m_valueYaml = Text(Required(entry, "m_valueYaml"));
                entity->m_prefabOverrides.push_back(std::move(value));
            }
        const auto readTransform = [&](const Cedo::Node& values) {
            auto* transform = entity->GetComponent<Transform>();
            const auto p = Vector3(values, "position", {});
            const auto scale = Vector3(values, "scale", {1,1,1});
            transform->SetPositionValue({p.x,p.y,p.z,1}, TransformWriteReason::Reflection);
            transform->SetScaleValue({scale.x,scale.y,scale.z,1}, TransformWriteReason::Reflection);
            if (const auto* rotation = values.Find("rotation"))
            {
                const auto q = Quaternion(*rotation);
                transform->SetRotationValue({q.x,q.y,q.z,q.w}, TransformWriteReason::Reflection);
            }
        };
        if (const auto* legacy = node.Find("m_transform")) readTransform(*legacy);
        bool hasTransform = false;
        for (const auto& component : Required(node, "m_components").children)
        {

            Component* native = nullptr;
            if (component.Find("Transform"))
            {
                if (hasTransform) throw std::runtime_error("Duplicate Transform");
                hasTransform = true;
                readTransform(component);
                native = entity->GetComponent<Transform>();
            }
            else if (component.Find("PhysicsBodyComponent"))
            {
                Fields(component, {"PhysicsBodyComponent", "m_typeUUID", "m_name", "m_instanceID", "m_isEnabled", "m_FileID",
                    "m_motion", "m_mass", "m_gravityEnabled", "m_initialLinearVelocity", "m_initialAngularVelocity", "m_shapes",
                    "m_shapeSchema", "m_translationLocks", "m_rotationLocks", "m_linearDamping", "m_angularDamping"});
                auto* body = entity->AddComponent<PhysicsBodyComponent>();
                if (!body) throw std::runtime_error("Duplicate physics ownership");
                const auto motion = Number<int>(component, "m_motion", 2);
                if (motion < 0 || motion > 2) throw std::runtime_error("Invalid rigid body kind");
                body->m_motion = static_cast<ce::physics::body_kind>(motion);
                body->m_mass = Number<float>(component, "m_mass", 1);
                body->m_gravityEnabled = Boolean(component, "m_gravityEnabled", true);
                body->m_translationLocks = static_cast<ce::physics::axis_lock>(Number<int>(component, "m_translationLocks", 0));
                body->m_rotationLocks = static_cast<ce::physics::axis_lock>(Number<int>(component, "m_rotationLocks", 0));
                body->m_linearDamping = Number<float>(component, "m_linearDamping", 0);
                body->m_angularDamping = Number<float>(component, "m_angularDamping", .05f);
                body->m_initialLinearVelocity = Vector3(component, "m_initialLinearVelocity", {});
                body->m_initialAngularVelocity = Vector3(component, "m_initialAngularVelocity", {});
                if (Number<int>(component, "m_shapeSchema", 1) != 1) throw std::runtime_error("Unknown shape schema");
                ce::dynamic_array<PhysicsShapeDefinition> shapes;
                for (const auto& shape : Required(component, "m_shapes").children)
                {
                    PhysicsShapeDefinition value;
                    value.shapeId = Number<std::uint32_t>(Required(shape, "shapeId"));
                    value.kind = static_cast<PhysicsShapeKind>(Number<int>(Required(shape, "kind")));
                    value.halfExtent = Vector3(shape, "halfExtent", {.5f,.5f,.5f});
                    value.radius = Number<float>(shape, "radius", .5f);
                    value.halfHeight = Number<float>(shape, "halfHeight", .5f);
                    value.localPosition = Vector3(shape, "localPosition", {});
                    if (const auto* qnode = shape.Find("localRotation"))
                    {
                        const auto q = Quaternion(*qnode);
                        value.localRotation = {q.x,q.y,q.z,q.w};
                    }
                    value.staticFriction = Number<float>(shape, "staticFriction", .5f);
                    value.dynamicFriction = Number<float>(shape, "dynamicFriction", .5f);
                    value.restitution = Number<float>(shape, "restitution", 0);
                    value.sensor = Boolean(shape, "sensor", false);
                    value.queryEnabled = Boolean(shape, "queryEnabled", true);
                    value.layerOverride = Number<std::uint64_t>(shape, "layerOverride", 0);
                    if (const auto* role = shape.Find("contactRole")) value.contactRole = Text(*role);
                    if (const auto* asset = shape.Find("geometryAsset")) value.geometryAsset = Text(*asset);
                    value.geometryRevision = Number<std::uint64_t>(shape, "geometryRevision", 0);
                    value.geometryScale = Vector3(shape, "geometryScale", {1,1,1});
                    shapes.push_back(std::move(value));
                }
                body->m_shapes = std::move(shapes); // shared CaptureDefinition performs all runtime validation
                native = body;
            }
            else if (component.Find("ScriptComponent"))
            {
                auto owned = Component::CreateManaged<ScriptComponent>(entity->Domain());
                auto* script = owned.get();
                script->m_scriptType = Text(Required(component, "m_scriptType"));
                if (const auto* fields = component.Find("m_fieldData"))
                    for (const auto& field : fields->children) script->m_fieldData.push_back(Text(field));
                entity->PublishManagedComponent(owned, true); // same owner, mask, pending lifecycle, and rollback
                native = script;
            }
            else if (component.Find("CharacterMovementComponent"))
            {
                if (Number<int>(component, "m_characterSchema", 1) != 1)
                    throw std::runtime_error("Unknown character schema");
                auto* character = entity->AddComponent<CharacterMovementComponent>();
                if (!character) throw std::runtime_error("Duplicate character ownership");
                character->m_radius = Number<float>(component, "m_radius", .5f);
                character->m_cylinderHeight = Number<float>(component, "m_cylinderHeight", 1);
                character->m_contactOffset = Number<float>(component, "m_contactOffset", .05f);
                character->m_stepOffset = Number<float>(component, "m_stepOffset", .3f);
                character->m_slopeLimitCosine = Number<float>(component, "m_slopeLimitCosine", .70710678f);
                character->m_gravity = Number<float>(component, "m_gravity", -9.81f);
                character->m_minimumDistance = Number<float>(component, "m_minimumDistance", .001f);
                character->m_acceleration = Number<float>(component, "m_acceleration", 20);
                character->m_brakingDecay = Number<float>(component, "m_brakingDecay", 8);
                character->m_jumpSpeed = Number<float>(component, "m_jumpSpeed", 5);
                character->m_maxFallSpeed = Number<float>(component, "m_maxFallSpeed", 55);
                character->m_initialVelocity = Vector3(component, "m_initialVelocity", {});
                native = character;
            }
            else
            {
                ComponentReader reader = nullptr;
                {
                    std::lock_guard lock(readersMutex);
                    for (const auto& [type, candidate] : componentReaders)
                        if (component.Find(type))
                        {
                            if (reader) throw std::runtime_error("Ambiguous cooked component type");
                            reader = candidate;
                        }
                }
                if (!reader) throw std::runtime_error("Cooked component provider is not installed");
                native = reader(*entity, component);
                if (!native || native->GetOwner() != entity || native->IsDestroyMark())
                    throw std::runtime_error("Cooked provider did not attach its component to the Scene owner");
            }

            TypeTrait::GUIDCreator::EraseGUID(native->m_instanceID);
            native->m_instanceID = Number<std::uint64_t>(Required(component, "m_instanceID"));
            TypeTrait::GUIDCreator::InsertGUID(native->m_instanceID);
            native->m_isEnabled = Boolean(component, "m_isEnabled", true);
            if (const auto* file = component.Find("m_FileID")) native->m_FileID = FileGuid{Text(*file)};
        }
        entity->RefreshComponentIdIndices();
    }
    // Restore parent links through the ordinary atomic hierarchy transaction.
    for (std::size_t i = 1; i < source->children.size(); ++i)
    {
        const auto parent = parents[i];
        if (parent < 0) throw std::runtime_error("Only the Scene root may have no parent");
        const auto result = scene.Reparent(scene.HandleOf(static_cast<int>(i)), scene.HandleOf(parent));
        if (result != ReparentResult::Success && result != ReparentResult::NoChange)
            throw std::runtime_error(ReparentResultName(result));
    }
    for (std::size_t i = 0; i < source->children.size(); ++i)
    {
        const auto& node = source->children[i];
        if (const auto* rootIndex = node.Find("m_rootIndex"))
        {
            const auto value = indexValue(*rootIndex);
            const auto found = positions.find(value);
            if (value != -1 && found == positions.end())
                throw std::runtime_error("Invalid cooked hierarchy root reference");
            scene.TryGetEntity(static_cast<int>(i))->SetRootIndex(value == -1
                ? Entity::kInvalidIndex : static_cast<Entity::Index>(found->second));
        }
        if (const auto* children = node.Find("m_childrenIndices"))
        {
            if (children->kind != Cedo::Kind::Sequence) throw std::runtime_error("Invalid cooked child inventory");
            ce::dynamic_array<EntityHandle> order;
            for (const auto& child : children->children)
            {
                const auto found = positions.find(indexValue(child));
                if (found == positions.end()) throw std::runtime_error("Invalid cooked child reference");
                order.push_back(scene.HandleOf(static_cast<Entity::Index>(found->second)));
            }
            if (!scene.ReorderChildren(scene.HandleOf(static_cast<int>(i)), order))
                throw std::runtime_error("Cooked child order disagrees with parent membership");
        }
    }
    scene.AllUpdateWorldMatrix(TransformSyncPoint::SceneLoad);
    if (scene.GetHierarchyIntegrityMetrics().Total()) throw std::runtime_error("Cooked hierarchy integrity failed");
}
