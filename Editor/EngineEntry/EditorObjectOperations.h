#pragma once
#include "CommandCore/CommandResult.h"
#include "EntityHandle.h"
#include "AuthoringWriteNode.h"
#include <memory>
#include <string>
#include <vector>
#include <mathematics/vector3.hpp>
#include <mathematics/quaternion.hpp>
class Scene;
class Component;
class Prefab;
class Material;
enum class MaterialRenderingMode;
enum class ValueType : std::uint16_t;
class Animator;
enum class GameObjectType;

namespace EditorObjectOperations
{
    // Runtime identity: @scene:index:generation. Names are lookup input, not identity.
    std::string ObjectId(EntityHandle target);
    CommandCore::CommandResult ResolveTarget(const std::string& nameOrId, EntityHandle& target);
    CommandCore::CommandResult Describe(EntityHandle target);
    CommandCore::CommandResult Properties(EntityHandle target, const std::string& component);
    CommandCore::CommandResult Rename(EntityHandle target, const std::string& name);
    CommandCore::CommandResult Create(Scene* scene, const std::string& name, GameObjectType type, uint32_t parent = 0);
    CommandCore::CommandResult Delete(EntityHandle target);
    CommandCore::CommandResult Duplicate(EntityHandle target, const std::string& name = {});
    CommandCore::CommandResult Parent(EntityHandle target, EntityHandle parent);
    CommandCore::CommandResult Transform(EntityHandle target, math::vector3 position, math::quaternion rotation, math::vector3 scale);
    // A GUI drag keeps handles and owned values, never component pointers. One
    // commit groups all affected objects into a single Undo entry.
    struct PropertyEdit
    {
        EntityHandle target;
        std::string component;
        std::vector<std::string> fields;
        Authoring::WriteDocument before;
    };
    PropertyEdit CapturePropertyEdit(Component& component, std::vector<std::string> fields);
    bool CommitPropertyEdits(std::vector<PropertyEdit> edits);
    bool CommitProperty(Component& component, const std::string& field, Authoring::WriteDocument before);
    CommandCore::CommandResult Property(EntityHandle target, const std::string& component, const std::string& field, const std::string& value);
    CommandCore::CommandResult AddComponent(EntityHandle target, const std::string& type);
    CommandCore::CommandResult RemoveComponent(EntityHandle target, const std::string& component);
    CommandCore::CommandResult Select(Scene* scene, const std::vector<EntityHandle>& targets);
    CommandCore::CommandResult InstantiatePrefab(const std::string& prefab, const std::string& name);
    CommandCore::CommandResult InstantiatePrefab(Prefab* prefab, const std::string& name);
    CommandCore::CommandResult MaterialMode(const std::vector<std::shared_ptr<Material>>& materials, MaterialRenderingMode mode);
    CommandCore::CommandResult MaterialMode(EntityHandle target, MaterialRenderingMode mode);
    CommandCore::CommandResult AnimatorParameter(EntityHandle target, const std::string& name, ValueType type);
    CommandCore::CommandResult AnimatorDefaultParameter(Animator& animator, ValueType type);
    CommandCore::CommandResult UndoRedo(bool redo);
}
