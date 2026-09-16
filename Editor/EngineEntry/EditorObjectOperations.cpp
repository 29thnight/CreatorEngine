#include "EditorObjectOperations.h"
#include "EditorEntityIcons.h"
#include "EditorSessionState.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Entity.h"
#include "ReflectionUndo.h"
#include "GameObjectCommand.h"
#include "PrefabUtility.h"
#include "Material.h"
#include "Animator.h"
#include "MeshRenderer.h"
// PBR-W8 — MeshRenderer.h 는 MaterialInstance·ModelMeshHandle 을 전방선언만 한다.
#include "MaterialScriptBinding.h"
#include "Experiment/MaterialInstance.h"
#include "Assets/ModelAssetGeneration.h"
#include "StandardMaterialProperty.h"
#include "ScriptComponent.h"
#include "ClrHost.h"
#include <unordered_set>
#include "RectTransformComponent.h"
#include "AuthoringNodeEquality.h"
#include <cmath>
#include <limits>
#include <sstream>
#include <mathematics/color.hpp>
#include <charconv>
#include <memory>
#include <utility>

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
            data.Set("editorIcon", D::String(object.m_editorIcon));
            data.Set("editorLocked", D::Bool(object.m_editorLocked));
            data.Set("editBlocked", D::Bool(IsEditLocked(&object, true)));
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

    bool IsEditLocked(const Entity* target, bool includeDescendants)
    {
        if (!target) return false;
        auto* scene = const_cast<Entity*>(target)->GetScene();
        for (auto* ancestor = target; ancestor;)
        {
            if (ancestor->m_editorLocked) return true;
            if (!scene || ancestor->m_index == 0) break;
            ancestor = scene->TryGetEntity(ancestor->GetParentIndex());
        }
        if (includeDescendants && scene)
            for (auto child : target->GetChildrenIndices())
                if (IsEditLocked(scene->TryGetEntity(child), true)) return true;
        return false;
    }

    CommandCore::CommandResult SetEditLocked(EntityHandle target, bool locked)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        if (!object) return PreconditionFailed("object.stale", "Object no longer exists");
        if (IsEditLocked(object->GetScene()->TryGetEntity(object->GetParentIndex())))
            return PreconditionFailed("object.locked", "Unlock the parent first");
        const bool before = object->m_editorLocked;
        if (before != locked)
        {
            const Meta::EntityReference reference(object);
            Meta::MakeCustomChangeCommand(
                [reference, before] { if (auto* entity = reference.Resolve()) entity->m_editorLocked = before; },
                [reference, locked] { if (auto* entity = reference.Resolve()) entity->m_editorLocked = locked; });
        }
        return Describe(target);
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
        if (IsEditLocked(object, true)) return PreconditionFailed("object.locked", "Unlock the entity or its hierarchy before editing");
        const std::string before = object->m_name.ToString();
        const bool changed = before != name;
        if (changed) Meta::UndoManager::GetInstance()->Execute(std::make_unique<RenameCommand>(target, before, name));
        auto data = Snapshot(target, *object);
        data.Set("from", CommandData::String(before));
        data.Set("to", CommandData::String(name));
        data.Set("changed", CommandData::Bool(changed));
        return Ok("object.rename", std::move(data));
    }

    CommandCore::CommandResult SetIcon(EntityHandle target, const std::string& preset)
    {
        using namespace CommandCore;
        if (editor::EntityIconPresets[editor::EntityIconIndex(preset)].id != preset)
            return InvalidArguments("Unknown entity icon preset");
        auto* object = Resolve(target);
        if (!object) return PreconditionFailed("object.stale", "Object no longer exists");
        if (IsEditLocked(object, true)) return PreconditionFailed("object.locked", "Unlock the entity or its hierarchy before editing");
        const std::string before = object->m_editorIcon;
        if (before != preset)
        {
            const Meta::EntityReference reference(object);
            Meta::MakeCustomChangeCommand(
                [reference, before] { if (auto* entity = reference.Resolve()) entity->m_editorIcon = before; },
                [reference, preset] { if (auto* entity = reference.Resolve()) entity->m_editorIcon = preset; });
        }
        return Describe(target);
    }

    CommandCore::CommandResult Create(Scene* scene, const std::string& name, GameObjectType type, uint32_t parent)
    {
        using namespace CommandCore;
        if (!scene) return PreconditionFailed("scene.none", "No active scene");
        if (name.empty() || name.find('\0') != std::string::npos) return InvalidArguments("Invalid object name");
        if (IsEditLocked(scene->TryGetEntity(parent)))
            return PreconditionFailed("object.locked", "Cannot create inside a locked hierarchy");
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
        if (IsEditLocked(object, true)) return PreconditionFailed("object.locked", "Unlock the entity or its hierarchy before editing");
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
        if (IsEditLocked(object, true) || IsEditLocked(destination))
            return PreconditionFailed("object.locked", "Unlock the entity hierarchy before reparenting");
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
        if (IsEditLocked(object, true)) return PreconditionFailed("object.locked", "Unlock the entity or its hierarchy before editing");
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
            if (IsEditLocked(object, true)) { apply(*before); continue; }
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
        if (IsEditLocked(object, true)) return PreconditionFailed("object.locked", "Unlock the entity or its hierarchy before editing");
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

    CommandCore::CommandResult NavigateSelection(Scene* scene, int direction)
    {
        using namespace CommandCore;
        if (!scene) return PreconditionFailed("scene.none", "No active scene");
        if (direction != -1 && direction != 1) return InvalidArguments("Direction must be -1 or 1");
        auto& history = EditorSessionState::Get().SelectionHistory();
        const auto handle = history.Move(direction, [scene](EntityHandle handle) {
            auto* entity = scene->Resolve(handle);
            return entity && !entity->IsDestroyMark();
        });
        if (!handle.IsValid()) return PreconditionFailed("selection.history.end", "No selection in that direction");
        Meta::SelectionSnapshot snapshot;
        snapshot.sceneId = scene->GetSceneId();
        snapshot.primary = Meta::EntityReference(scene->Resolve(handle));
        snapshot.selected.push_back(snapshot.primary);
        snapshot.Apply();
        return Describe(handle);
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
        auto* selected = scene->m_selectedEntity;
        EditorSessionState::Get().SelectionHistory().Observe(scene->GetSceneId(),
            selected ? scene->HandleOf(selected->m_index) : EntityHandle{});
        auto data = CommandData::Object(); data.Set("changed", CommandData::Bool(changed));
        data.Set("count", CommandData::Int(after.selected.size()));
        return Ok("scene.select", std::move(data));
    }

    CommandCore::CommandResult AddComponent(EntityHandle target, const std::string& typeName)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        if (!object) return PreconditionFailed("object.stale", "Object no longer exists");
        if (IsEditLocked(object, true)) return PreconditionFailed("object.locked", "Unlock the entity or its hierarchy before editing");
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

    CommandCore::CommandResult AddManagedScript(EntityHandle target, const std::string& typeName)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        if (!object) return PreconditionFailed("object.stale", "Object no longer exists");
        if (IsEditLocked(object, true)) return PreconditionFailed("object.locked", "Unlock the entity or its hierarchy before editing");
        auto& clr = ClrHost::Get();
        if (!clr.IsReady()) return PreconditionFailed("script.not_ready", "The script runtime is not ready");
        const auto names = clr.GetComponentTypeNames();
        if (std::ranges::find(names, typeName) == names.end())
            return InvalidArguments("The script is not compiled or is not an attachable component", "script.type_not_found");
        const auto* type = Meta::Find(type_guid(ScriptComponent));
        if (!type) return InternalError("script.component_type_missing", "Script component type is unavailable");

        // Create before publishing an undo record: failure must leave neither an empty
        // component nor an undo entry. The standard lifecycle drain owns initialization.
        auto* script = dynamic_cast<ScriptComponent*>(object->AddComponentAllowMultiple(*type));
        if (!script) return InternalError("script.attach_failed", "Cannot create the script component");
        script->m_scriptType = typeName;
        object->GetScene()->DrainPendingLifecycle();
        if (!script->HasInstance())
        {
            object->RemoveComponent(script);
            return Fail("script.attach_failed", "Cannot create the script instance; see Output Log");
        }
        script->CaptureFields();
        auto snapshot = std::make_shared<Authoring::WriteDocument>(Meta::SerializeDocument(script, *type));
        const auto id = "#" + std::to_string(script->GetInstanceID());
        const auto instanceId = script->GetInstanceId();
        Meta::EntityReference reference(object);
        auto firstExecution = std::make_shared<bool>(true);
        Meta::MakeCustomChangeCommand([=] {
            if (auto* owner = reference.Resolve()) if (auto* component = FindComponent(owner, id))
            {
                if (auto* managed = dynamic_cast<ScriptComponent*>(component)) managed->CaptureFields();
                *snapshot = Meta::SerializeDocument(component, *type);
                owner->RemoveComponent(component);
            }
        }, [=] {
            if (std::exchange(*firstExecution, false)) return;
            if (auto* owner = reference.Resolve())
            {
                const auto node = snapshot->Root().Read();
                ComponentFactorys->LoadComponent(owner, Authoring::NodeViewAccess::Make(node), false);
                owner->GetScene()->DrainPendingLifecycle();
            }
        });
        auto data = CommandData::Object();
        data.Set("object", CommandData::String(object->m_name.ToString()));
        data.Set("type", CommandData::String(typeName));
        data.Set("component", CommandData::String(id));
        data.Set("instanceId", CommandData::Int(instanceId));
        return Ok("Script attached", std::move(data));
    }

    CommandCore::CommandResult RemoveComponent(EntityHandle target, const std::string& name)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        if (!object) return PreconditionFailed("object.stale", "Object no longer exists");
        if (IsEditLocked(object, true)) return PreconditionFailed("object.locked", "Unlock the entity or its hierarchy before editing");
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
        if (IsEditLocked(object, true)) return CommandCore::PreconditionFailed("object.locked", "Unlock the entity hierarchy before editing");
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

    // PBR-W8 — 렌더러별 MaterialInstance override 를 헤드리스로 얹는다.
    //
    // ★ 이 명령이 없던 동안 `SetPropertyOverride` 에 닿는 표면은 GUI 인스펙터와 C#
    //   스크립트뿐이었다. 게이트는 `--commandlet-script` 로만 움직이므로 "같은
    //   `Material*` 을 공유하는 렌더러 둘이 서로 다른 override 를 갖는" 상태를 만들
    //   수 없었고, 그래서 W8 의 핵심 수정(밀봉 키를 주소에서 값으로)을 자극하는
    //   fixture 가 저장소에 없었다. 그 구멍을 메우는 자리다.
    CommandCore::CommandResult MaterialOverride(EntityHandle target,
        int rendererIndex, const std::string& property,
        const std::vector<float>& values)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        if (!object) return PreconditionFailed("object.stale", "Object no longer exists");
        if (IsEditLocked(object, true)) return PreconditionFailed("object.locked", "Unlock the entity hierarchy before editing");
        if (rendererIndex < 0) return InvalidArguments("Renderer index must be >= 0");
        if (property.empty()) return InvalidArguments("Property name is required");
        if (values.size() != 1 && values.size() != 4)
            return InvalidArguments("Value must be one scalar or four baseColor components");

        // MaterialMode(EntityHandle) 와 **같은** 깊이 우선 순회다. 색인 규약이 두
        // 곳에서 따로 정해지면 같은 인자가 다른 렌더러를 가리키게 된다.
        std::vector<MeshRenderer*> renderers;
        std::function<void(Entity*)> collect = [&](Entity* node) {
            if (!node || node->IsDestroyMark()) return;
            for (const auto& component : node->m_components)
                if (auto* renderer = dynamic_cast<MeshRenderer*>(component.get());
                    renderer && !renderer->IsDestroyMark())
                    renderers.push_back(renderer);
            for (auto index : node->GetChildrenIndices()) collect(node->OwnerSceneFindIndex(index));
        };
        collect(object);
        if (static_cast<std::size_t>(rendererIndex) >= renderers.size())
            return InvalidArguments("Renderer index " + std::to_string(rendererIndex)
                + " is out of range (" + std::to_string(renderers.size()) + " mesh renderers)");

        MeshRenderer* renderer = renderers[static_cast<std::size_t>(rendererIndex)];
        if (!renderer->m_Material)
            return PreconditionFailed("material.missing", "Mesh renderer has no material");
        experiment::MaterialInstance* instance = renderer->GetMaterialInstance();
        if (nullptr == instance)
            return PreconditionFailed("material.instance.missing",
                "Mesh renderer has no authored base — override는 저작 정본이 있어야 얹힌다");

        if (1 == values.size())
        {
            if (!MaterialScriptBinding::SetFloat(*renderer->m_Material, property,
                values[0], instance))
            {
                return InvalidArguments("ShaderMeta에 없는 property이거나 스칼라가 아니다: "
                    + property);
            }
        }
        else if (property != std::string(standard_material::property::BaseColor))
        {
            return InvalidArguments("Four components are only accepted for "
                + std::string(standard_material::property::BaseColor));
        }
        else
        {
            MaterialScriptBinding::SetBaseColor(*renderer->m_Material,
                math::color{ values[0], values[1], values[2], values[3] }, instance);
        }
        renderer->PublishRenderProxyDirty(ProxyDirty::Material);

        using D = CommandCore::CommandData;
        D data = D::Object();
        data.Set("object", D::String(ObjectId(target)));
        data.Set("renderer", D::Int(rendererIndex));
        data.Set("renderers", D::Int(static_cast<int>(renderers.size())));
        data.Set("property", D::String(property));
        D applied = D::Array();
        for (float value : values) applied.Append(D::Double(value));
        data.Set("values", std::move(applied));
        data.Set("revision", D::Int(static_cast<std::int64_t>(instance->Revision())));

        // ★ 캡처 manifest 의 draw 와 **같은 출처**로 적는다. 캡처는
        //   `draw.modelMeshView.handle` 을 쓰고 그 핸들은 BuildRHIModelMeshView 가
        //   `{generation.Identity().modelId, mesh.meshId, ...}` 로 짓는다 —
        //   GetModelMeshHandle() 이 같은 세 값을 같은 순서로 낸다. 재질 이름 같은
        //   사람 눈의 표지를 쓰면 게이트의 조인이 조용히 어긋난다.
        const assets::ModelMeshHandle meshHandle = renderer->GetModelMeshHandle();
        if (meshHandle.IsValid())
        {
            data.Set("modelId", D::String(FileGuid(meshHandle.modelId).ToString()));
            data.Set("meshId", D::String(FileGuid(meshHandle.meshId).ToString()));
        }

        // ★ fixture 의 전제("주소가 실제로 공유됐다")를 값으로 돌려준다. 모델
        //   인스턴스화가 언젠가 사본을 주도록 바뀌면 이 수가 1 이 되고, 게이트는
        //   조용히 아무것도 재지 않는 대신 그 자리에서 붉어진다.
        int sharedWith = 0;
        for (const MeshRenderer* other : renderers)
            if (other->m_Material.get() == renderer->m_Material.get()) ++sharedWith;
        data.Set("sharedMaterialRenderers", D::Int(sharedWith));
        data.Set("changed", D::Bool(true));
        return Ok("material override applied", std::move(data));
    }

    CommandCore::CommandResult AnimatorParameter(EntityHandle target, const std::string& name, ValueType type)
    {
        using namespace CommandCore;
        auto* object = Resolve(target);
        auto* animator = object ? object->GetComponent<Animator>() : nullptr;
        if (!animator) return PreconditionFailed("animator.not_found", "Animator is unavailable");
        if (IsEditLocked(object, true)) return PreconditionFailed("object.locked", "Unlock the entity before editing");
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
