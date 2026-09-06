#pragma once
#include "MetaStateCommand.h"
#include <functional>
#include <stdexcept>
#include "Scene.h"
#include "ModelSceneInstantiation.h" // MBC9: generation 인스턴스화
#include "DataSystem.h"
#include "Assets/ModelAssetGeneration.h"
#include "SceneManager.h"
#include "Entity.h"
#include "EntityAuthoringRead.h" // D3-a-2: 저작 읽기 어댑터
#include "AuthoringNodeViewAccess.h" // D3-a-5
#include "ReflectionYml.h"
#include "ComponentFactory.h"
#include "PrefabUtility.h"
#include "TagManager.h"

namespace Meta
{
    struct EditorObjectIdentity
    {
        static void Restore(Object& object, HashedGuid id)
        {
            TypeTrait::GUIDCreator::EraseGUID(object.m_instanceID);
            object.m_instanceID = id;
            TypeTrait::GUIDCreator::InsertGUID(id);
        }
    };

    // Undo addresses logical identity; public HTTP handles still reject stale slot generations.
    struct EntityReference
    {
        uint32_t sceneId{};
        HashedGuid guid{};
        EntityReference() = default;
        explicit EntityReference(Entity* object)
        {
            if (object && object->GetScene()) { sceneId = object->GetScene()->GetSceneId(); guid = HashedGuid(object->GetInstanceID()); }
        }
        Scene* ScenePtr() const
        {
            for (Scene* scene : SceneManagers->GetScenes())
                if (scene && scene->GetSceneId() == sceneId) return scene;
            return nullptr;
        }
        Entity* Resolve() const
        {
            auto* scene = ScenePtr();
            if (scene) for (const auto& object : scene->m_Entities)
                if (object && !object->IsDestroyMark() && object->GetInstanceID() == guid.m_ID_Data) return object.get();
            return nullptr;
        }
    };

    struct SelectionSnapshot
    {
        uint32_t sceneId{};
        std::vector<EntityReference> selected;
        EntityReference primary;
        void Capture(Scene* scene)
        {
            sceneId = scene->GetSceneId(); selected.clear();
            for (auto* object : scene->m_selectedEntities) if (object) selected.emplace_back(object);
            primary = EntityReference(scene->m_selectedEntity);
        }
        void Apply() const
        {
            for (auto* scene : SceneManagers->GetScenes()) if (scene && scene->GetSceneId() == sceneId)
            {
                resetSelectedObjectEvent.Broadcast();
                scene->ClearSelectedEntities();
                for (const auto& reference : selected) if (auto* object = reference.Resolve()) scene->AddSelectedEntity(object);
                scene->m_selectedEntity = primary.Resolve();
            }
        }
    };

    inline void PrepareObjectRemoval(Entity* root)
    {
        Scene* scene = root->GetScene();
        const auto removed = [root, scene](Entity* object) {
            for (; object; object = scene->TryGetEntity(object->GetParentIndex()))
            { if (object == root) return true; if (!object->m_index) break; }
            return false;
        };
        SelectionSnapshot selection; selection.Capture(scene);
        bool changed = removed(scene->m_selectedEntity);
        if (changed) selection.primary = {};
        std::erase_if(selection.selected, [&](const EntityReference& reference) {
            const bool erase = removed(reference.Resolve()); changed = changed || erase; return erase;
        });
        if (changed) selection.Apply();
    }

    // Snapshot the entire owned subtree, including external parent/root references.
    // Restoration uses the same authoring component loader as scene/object cloning.
    class EntityArchive
    {
        struct Record
        {
            EntityReference object, parent, root;
            Authoring::WriteDocument document;
            std::string name;
        };
        std::vector<Record> m_records;
    public:
        void Capture(Entity* object)
        {
            m_records.clear();
            if (!object) return;
            Scene* scene = object->GetScene();
            std::vector<Entity*> nodes{object};
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                Entity* node = nodes[i];
                Record record;
                record.object = EntityReference(node);
                record.parent = EntityReference(scene->TryGetEntity(node->GetParentIndex()));
                record.root = EntityReference(scene->TryGetEntity(node->GetRootIndex()));
                record.document = Meta::SerializeDocument(node);
                record.name = node->m_name.ToString();
                m_records.push_back(std::move(record));
                for (auto index : node->GetChildrenIndices())
                    if (auto* child = scene->TryGetEntity(index)) nodes.push_back(child);
            }
        }
        Entity* Restore()
        {
            if (m_records.empty()) return nullptr;
            Scene* scene = m_records.front().object.ScenePtr();
            if (!scene) return nullptr;
            auto transaction = scene->BeginHierarchyBulkBuild();
            for (auto& record : m_records)
            {
                for (const auto& existing : scene->m_Entities)
                    if (existing && existing->GetInstanceID() == record.object.guid.m_ID_Data)
                        throw std::runtime_error("Undo identity is still present; wait for frame-end deletion");
                const auto node = record.document.Root().Read();
                Entity* restored = scene->CreateEntity(record.name, EntityAuthoring::InferCreationType(node));
                if (!restored) throw std::runtime_error("Cannot restore entity");
                const auto slot = restored->m_index;
                TypeTrait::GUIDCreator::EraseGUID(HashedGuid(restored->GetInstanceID()));
                Meta::Deserialize(restored, node);
                EditorObjectIdentity::Restore(*restored, record.object.guid);
                restored->m_index = slot;
                restored->m_name.SetString(record.name);
                if (!restored->m_tag.ToString().empty()) TagManager::GetInstance()->AddTagToObject(restored->m_tag.ToString(), restored);
                if (!restored->m_layer.ToString().empty()) TagManager::GetInstance()->AddObjectToLayer(restored->m_layer.ToString(), restored);
                if (restored->m_prefabFileGuid != FileGuid{})
                    if (auto* prefab = PrefabUtilitys->LoadPrefabGuid(restored->m_prefabFileGuid)) PrefabUtilitys->RegisterInstance(restored, prefab);
                if (const auto components = node["m_components"])
                    for (const auto component : components)
                        ComponentFactorys->LoadComponent(restored, Authoring::NodeViewAccess::Make(component), false);
            }
            for (auto& record : m_records)
            {
                Entity* object = record.object.Resolve();
                Entity* parent = record.parent.Resolve();
                scene->Reparent(scene->HandleOf(object->m_index), scene->HandleOf(parent ? parent->m_index : 0));
                Entity* root = record.root.Resolve();
                object->SetRootIndex(root ? root->m_index : Entity::INVALID_INDEX);
            }
            return m_records.front().object.Resolve();
        }
    };

    class CreateEntityCommand : public IUndoableCommand
    {
    public:
        CreateEntityCommand(Scene* scene, std::string name, GameObjectType type, Entity::Index parentIndex = 0)
            : m_sceneId(scene->GetSceneId()), m_parent(scene->TryGetEntity(parentIndex)), m_name(std::move(name)), m_type(type) {}
        void Undo() override
        {
            if (auto* object = m_created.Resolve())
            {
                m_archive.Capture(object);
                m_selection.Capture(object->GetScene());
                PrepareObjectRemoval(object);
                object->GetScene()->DestroyEntity(object);
            }
        }
        void Redo() override
        {
            if (m_created.sceneId)
            {
                if (auto* object = m_archive.Restore()) { m_index = object->m_index; m_selection.Apply(); }
                return;
            }
            Scene* scene = nullptr;
            for (auto* candidate : SceneManagers->GetScenes())
                if (candidate && candidate->GetSceneId() == m_sceneId) scene = candidate;
            if (!scene) return;
            auto* parent = m_parent.Resolve();
            Entity* object = scene->CreateEntity(m_name, m_type, parent ? parent->m_index : 0);
            if (!object) throw std::runtime_error("Cannot create entity");
            const char* componentName = m_type == GameObjectType::Light ? "LightComponent"
                : m_type == GameObjectType::Camera ? "CameraComponent"
                : m_type == GameObjectType::Mesh ? "MeshRenderer"
                : m_type == GameObjectType::Canvas ? "Canvas" : nullptr;
            if (componentName)
            {
                const auto found = ComponentFactorys->m_componentTypes.find(componentName);
                if (found == ComponentFactorys->m_componentTypes.end() || !found->second) throw std::runtime_error("Unregistered object archetype component");
                auto* component = object->AddComponent(*found->second);
                if (!component) throw std::runtime_error("Cannot create object archetype component");
                if (auto* initializable = dynamic_cast<System::IInitializable*>(component)) initializable->Initialize();
            }
            m_created = EntityReference(object);
            m_name = object->m_name.ToString();
            m_index = object->m_index;
        }
        Entity::Index GetCreatedIndex() const noexcept { return m_index; }
    private:
        uint32_t m_sceneId{};
        EntityReference m_parent, m_created;
        EntityArchive m_archive;
        SelectionSnapshot m_selection;
        std::string m_name;
        GameObjectType m_type;
        Entity::Index m_index{Entity::INVALID_INDEX};
    };

    class DeleteGameObjectCommand : public IUndoableCommand
    {
    public:
        DeleteGameObjectCommand(Scene* scene, Entity::Index index) : m_target(scene->TryGetEntity(index))
        {}
        void Undo() override { m_archive.Restore(); m_selection.Apply(); }
        void Redo() override
        {
            if (auto* object = m_target.Resolve())
            {
                m_archive.Capture(object);
                m_selection.Capture(object->GetScene());
                PrepareObjectRemoval(object);
                object->GetScene()->DestroyEntity(object);
            }
        }
    private:
        EntityReference m_target;
        EntityArchive m_archive;
        SelectionSnapshot m_selection;
    };

    class DuplicateGameObjectCommand : public IUndoableCommand
    {
    public:
        DuplicateGameObjectCommand(Scene* scene, Entity::Index originalIndex, std::string name = {})
            : m_source(scene->TryGetEntity(originalIndex)), m_name(std::move(name)) {}
        void Undo() override { if (m_delete) m_delete->Redo(); }
        void Redo() override
        {
            if (m_delete)
            {
                m_delete->Undo();
                if (auto* object = m_created.Resolve()) m_createdIndex = object->m_index;
                return;
            }
            Entity* original = m_source.Resolve();
            if (!original) return;
            Scene* scene = original->GetScene();
            auto* cloned = dynamic_cast<Entity*>(Object::Instantiate(original, m_name.empty() ? original->m_name.ToString() : m_name));
            if (!cloned) throw std::runtime_error("Cannot duplicate entity");
            if (!m_name.empty()) cloned->m_name.SetString(m_name);
            scene->Reparent(scene->HandleOf(cloned->m_index), scene->HandleOf(original->GetParentIndex()));
            m_created = EntityReference(cloned);
            m_createdIndex = cloned->m_index;
            m_delete = std::make_unique<DeleteGameObjectCommand>(scene, cloned->m_index);
            scene->AddSelectedEntity(cloned);
        }
        Entity::Index GetCreatedIndex() const noexcept { return m_createdIndex; }
    private:
        EntityReference m_source, m_created;
        std::string m_name;
        std::unique_ptr<DeleteGameObjectCommand> m_delete;
        Entity::Index m_createdIndex{Entity::INVALID_INDEX};
    };

    class DuplicateGameObjectsCommand : public IUndoableCommand
    {
    public:
        DuplicateGameObjectsCommand(Scene* scene, std::span<Entity* const> originals)
            : m_scene(scene)
        {
            std::unordered_set<Entity::Index> selectedIndices{};
            selectedIndices.reserve(originals.size());

            for (auto* obj : originals)
            {
                if (obj)
                    selectedIndices.insert(obj->m_index);
            }

            for (auto* obj : originals)
            {
                if (!obj)
                    continue;

                Entity::Index parentIndex = obj->GetParentIndex();
                bool skip = false;

                while (Entity::IsValidIndex(parentIndex))
                {
                    if (selectedIndices.contains(parentIndex))
                    {
                        skip = true;
                        break;
                    }

                    auto parentObj = m_scene->GetEntity(parentIndex);
                    if (!parentObj)
                        break;
                    parentIndex = parentObj->GetParentIndex();
                }

                if (skip)
                    continue;

                m_commands.emplace_back(scene, obj->m_index);
            }
        }

        void Undo() override
        {
            resetSelectedObjectEvent.Broadcast();
            for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
                it->Undo();
        }

        void Redo() override
        {
            resetSelectedObjectEvent.Broadcast();
            for (auto& cmd : m_commands)
                cmd.Redo();
        }

    private:
        Scene* m_scene{};
        std::vector<DuplicateGameObjectCommand> m_commands{};
    };

    class LoadModelToSceneObjCommand : public IUndoableCommand
    {
    public:
        LoadModelToSceneObjCommand(Scene* scene,
            std::shared_ptr<const assets::ModelAssetGeneration> generation,
            Entity** outObj = nullptr)
            : m_sceneId(scene->GetSceneId()), m_generation(std::move(generation)), m_outObj(outObj) {
        }

        void Undo() override
        {
            resetSelectedObjectEvent.Broadcast();
            if (m_delete) m_delete->Redo();
        }
        void Redo() override
        {
            if (m_delete) { m_delete->Undo(); return; }
            Scene* scene = nullptr;
            for (auto* candidate : SceneManagers->GetScenes()) if (candidate && candidate->GetSceneId() == m_sceneId) scene = candidate;
            if (!scene || !m_generation) return;
            ModelSceneInstantiation::Options options{};
            options.createMeshCollider = DataSystems->ReadModelCreateMeshCollider(FileGuid(m_generation->Identity().modelId));
            auto* object = ModelSceneInstantiation::Instantiate(*scene, m_generation, options);
            if (!object) throw std::runtime_error("Cannot instantiate model");
            m_delete = std::make_unique<DeleteGameObjectCommand>(scene, object->m_index);
            if (m_outObj) { *m_outObj = object; m_outObj = nullptr; }
        }

    private:
        uint32_t m_sceneId{};
        std::unique_ptr<DeleteGameObjectCommand> m_delete;
        std::shared_ptr<const assets::ModelAssetGeneration> m_generation{};
        Entity** m_outObj{};
    };
}
