#pragma once
#include "MetaStateCommand.h"
#include <functional>
#include "Scene.h"
#include "GameObject.h"

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
            }
        }

        void Undo() override
        {
            auto obj = m_scene->CreateGameObject(m_name, m_type, m_parentIndex);
            m_index = obj ? obj->m_index : GameObject::INVALID_INDEX;
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
    };
}
