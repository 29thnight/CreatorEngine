#include "EditorObjectOperations.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Entity.h"
#include "ReflectionUndo.h"
#include "GameObjectCommand.h"
#include "PrefabUtility.h"
#include "Material.h"
#include "Animator.h"
#include "MeshRenderer.h"
#include <unordered_set>
#include "RectTransformComponent.h"
#include "AuthoringNodeEquality.h"
#include <cmath>
#include <limits>
#include <sstream>
#include <mathematics/color.hpp>
#include <charconv>
#include <memory>

namespace EditorObjectOperations
{
    namespace
    {
        Entity* Resolve(EntityHandle handle)
        {
            // Never retain Scene* or Entity* in undo records across scene replacement.
            for (Scene* scene : SceneManagers->GetScenes())
                if (scene && scene->GetSceneId() == handle.sceneId) { auto* object = scene->Resolve(handle); return object && !object->IsDestroyMark() ? object : nullptr; }
            return nullptr;
        }

        class RenameCommand final : public Meta::IUndoableCommand
        {
        public:
            RenameCommand(EntityHandle target, std::string before, std::string after)
                : m_target(Resolve(target)), m_before(std::move(before)), m_after(std::move(after)) {}
            void Undo() override { if (auto* object = m_target.Resolve()) object->m_name.SetString(m_before); }
            void Redo() override { if (auto* object = m_target.Resolve()) object->m_name.SetString(m_after); }
        private:
            Meta::EntityReference m_target;
            std::string m_before, m_after;
        };

        Component* FindComponent(Entity* object, const std::string& name)
        {
            const auto* type = Meta::Find(name);
            Component* found = nullptr;
            for (const auto& component : object->m_components)
            {
                if (!component || component->IsDestroyMark()) continue;
                const bool matches = name == "#" + std::to_string(component->GetInstanceID())
                    || component->ToString() == name || (type && component->GetTypeID() == type->typeID);
                if (!matches) continue;
                if (found) return nullptr; // require #instanceId for repeated component types
                found = component.get();
            }
            return found;
        }
        const Meta::Property* FindProperty(const Meta::Type* type, const std::string& field)
        {
            for (; type; type = type->parent)
                for (const auto& property : type->properties)
                    if (property.name && field == property.name) return &property;
            return nullptr;
        }
        bool ParsePropertyValue(const Meta::Property& property, const std::string& raw, std::any& value)
        {
            const auto hash = property.typeID;
            if (hash == GUIDCreator::GetTypeID<std::string>()) { value = raw; return true; }
            if (hash == GUIDCreator::GetTypeID<HashingString>()) { value = HashingString(raw); return true; }
            if (property.typeName == "FileGuid")
            { try { value = FileGuid(raw); return true; } catch (...) { return false; } }
            if (hash == GUIDCreator::GetTypeID<bool>() || property.typeName == "bool32")
            {
                if (raw != "true" && raw != "false" && raw != "0" && raw != "1") return false;
                value = raw == "true" || raw == "1"; return true;
            }
            if (property.enumType)
                for (const auto& entry : property.enumType->values)
                    if (entry.name && raw == entry.name) { value = entry.value; return true; }
            std::string buffer = raw;
            for (char& c : buffer) if (c == ',') c = ' ';
            std::istringstream stream(buffer);
            std::vector<double> numbers;
            double number;
            while (stream >> number) { if (!std::isfinite(number)) return false; numbers.push_back(number); }
            if (!stream.eof() || numbers.empty()) return false;
            if (hash == GUIDCreator::GetTypeID<double>() && numbers.size() == 1) { value = numbers[0]; return true; }
            const auto f = [&](size_t i) { return static_cast<float>(numbers[i]); };
            for (double n : numbers) if (std::abs(n) > (std::numeric_limits<float>::max)()) return false;
            if (hash == GUIDCreator::GetTypeID<float>() && numbers.size() == 1) { value = f(0); return true; }
            if (numbers.size() == 1 && numbers[0] == std::trunc(numbers[0]))
            {
                if ((hash == GUIDCreator::GetTypeID<int>() || property.enumType) && numbers[0] >= INT_MIN && numbers[0] <= INT_MAX)
                {
                    const int n = static_cast<int>(numbers[0]);
                    if (property.enumType)
                    {
                        bool valid = false;
                        for (const auto& entry : property.enumType->values) if (entry.value == n) valid = true;
                        if (!valid) return false;
                    }
                    value = n; return true;
                }
                if ((hash == GUIDCreator::GetTypeID<unsigned int>() || property.typeName == "UINT") && numbers[0] >= 0 && numbers[0] <= UINT_MAX)
                { value = static_cast<unsigned int>(numbers[0]); return true; }
            }
            if (hash == GUIDCreator::GetTypeID<math::vector2>() && numbers.size() == 2) { value = math::vector2{f(0), f(1)}; return true; }
            if (hash == GUIDCreator::GetTypeID<math::vector3>() && numbers.size() == 3) { value = math::vector3{f(0), f(1), f(2)}; return true; }
            if (hash == GUIDCreator::GetTypeID<math::vector4>() && numbers.size() == 4) { value = math::vector4{f(0), f(1), f(2), f(3)}; return true; }
            if (hash == GUIDCreator::GetTypeID<math::color>() && numbers.size() == 4) { value = math::color{f(0), f(1), f(2), f(3)}; return true; }
            return false;
        }

        CommandCore::CommandData Snapshot(EntityHandle handle, Entity& object)
        {
            using D = CommandCore::CommandData;
            D data = D::Object();
            data.Set("id", D::String(ObjectId(handle)));
            data.Set("name", D::String(object.m_name.ToString()));
            data.Set("sceneId", D::Int(handle.sceneId));
            data.Set("index", D::Int(handle.index));
            data.Set("generation", D::Int(handle.generation));
            auto* scene = const_cast<Entity&>(object).GetScene();
            data.Set("parent", D::String(ObjectId(scene->HandleOf(object.GetParentIndex()))));
            const auto vector = [](auto value) { D array = D::Array(); array.Append(D::Double(value.x)); array.Append(D::Double(value.y)); array.Append(D::Double(value.z)); return array; };
            if (auto* transform = object.GetComponent<::Transform>())
            {
                data.Set("position", vector(transform->GetPosition()));
                data.Set("scale", vector(transform->GetScale()));
                auto rotation = vector(transform->GetRotation()); rotation.Append(D::Double(transform->GetRotation().w));
                data.Set("rotation", std::move(rotation));
            }
            D children = D::Array();
            for (auto index : object.GetChildrenIndices()) children.Append(D::String(ObjectId(scene->HandleOf(index))));
            data.Set("children", std::move(children));
            D components = D::Array();
            for (const auto& component : object.m_components)
                if (component && !component->IsDestroyMark())
                {
                    D entry = D::Object();
                    entry.Set("id", D::String("#" + std::to_string(component->GetInstanceID())));
                    const auto* type = Meta::Find(component->GetTypeID().m_ID_Data);
                    entry.Set("type", D::String(type ? type->name : component->ToString()));
                    components.Append(std::move(entry));
                }
            data.Set("components", std::move(components));
            return data;
        }
    }

    std::string ObjectId(EntityHandle target)
    {
        return "@" + std::to_string(target.sceneId) + ":" + std::to_string(target.index)
            + ":" + std::to_string(target.generation);
    }

    CommandCore::CommandResult ResolveTarget(const std::string& nameOrId, EntityHandle& target)
    {
        using namespace CommandCore;
        target = {};
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) return PreconditionFailed("scene.none", "No active scene");
        if (!nameOrId.empty() && nameOrId.front() == '@')
        {
            const char* cursor = nameOrId.data() + 1;
            const char* end = nameOrId.data() + nameOrId.size();
            uint32_t values[3]{};
            for (int i = 0; i < 3; ++i)
            {
                const auto parsed = std::from_chars(cursor, end, values[i]);
                if (parsed.ec != std::errc{} || (i < 2 ? parsed.ptr == end || *parsed.ptr != ':' : parsed.ptr != end))
                    return InvalidArguments("Invalid object id; expected @scene:index:generation", "object.id_invalid");
                cursor = i < 2 ? parsed.ptr + 1 : parsed.ptr;
            }
            target = {values[0], values[1], values[2]};
            if (target.sceneId != scene->GetSceneId() || !Resolve(target)) return PreconditionFailed("object.stale", "Object is absent or belongs to another scene");
            return Ok();
        }
        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark() || object->m_name.ToString() != nameOrId) continue;
            if (target.IsValid()) return InvalidArguments("Ambiguous object name; use object id", "object.ambiguous");
            target = scene->HandleOf(object->m_index);
        }
        return target.IsValid() ? Ok() : PreconditionFailed("object.not_found", "Object not found: " + nameOrId);
    }

    CommandCore::CommandResult Describe(EntityHandle target)
    {
        auto* object = Resolve(target);
        if (!object) return CommandCore::PreconditionFailed("object.stale", "Object no longer exists");
        return CommandCore::Ok("object", Snapshot(target, *object));
    }

    CommandCore::CommandResult Properties(EntityHandle target, const std::string& name)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        if (!object) return PreconditionFailed("object.stale", "Object no longer exists");
        auto* component = FindComponent(object, name);
        if (!component) return InvalidArguments("Missing or ambiguous component");
        const auto* type = Meta::Find(component->GetTypeID().m_ID_Data);
        if (!type) return InvalidArguments("Unregistered component type");
        const auto convert = [](auto&& self, const Authoring::ReadNode& node) -> CommandData {
            if (node.IsMap())
            {
                auto data = CommandData::Object();
                for (const auto item : node.Map()) data.Set(item.key.AsString(), self(self, item.value));
                return data;
            }
            if (node.IsSequence())
            {
                auto data = CommandData::Array();
                for (const auto item : node) data.Append(self(self, item));
                return data;
            }
            return CommandData::String(node.AsString());
        };
        auto document = Meta::SerializeDocument(component, *type);
        auto data = CommandData::Object();
        data.Set("component", CommandData::String("#" + std::to_string(component->GetInstanceID())));
        data.Set("values", convert(convert, document.Root().Read()));
        auto fields = CommandData::Array();
        for (auto* current = type; current; current = current->parent)
            for (const auto& property : current->properties)
            {
                auto field = CommandData::Object();
                field.Set("name", CommandData::String(property.name)); field.Set("type", CommandData::String(property.typeName));
                fields.Append(std::move(field));
            }
        data.Set("fields", std::move(fields));
        return Ok("object.properties", std::move(data));
    }

    CommandCore::CommandResult Rename(EntityHandle target, const std::string& name)
    {
        using namespace CommandCore;
        if (name.empty() || name.find('\0') != std::string::npos)
            return InvalidArguments("Name must be non-empty and contain no NUL", "object.name_invalid");
        auto* object = Resolve(target);
        if (!object) return PreconditionFailed("object.stale", "Object no longer exists");
        const std::string before = object->m_name.ToString();
        const bool changed = before != name;
        if (changed) Meta::UndoManager::GetInstance()->Execute(std::make_unique<RenameCommand>(target, before, name));
        auto data = Snapshot(target, *object);
        data.Set("from", CommandData::String(before));
        data.Set("to", CommandData::String(name));
        data.Set("changed", CommandData::Bool(changed));
        return Ok("object.rename", std::move(data));
    }

    CommandCore::CommandResult Create(Scene* scene, const std::string& name, GameObjectType type, uint32_t parent)
    {
        using namespace CommandCore;
        if (!scene) return PreconditionFailed("scene.none", "No active scene");
        if (name.empty() || name.find('\0') != std::string::npos) return InvalidArguments("Invalid object name");
        auto command = std::make_unique<Meta::CreateEntityCommand>(scene, name, type, parent);
        auto* created = command.get();
        Meta::UndoManager::GetInstance()->Execute(std::move(command));
        return Describe(scene->HandleOf(created->GetCreatedIndex()));
    }

    CommandCore::CommandResult Delete(EntityHandle target)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        if (!object || object->IsDestroyMark()) return PreconditionFailed("object.stale", "Object no longer exists");
        if (target.index == 0) return InvalidArguments("Cannot delete the scene root");
        auto data = Snapshot(target, *object);
        Meta::UndoManager::GetInstance()->Execute(std::make_unique<Meta::DeleteGameObjectCommand>(object->GetScene(), target.index));
        return Ok("object.delete", std::move(data));
    }

    CommandCore::CommandResult Duplicate(EntityHandle target, const std::string& name)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        if (!object || object->IsDestroyMark()) return PreconditionFailed("object.stale", "Object no longer exists");
        if (target.index == 0) return InvalidArguments("Cannot duplicate the scene root");
        Scene* scene = object->GetScene();
        auto command = std::make_unique<Meta::DuplicateGameObjectCommand>(scene, target.index, name);
        auto* created = command.get();
        Meta::UndoManager::GetInstance()->Execute(std::move(command));
        return Describe(scene->HandleOf(created->GetCreatedIndex()));
    }

    CommandCore::CommandResult Parent(EntityHandle target, EntityHandle parent)
    {
        using namespace CommandCore;
        Entity* object = Resolve(target);
        Entity* destination = Resolve(parent);
        if (!object || !destination || target.sceneId != parent.sceneId) return PreconditionFailed("object.stale", "Both objects must belong to the same scene");
        if (!target.index) return InvalidArguments("Cannot reparent the scene root");
        for (Entity* ancestor = destination; ancestor; ancestor = ancestor->GetScene()->TryGetEntity(ancestor->GetParentIndex()))
        {
            if (ancestor == object) return InvalidArguments("Parent would create a cycle", "object.parent.cycle");
            if (!ancestor->m_index) break;
        }
        const bool changed = object->GetParentIndex() != parent.index;
        if (changed)
        {
            Meta::EntityReference reference(object), before(object->GetScene()->TryGetEntity(object->GetParentIndex())), after(destination);
            auto beforeRect = std::make_shared<Authoring::WriteDocument>();
            auto afterRect = std::make_shared<Authoring::WriteDocument>();
            auto capturedAfter = std::make_shared<bool>(false);
            if (auto* rect = object->GetComponent<RectTransformComponent>())
                *beforeRect = Meta::SerializeDocument(rect);
            const auto apply = [reference, capturedAfter](Meta::EntityReference destination, const std::shared_ptr<Authoring::WriteDocument>& rectState, bool first)
            {
                auto* object = reference.Resolve(); auto* parent = destination.Resolve();
                if (!object) return;
                Scene* scene = object->GetScene();
                const auto result = scene->Reparent(scene->HandleOf(object->m_index), scene->HandleOf(parent ? parent->m_index : 0));
                if (result != ReparentResult::Success) throw std::runtime_error("Cannot reparent object");
                if (auto* rect = object->GetComponent<RectTransformComponent>())
                {
                    if (first && !*capturedAfter)
                    {
                        rect->SetParentKeepWorldPosition(parent);
                        *rectState = Meta::SerializeDocument(rect); *capturedAfter = true;
                    }
                    else Meta::Deserialize(rect, rectState->Root().Read());
                    scene->MarkUILayoutDirty();
                }
            };
            Meta::MakeCustomChangeCommand([=] { apply(before, beforeRect, false); }, [=] { apply(after, afterRect, true); });
        }
        auto data = Snapshot(target, *object); data.Set("changed", CommandData::Bool(changed));
        return Ok("object.parent", std::move(data));
    }

    CommandCore::CommandResult Transform(EntityHandle target, math::vector3 position, math::quaternion rotation, math::vector3 scale)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        if (!object || !object->GetComponent<::Transform>()) return PreconditionFailed("object.transform.missing", "Object has no Transform");
        for (float value : {position.x, position.y, position.z, rotation.x, rotation.y, rotation.z, rotation.w, scale.x, scale.y, scale.z})
            if (!std::isfinite(value)) return InvalidArguments("Transform requires finite numbers");
        std::vector<PropertyEdit> edits;
        edits.push_back(CapturePropertyEdit(object->Transform_(), {"position", "rotation", "scale"}));
        object->Transform_().SetPosition(position); object->Transform_().SetRotation(rotation); object->Transform_().SetScale(scale);
        const bool changed = CommitPropertyEdits(std::move(edits));
        auto data = Snapshot(target, *object); data.Set("changed", CommandData::Bool(changed));
        return Ok("object.transform", std::move(data));
    }

    PropertyEdit CapturePropertyEdit(Component& component, std::vector<std::string> fields)
    {
        const auto* type = Meta::Find(component.GetTypeID().m_ID_Data);
        auto* object = component.GetOwner();
        return {object->GetScene()->HandleOf(object->m_index), "#" + std::to_string(component.GetInstanceID()),
            std::move(fields), Meta::SerializeDocument(&component, *type)};
    }

    bool CommitPropertyEdits(std::vector<PropertyEdit> edits)
    {
        struct Action { std::function<void()> undo, redo; };
        std::vector<Action> actions;
        for (auto& edit : edits)
        {
            auto* object = Resolve(edit.target);
            auto* component = object ? FindComponent(object, edit.component) : nullptr;
            if (!component) continue; // the gesture's object was removed or its scene changed
            const auto* type = Meta::Find(component->GetTypeID().m_ID_Data);
            if (!type) continue;
            auto before = std::make_shared<Authoring::WriteDocument>(std::move(edit.before));
            auto after = std::make_shared<Authoring::WriteDocument>(Meta::SerializeDocument(component, *type));
            auto fields = std::move(edit.fields);
            std::erase_if(fields, [&](const std::string& field) {
                return Authoring::NodesEqual(before->Root().Read()[field.c_str()], after->Root().Read()[field.c_str()]);
            });
            if (fields.empty()) continue;
            Meta::EntityReference reference(object);
            const std::string componentId = edit.component;
            auto beforeOverrides = object->m_prefabOverrides;
            const auto apply = [reference, componentId, fields, type](const Authoring::WriteDocument& source) {
                if (auto* object = reference.Resolve())
                    if (auto* component = FindComponent(object, componentId))
                    {
                        auto current = Meta::SerializeDocument(component, *type);
                        for (const auto& field : fields) current.Root().Child(field).Assign(source.Root()[field.c_str()]);
                        Meta::Deserialize(component, *type, current.Root().Read());
                    }
            };
            actions.push_back({[=] {
                apply(*before); if (auto* object = reference.Resolve()) object->m_prefabOverrides = beforeOverrides;
            }, [=] {
                apply(*after); if (auto* object = reference.Resolve())
                    if (auto* component = FindComponent(object, componentId))
                        for (const auto& field : fields) PrefabUtility::RecordPropertyOverride(*object, *component, field);
            }});
        }
        if (actions.empty()) return false;
        Meta::MakeCustomChangeCommand([actions] {
            for (auto it = actions.rbegin(); it != actions.rend(); ++it) it->undo();
        }, [actions] { for (const auto& action : actions) action.redo(); });
        return true;
    }

    bool CommitProperty(Component& component, const std::string& field, Authoring::WriteDocument before)
    {
        auto* object = component.GetOwner();
        std::vector<PropertyEdit> edits;
        edits.push_back({object->GetScene()->HandleOf(object->m_index), "#" + std::to_string(component.GetInstanceID()), {field}, std::move(before)});
        return CommitPropertyEdits(std::move(edits));
    }

    CommandCore::CommandResult Property(EntityHandle target, const std::string& componentName, const std::string& field, const std::string& raw)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        if (!object) return PreconditionFailed("object.stale", "Object no longer exists");
        auto* component = FindComponent(object, componentName);
        if (!component) return InvalidArguments("Missing or ambiguous component; use its #id", "component.not_found");
        const auto* type = Meta::Find(component->GetTypeID().m_ID_Data);
        const auto* property = FindProperty(type, field);
        if (!property || !property->setter) return InvalidArguments("Field is not editable", "property.not_found");
        // Engine identity and ownership fields are not authoring properties.
        if (field == "m_instanceID" || field == "m_index" || field == "m_typeID") return InvalidArguments("Identity fields are read-only");
        std::any value;
        if (!ParsePropertyValue(*property, raw, value)) return InvalidArguments("Value does not match property type", "property.value_invalid");
        auto before = Meta::SerializeDocument(component, *type);
        property->setter(component, value);
        const bool changed = CommitProperty(*component, field, std::move(before));
        auto data = Snapshot(target, *object); data.Set("changed", CommandData::Bool(changed));
        data.Set("field", CommandData::String(field)); data.Set("value", CommandData::String(raw));
        return Ok("object.property", std::move(data));
    }

    CommandCore::CommandResult Select(Scene* scene, const std::vector<EntityHandle>& targets)
    {
        using namespace CommandCore;
        if (!scene) return PreconditionFailed("scene.none", "No active scene");
        Meta::SelectionSnapshot before, after;
        before.Capture(scene); after.sceneId = scene->GetSceneId();
        for (auto target : targets)
        {
            auto* object = Resolve(target);
            if (!object || target.sceneId != after.sceneId) return PreconditionFailed("object.stale", "Selection contains a stale object");
            Meta::EntityReference reference(object);
            if (std::none_of(after.selected.begin(), after.selected.end(), [&](const auto& entry) { return entry.guid == reference.guid; }))
                after.selected.push_back(reference);
            after.primary = reference;
        }
        bool changed = before.selected.size() != after.selected.size() || before.primary.guid != after.primary.guid;
        for (size_t i = 0; !changed && i < before.selected.size(); ++i) changed = before.selected[i].guid != after.selected[i].guid;
        if (changed) Meta::MakeCustomChangeCommand([before] { before.Apply(); }, [after] { after.Apply(); });
        auto data = CommandData::Object(); data.Set("changed", CommandData::Bool(changed));
        data.Set("count", CommandData::Int(after.selected.size()));
        return Ok("scene.select", std::move(data));
    }

    CommandCore::CommandResult AddComponent(EntityHandle target, const std::string& typeName)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        if (!object) return PreconditionFailed("object.stale", "Object no longer exists");
        const auto found = ComponentFactorys->m_componentTypes.find(typeName);
        if (found == ComponentFactorys->m_componentTypes.end() || !found->second) return InvalidArguments("Unknown component type");
        const auto* type = found->second;
        for (const auto& component : object->m_components)
            if (component && component->GetTypeID() == type->typeID && !component->IsDestroyMark())
                {
                    auto result = Describe(target); result.data.Set("changed", CommandData::Bool(false)); return result;
                }
        Meta::EntityReference reference(object);
        auto id = std::make_shared<std::string>();
        auto snapshot = std::make_shared<Authoring::WriteDocument>();
        Meta::MakeCustomChangeCommand([=] {
            if (auto* object = reference.Resolve()) if (auto* component = FindComponent(object, *id)) object->RemoveComponent(component);
        }, [=] {
            if (auto* object = reference.Resolve())
            {
                if (id->empty())
                {
                    auto* component = object->AddComponent(*type);
                    if (!component) throw std::runtime_error("Cannot add component");
                    if (auto* initializable = dynamic_cast<System::IInitializable*>(component)) initializable->Initialize();
                    *id = "#" + std::to_string(component->GetInstanceID());
                    *snapshot = Meta::SerializeDocument(component, *type);
                }
                else { const auto node = snapshot->Root().Read(); ComponentFactorys->LoadComponent(object, Authoring::NodeViewAccess::Make(node), false); }
            }
        });
        return Describe(target);
    }

    CommandCore::CommandResult RemoveComponent(EntityHandle target, const std::string& name)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        if (!object) return PreconditionFailed("object.stale", "Object no longer exists");
        auto* component = FindComponent(object, name);
        if (!component) return InvalidArguments("Missing or ambiguous component");
        if (dynamic_cast<::Transform*>(component) || dynamic_cast<RectTransformComponent*>(component)) return InvalidArguments("Spatial component is required by the object type");
        auto* type = Meta::Find(component->GetTypeID().m_ID_Data);
        auto snapshot = std::make_shared<Authoring::WriteDocument>(Meta::SerializeDocument(component, *type));
        const std::string id = "#" + std::to_string(component->GetInstanceID());
        Meta::EntityReference reference(object);
        Meta::MakeCustomChangeCommand([=] {
            if (auto* object = reference.Resolve())
            {
                const auto node = snapshot->Root().Read();
                ComponentFactorys->LoadComponent(object, Authoring::NodeViewAccess::Make(node), false);
            }
        }, [=] {
            if (auto* object = reference.Resolve()) if (auto* component = FindComponent(object, id)) object->RemoveComponent(component);
        });
        return Describe(target);
    }

    CommandCore::CommandResult InstantiatePrefab(const std::string& prefabName, const std::string& name)
    {
        return InstantiatePrefab(PrefabUtilitys->LoadPrefab(prefabName), name.empty() ? prefabName : name);
    }

    CommandCore::CommandResult InstantiatePrefab(Prefab* prefab, const std::string& name)
    {
        using namespace CommandCore;
        if (!prefab) return PreconditionFailed("prefab.not_found", "Prefab does not exist");
        auto deletion = std::make_shared<std::unique_ptr<Meta::DeleteGameObjectCommand>>();
        auto reference = std::make_shared<Meta::EntityReference>();
        Meta::MakeCustomChangeCommand([=] { if (*deletion) (*deletion)->Redo(); }, [=] {
            if (*deletion) { (*deletion)->Undo(); return; }
            auto* object = PrefabUtilitys->InstantiatePrefab(prefab, name);
            if (!object) throw std::runtime_error("Cannot instantiate prefab");
            *reference = Meta::EntityReference(object);
            *deletion = std::make_unique<Meta::DeleteGameObjectCommand>(object->GetScene(), object->m_index);
        });
        auto* object = reference->Resolve();
        return object ? Describe(object->GetScene()->HandleOf(object->m_index)) : Fail("prefab.instantiate_failed", "Cannot instantiate prefab");
    }

    CommandCore::CommandResult MaterialMode(const std::vector<std::shared_ptr<Material>>& materials, MaterialRenderingMode mode)
    {
        using namespace CommandCore;
        if (mode != MaterialRenderingMode::Opaque && mode != MaterialRenderingMode::Transparent)
            return InvalidArguments("Material mode must be opaque or transparent");
        std::vector<std::pair<std::shared_ptr<Material>, MaterialRenderingMode>> changes;
        std::unordered_set<Material*> seen;
        for (const auto& material : materials)
            if (material && seen.insert(material.get()).second && material->m_renderingMode != mode)
                changes.emplace_back(material, material->m_renderingMode);
        if (seen.empty()) return PreconditionFailed("material.not_found", "No material in target hierarchy");
        if (!changes.empty()) Meta::MakeCustomChangeCommand(
            [changes] { for (const auto& [material, before] : changes) material->m_renderingMode = before; },
            [changes, mode] { for (const auto& [material, before] : changes) material->m_renderingMode = mode; });
        auto data = CommandData::Object();
        data.Set("materials", CommandData::Int(seen.size()));
        data.Set("changed", CommandData::Int(changes.size()));
        data.Set("mode", CommandData::String(mode == MaterialRenderingMode::Opaque ? "opaque" : "transparent"));
        return Ok("Shared material mode applied", std::move(data));
    }

    CommandCore::CommandResult MaterialMode(EntityHandle target, MaterialRenderingMode mode)
    {
        auto* object = Resolve(target);
        if (!object) return CommandCore::PreconditionFailed("object.stale", "Object no longer exists");
        std::vector<std::shared_ptr<Material>> materials;
        std::function<void(Entity*)> collect = [&](Entity* node) {
            if (!node || node->IsDestroyMark()) return;
            for (const auto& component : node->m_components)
                if (auto* renderer = dynamic_cast<MeshRenderer*>(component.get()); renderer && !renderer->IsDestroyMark())
                    materials.push_back(renderer->m_Material);
            for (auto index : node->GetChildrenIndices()) collect(node->OwnerSceneFindIndex(index));
        };
        collect(object);
        auto result = MaterialMode(materials, mode);
        result.data.Set("id", CommandCore::CommandData::String(ObjectId(target)));
        return result;
    }

    CommandCore::CommandResult AnimatorParameter(EntityHandle target, const std::string& name, ValueType type)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        auto* animator = object ? object->GetComponent<Animator>() : nullptr;
        if (!animator) return PreconditionFailed("animator.not_found", "Animator is unavailable");
        if (name.empty() || (type != ValueType::Bool && type != ValueType::Float && type != ValueType::Int && type != ValueType::Trigger))
            return InvalidArguments("Animator parameter requires a name and a supported type");
        auto* existing = animator->FindParameter(name);
        if (existing && existing->vType != type) return InvalidArguments("Existing parameter has a different type");
        if (!existing)
        {
            Meta::EntityReference reference(object);
            const std::string componentId = "#" + std::to_string(animator->GetInstanceID());
            const auto resolve = [reference, componentId]() { auto* object = reference.Resolve(); return object ? dynamic_cast<Animator*>(FindComponent(object, componentId)) : nullptr; };
            Meta::MakeCustomChangeCommand([resolve, name] {
                if (auto* animator = resolve()) for (size_t i = 0; i < animator->Parameters.size(); ++i)
                    if (animator->Parameters[i]->name == name) { animator->DeleteParameter(static_cast<int>(i)); break; }
            }, [resolve, name, type] {
                if (auto* animator = resolve())
                {
                    if (auto* existing = animator->FindParameter(name); existing && existing->vType != type)
                        throw std::runtime_error("Animator parameter changed type before redo");
                    if (type == ValueType::Float) animator->AddParameter(name, 0.f, type);
                    else if (type == ValueType::Int) animator->AddParameter(name, 0, type);
                    else animator->AddParameter(name, false, type);
                }
            });
        }
        auto data = CommandData::Object();
        data.Set("id", CommandData::String(ObjectId(target))); data.Set("name", CommandData::String(name));
        data.Set("type", CommandData::String(type == ValueType::Float ? "float" : type == ValueType::Int ? "int" : type == ValueType::Bool ? "bool" : "trigger"));
        data.Set("changed", CommandData::Bool(existing == nullptr));
        return Ok({}, std::move(data));
    }

    CommandCore::CommandResult AnimatorDefaultParameter(Animator& animator, ValueType type)
    {
        auto* object = animator.GetOwner();
        if (!object || !object->GetScene()) return CommandCore::PreconditionFailed("object.stale", "Animator owner is unavailable");
        const std::string base = type == ValueType::Float ? "NewFloat" : type == ValueType::Int ? "NewInt" : type == ValueType::Bool ? "NewBool" : "NewTrigger";
        std::string name = base;
        for (size_t suffix = 1; animator.FindParameter(name); ++suffix) name = base + std::to_string(suffix);
        return AnimatorParameter(object->GetScene()->HandleOf(object->m_index), name, type);
    }

    CommandCore::CommandResult UndoRedo(bool redo)
    {
        using namespace CommandCore;
        auto* undo = Meta::UndoManager::GetInstance();
        const auto depth = redo ? (undo->m_isGameMode ? undo->GameRedoDepth() : undo->EditRedoDepth())
                                : (undo->m_isGameMode ? undo->GameUndoDepth() : undo->EditUndoDepth());
        if (redo) undo->Redo(); else undo->Undo();
        auto data = CommandData::Object();
        data.Set("changed", CommandData::Bool(depth != 0));
        data.Set("undoDepth", CommandData::Int(undo->m_isGameMode ? undo->GameUndoDepth() : undo->EditUndoDepth()));
        data.Set("redoDepth", CommandData::Int(undo->m_isGameMode ? undo->GameRedoDepth() : undo->EditRedoDepth()));
        return Ok(redo ? "redo" : "undo", std::move(data));
    }
}
