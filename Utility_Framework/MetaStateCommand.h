#pragma once
#ifndef interface
#define interface struct
#endif

#include "Scene.h"
#include "GameObject.h"

namespace Meta
{
	interface IUndoableCommand
	{
		virtual ~IUndoableCommand() = default;
		virtual void Undo() = 0;
		virtual void Redo() = 0;
	};

	template<typename T>
	class PropertyChangeCommand : public IUndoableCommand
	{
	public:
		PropertyChangeCommand(void* target, const Meta::Property& prop, T oldValue, T newValue)
			: m_target(target), m_property(prop), m_oldValue(oldValue), m_newValue(newValue) {
		}
		~PropertyChangeCommand() override = default;

		void Undo() override { m_property.setter(m_target, m_oldValue); }
		void Redo() override { m_property.setter(m_target, m_newValue); }

	private:
		void* m_target;
		Meta::Property m_property;
		T m_oldValue;
		T m_newValue;
	};

        class CustomChangeCommand : public IUndoableCommand
        {
        public:
                CustomChangeCommand(std::function<void()> undoFunc, std::function<void()> redoFunc)
                        : m_undoFunc(std::move(undoFunc)), m_redoFunc(std::move(redoFunc)) {
                }

		void Undo() override { if (m_undoFunc) m_undoFunc(); }
		void Redo() override { if (m_redoFunc) m_redoFunc(); }

	private:
                std::function<void()> m_undoFunc;
                std::function<void()> m_redoFunc;
        };

        class CreateGameObjectCommand : public IUndoableCommand
        {
        public:
                CreateGameObjectCommand(Scene* scene,
                        std::string name,
                        GameObjectType type,
                        GameObject::Index parentIndex = 0)
                        : m_scene(scene), m_name(std::move(name)), m_type(type),
                        m_parentIndex(parentIndex), m_index(GameObject::INVALID_INDEX) {}

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

