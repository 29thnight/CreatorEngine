#pragma once
#include "MetaStateCommand.h"
#include <functional>
#include "Scene.h"
#include "Model.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "ReflectionYml.h"
#include "ComponentFactory.h"

namespace Meta
{
    class CreateGameObjectCommand : public IUndoableCommand
    {
    public:
        CreateGameObjectCommand(Scene* scene,
            std::string name,
            GameObjectType type,
            GameObject::Index parentIndex = 0)
            : m_scene(scene), m_name(std::move(name)), m_type(type),
            m_parentIndex(parentIndex), m_index(GameObject::INVALID_INDEX) {
        }

        void Undo() override
        {
            if (GameObject::IsValidIndex(m_index))
            {
                m_scene->DestroyGameObject(m_index);
            }
        }

        void Redo() override
        {
            auto obj = m_scene->CreateGameObject(m_name, m_type, m_parentIndex);
            m_index = obj ? obj->m_index : GameObject::INVALID_INDEX;
        }

    private:
        Scene* m_scene;
        std::string m_name;
        GameObjectType m_type{ GameObjectType::Empty };
        GameObject::Index m_parentIndex{ 0 };
        GameObject::Index m_index;
    };

    class DeleteGameObjectCommand : public IUndoableCommand
    {
    public:
        DeleteGameObjectCommand(Scene* scene, GameObject::Index index)
            : m_scene(scene), m_index(index)
        {
            auto obj = m_scene->GetGameObject(index);
            if (obj)
            {
                m_name = obj->m_name.ToString();
                m_type = obj->GetType();
                m_parentIndex = obj->m_parentIndex;
                m_serializedNode = Meta::Serialize(obj.get());
            }
        }

        void Undo() override
        {
            auto objPtr = m_scene->CreateGameObject(m_name, m_type, m_parentIndex);
            if (objPtr)
            {
                Meta::Deserialize(objPtr.get(), m_serializedNode);
                if (m_serializedNode["m_components"])
                {
                    for (const auto& componentNode : m_serializedNode["m_components"])
                    {
                        try
                        {
                            ComponentFactorys->LoadComponent(objPtr.get(), componentNode);
                        }
                        catch (const std::exception& e)
                        {
                            Debug->LogError(e.what());
                            continue;
                        }
                    }
                }
            }
            m_index = objPtr ? objPtr->m_index : GameObject::INVALID_INDEX;
        }

        void Redo() override
        {
            if (GameObject::IsValidIndex(m_index))
            {
                m_scene->DestroyGameObject(m_index);
            }
        }

    private:
        Scene* m_scene;
        std::string m_name{};
        GameObjectType m_type{ GameObjectType::Empty };
        GameObject::Index m_parentIndex{ 0 };
        GameObject::Index m_index{ GameObject::INVALID_INDEX };
        MetaYml::Node m_serializedNode{};
    };

    class DuplicateGameObjectCommand : public IUndoableCommand
    {
    public:
        DuplicateGameObjectCommand(Scene* scene, GameObject::Index originalIndex)
            : m_scene(scene), m_originalIndex(originalIndex)
        {
        }

        void Undo() override
        {
            if (GameObject::IsValidIndex(m_createdIndex))
            {
                m_scene->DestroyGameObject(m_createdIndex);
            }
        }

        void Redo() override
        {
            auto original = m_scene->GetGameObject(m_originalIndex);
            if (!original)
                return;

            auto* cloned = dynamic_cast<GameObject*>(Object::Instantiate(original.get(), original->m_name.ToString()));
            if (!cloned)
                return;

            GameObject::Index parentIndex = cloned->m_parentIndex;
            auto parentObj = m_scene->GetGameObject(parentIndex);
            if (parentObj && parentIndex != cloned->m_parentIndex)
            {
                auto& rootChildren = m_scene->m_SceneObjects[0]->m_childrenIndices;
                rootChildren.erase(std::remove(rootChildren.begin(), rootChildren.end(), cloned->m_index), rootChildren.end());

                cloned->m_parentIndex = parentIndex;
                cloned->m_transform.SetParentID(parentIndex);
                parentObj->m_childrenIndices.push_back(cloned->m_index);
            }

            m_scene->AddSelectedSceneObject(cloned);
            m_createdIndex = cloned->m_index;
        }

    private:
        Scene* m_scene{};
        GameObject::Index m_originalIndex{ GameObject::INVALID_INDEX };
        GameObject::Index m_createdIndex{ GameObject::INVALID_INDEX };
    public:
        [[nodiscard]] GameObject::Index GetCreatedIndex() const { return m_createdIndex; }
    };

    class DuplicateGameObjectsCommand : public IUndoableCommand
    {
    public:
        DuplicateGameObjectsCommand(Scene* scene, std::span<GameObject* const> originals)
            : m_scene(scene)
        {
            std::unordered_set<GameObject::Index> selectedIndices{};
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

                GameObject::Index parentIndex = obj->m_parentIndex;
                bool skip = false;

                while (GameObject::IsValidIndex(parentIndex))
                {
                    if (selectedIndices.contains(parentIndex))
                    {
                        skip = true;
                        break;
                    }

                    auto parentObj = m_scene->GetGameObject(parentIndex);
                    if (!parentObj)
                        break;
                    parentIndex = parentObj->m_parentIndex;
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
        LoadModelToSceneObjCommand(Scene* scene, Model* model, GameObject** outObj = nullptr)
            : m_scene(scene), m_model(model), m_outObj(outObj) {
        }

        void Undo() override
        {
            resetSelectedObjectEvent.Broadcast();
            if (GameObject::IsValidIndex(m_rootIndex))
            {
                m_scene->DestroyGameObject(m_rootIndex);
            }
        }

        void Redo() override
        {
            GameObject* obj = Model::LoadModelToSceneObj(m_model, *m_scene);
            m_rootIndex = obj ? obj->m_index : GameObject::INVALID_INDEX;
            if (m_outObj)
                *m_outObj = obj;
        }

    private:
        Scene* m_scene{};
        Model* m_model{};
        GameObject::Index m_rootIndex{ GameObject::INVALID_INDEX };
        GameObject** m_outObj{};
    };
}
