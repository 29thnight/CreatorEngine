#pragma once
#include "MetaStateCommand.h"
#include <functional>
#include "Scene.h"
#include "Model.h"
#include "SceneManager.h"
#include "Entity.h"
#include "EntityAuthoringRead.h" // D3-a-2: 저작 읽기 어댑터
#include "AuthoringNodeViewAccess.h" // D3-a-5
#include "ReflectionYml.h"
#include "ComponentFactory.h"

namespace Meta
{
    class CreateEntityCommand : public IUndoableCommand
    {
    public:
        CreateEntityCommand(Scene* scene,
            std::string name,
            GameObjectType type,
            Entity::Index parentIndex = 0)
            : m_scene(scene), m_name(std::move(name)), m_type(type),
            m_parentIndex(parentIndex), m_index(Entity::INVALID_INDEX) {
        }

        void Undo() override
        {
            if (Entity::IsValidIndex(m_index))
            {
                m_scene->DestroyEntity(m_index);
            }
        }

        void Redo() override
        {
            auto obj = m_scene->CreateEntity(m_name, m_type, m_parentIndex);
            m_index = obj ? obj->m_index : Entity::INVALID_INDEX;
        }

    private:
        Scene* m_scene;
        std::string m_name;
        GameObjectType m_type{ GameObjectType::Empty };
        Entity::Index m_parentIndex{ 0 };
        Entity::Index m_index;
    };

    class DeleteGameObjectCommand : public IUndoableCommand
    {
    public:
        DeleteGameObjectCommand(Scene* scene, Entity::Index index)
            : m_scene(scene), m_index(index)
        {
            auto obj = m_scene->GetEntity(index);
            if (obj)
            {
                m_name = obj->m_name.ToString();
                m_parentIndex = obj->GetParentIndex();
				m_sourceRootIndex = obj->GetRootIndex();
				m_serializedNode = Meta::Serialize(obj);
				m_type = EntityAuthoring::InferCreationType(m_serializedNode);
            }
        }

        void Undo() override
        {
            auto objPtr = m_scene->CreateEntity(m_name, m_type, m_parentIndex);
            if (objPtr)
            {
				const Entity::Index restoredIndex = objPtr->m_index;
				Meta::Deserialize(objPtr, m_serializedNode);
				objPtr->m_index = restoredIndex;
				objPtr->SetRootIndex(m_sourceRootIndex);
                if (m_serializedNode["m_components"])
                {
                    for (const auto& componentNode : m_serializedNode["m_components"])
                    {
                        try
                        {
							ComponentFactorys->LoadComponent(objPtr, Authoring::NodeViewAccess::Make(componentNode));
                        }
                        catch (const std::exception& e)
                        {
                            Debug->LogError(e.what());
                            continue;
                        }
                    }
                }
            }
            m_index = objPtr ? objPtr->m_index : Entity::INVALID_INDEX;
        }

        void Redo() override
        {
            if (Entity::IsValidIndex(m_index))
            {
                m_scene->DestroyEntity(m_index);
            }
        }

    private:
        Scene* m_scene;
        std::string m_name{};
        GameObjectType m_type{ GameObjectType::Empty };
        Entity::Index m_parentIndex{ 0 };
		Entity::Index m_sourceRootIndex{ Entity::INVALID_INDEX };
        Entity::Index m_index{ Entity::INVALID_INDEX };
        MetaYml::Node m_serializedNode{};
    };

    class DuplicateGameObjectCommand : public IUndoableCommand
    {
    public:
        DuplicateGameObjectCommand(Scene* scene, Entity::Index originalIndex)
            : m_scene(scene), m_originalIndex(originalIndex)
        {
        }

        void Undo() override
        {
            if (Entity::IsValidIndex(m_createdIndex))
            {
                m_scene->DestroyEntity(m_createdIndex);
            }
        }

        void Redo() override
        {
            auto original = m_scene->GetEntity(m_originalIndex);
            if (!original)
                return;

			auto* cloned = dynamic_cast<Entity*>(Object::Instantiate(original, original->m_name.ToString()));
            if (!cloned)
                return;

            Entity::Index parentIndex = cloned->GetParentIndex();
            if (Entity::IsValidIndex(parentIndex) && parentIndex != 0)
            {
                auto parentObj = m_scene->GetEntity(parentIndex);
                if (parentObj)
                {
                    m_scene->m_Entities[0]->DetachChildIndex(cloned->m_index);
                    cloned->SetParentIndex(parentIndex);
                    parentObj->AttachChildIndex(cloned->m_index);
                }
            }

            m_scene->AddSelectedEntity(cloned);
            m_createdIndex = cloned->m_index;
        }

    private:
        Scene* m_scene{};
        Entity::Index m_originalIndex{ Entity::INVALID_INDEX };
        Entity::Index m_createdIndex{ Entity::INVALID_INDEX };
    public:
        [[nodiscard]] Entity::Index GetCreatedIndex() const { return m_createdIndex; }
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
        LoadModelToSceneObjCommand(Scene* scene, std::shared_ptr<Model> model,
            Entity** outObj = nullptr)
            : m_scene(scene), m_model(std::move(model)), m_outObj(outObj) {
        }

        void Undo() override
        {
            resetSelectedObjectEvent.Broadcast();
            if (Entity::IsValidIndex(m_rootIndex))
            {
                m_scene->DestroyEntity(m_rootIndex);
            }
        }

        void Redo() override
        {
            Entity* obj = (m_model && m_scene)
                ? Model::LoadModelToSceneObj(m_model.get(), *m_scene) : nullptr;
            m_rootIndex = obj ? obj->m_index : Entity::INVALID_INDEX;
            if (m_outObj)
                *m_outObj = obj;
        }

    private:
        Scene* m_scene{};
        std::shared_ptr<Model> m_model{};
        Entity::Index m_rootIndex{ Entity::INVALID_INDEX };
        Entity** m_outObj{};
    };
}
