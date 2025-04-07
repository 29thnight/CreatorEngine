#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "IObject.h"
#include "TypeTrait.h"
#include "Component.h"
#include "Transform.h"
#include "../Utility_Framework/HashingString.h"
#include "HotLoadSystem.h"
#include <ranges>

class Scene;
class Bone;
class RenderScene;
class ModelLoader;
class ModuleBehavior;
class LightComponent;
class GameObject : public IObject
{
public:
	using Index = int;

	enum class Type
	{
		Empty,
		Camera,
		Light,
		Mesh,
		Bone,
		TypeMax
	};

	GameObject(const std::string_view& name, GameObject::Type type, GameObject::Index index, GameObject::Index parentIndex);
	GameObject(GameObject&) = delete;
	GameObject(GameObject&&) noexcept = default;
	GameObject& operator=(GameObject&) = delete;

	std::string ToString() const override;
	HashingString GetHashedName() const { return m_name; }
	unsigned int GetInstanceID() const override { return m_instanceID; }

	template<typename T>
	T* AddComponent()
	{
		if (std::ranges::find_if(m_components, [&](Component* component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<T>(); }) != m_components.end())
		{
			return nullptr;
		}

		T* component = new T();
        if(typeid(T) == typeid(LightComponent) && m_pScene)
        {
            LightComponent* lightComponent = static_cast<LightComponent*>(component);
            lightComponent->m_lightIndex = m_pScene->AddLightCount();
        }

		m_components.push_back(component);
		component->SetOwner(this);
		m_componentIds[component->GetTypeID()] = m_components.size();

		ComponentsSort();

		return component;
	}

	template<typename T>
	T* AddScriptComponent(const std::string_view& scriptName)
	{
		if (std::ranges::find_if(m_components, [&](Component* component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<T>(); }) != m_components.end())
		{
			return nullptr;
		}

		T* component = ScriptManager->CreateMonoBehavior(scriptName.data());
		m_components.push_back(component);
		component->SetOwner(this);
		m_componentIds[component->GetTypeID()] = m_components.size();

		ComponentsSort();

		size_t index = m_componentIds[component->GetTypeID()];

		ScriptManager->CollectScriptComponent(this, index, scriptName.data());

		return component;
	}

	template<typename T, typename... Args>
	T* AddComponent(Args&&... args)
	{
		if (std::ranges::find_if(m_components, [&](Component* component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<T>(); }) != m_components.end())
		{
			return nullptr;
		}

		T* component = new T(std::forward<Args>(args)...);
		m_components.push_back(component);
		component->SetOwner(this);
		m_componentIds[component->GetTypeID()] = m_components.size();

		ComponentsSort();

		return component;
	}

	template<typename T>
	T* GetComponent(uint32 id)
	{
		auto it = m_componentIds.find(id);
		if (it == m_componentIds.end())
			return nullptr;
		return static_cast<T*>(&m_components[it->second]);
	}

	template<typename T>
	T* GetComponent()
	{
		for (auto& component : m_components)
		{
			if (T* castedComponent = dynamic_cast<T*>(component))
				return castedComponent;
		}
		return nullptr;
	}

	template<typename T>
	std::vector<T*> GetComponents() {
		std::vector<T*> comps;
		for (auto& component : m_components)
		{
			if (T* castedComponent = dynamic_cast<T*>(component))
				comps.push_back(castedComponent);
		}
		return comps;
	}

	template<typename T>
	void RemoveComponent(T* component)
	{
		component->SetDestroyMark();
	}

	GameObject::Type GetType() const { return m_gameObjectType; }
	void SetScene(Scene* pScene) { m_pScene = pScene; }

	Transform m_transform{};
	GameObject::Index m_index;
	GameObject::Index m_parentIndex;
	//for bone update
	GameObject::Index m_rootIndex{ 0 };
	std::vector<GameObject::Index> m_childrenIndices;

private:
	friend class RenderScene;
	friend class ModelLoader;
	friend class HotLoadSystem;
    friend class LightComponent;

	void ComponentsSort()
	{
		std::ranges::sort(m_components, [&](Component* a, Component* b)
		{
			return a->GetOrderID() < b->GetOrderID();
		});

		std::ranges::for_each(std::views::iota(0, static_cast<int>(m_components.size())), [&](int i)
		{
			m_componentIds[m_components[i]->GetTypeID()] = i;
		});
	}

private:
	GameObject::Type m_gameObjectType{ GameObject::Type::Empty };
	
	const size_t m_typeID{ TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
	const size_t m_instanceID{ TypeTrait::GUIDCreator::GetGUID() };
	
	HashingString m_name{};
	HashingString m_tag{};
	static Scene* m_pScene;
	
	std::unordered_map<uint32_t, size_t> m_componentIds{};
	std::vector<Component*> m_components{};

	//debug layer
	Bone* selectedBone{ nullptr };
};

