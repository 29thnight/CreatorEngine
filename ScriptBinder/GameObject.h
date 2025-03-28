#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "IObject.h"
#include "TypeTrait.h"
#include "Component.h"
#include "Transform.h"
#include "../Utility_Framework/HashingString.h"
#include <ranges>

class Scene;
class Bone;
class RenderScene;
class ModelLoader;
class GameObject : public IObject
{
public:
	using Index = int;
	GameObject(const std::string_view& name, GameObject::Index index, GameObject::Index parentIndex);
	GameObject(GameObject&) = delete;
	GameObject(GameObject&&) noexcept = default;
	GameObject& operator=(GameObject&) = delete;

	std::string ToString() const override;
	unsigned int GetInstanceID() const override { return m_instanceID; }

	void ShowBoneHierarchy(Bone* bone);
	void RenderBoneEditor();
	void EditorMeshRenderer();

	template<typename T>
	T* AddComponent()
	{
		T* component = new T();
		m_components.push_back(component);
		component->SetOwner(this);
		m_componentIds[component->GetTypeID()] = m_components.size();

		std::ranges::sort(m_components, [&](Component* a, Component* b)
		{
			return a->GetOrderID() < b->GetOrderID();
		});

		std::ranges::for_each(std::views::iota(0, static_cast<int>(m_components.size())), [&](int i)
		{
			m_componentIds[m_components[i]->GetTypeID()] = i;
		});

		return component;
	}

	template<typename T, typename... Args>
	T* AddComponent(Args&&... args)
	{
		T* component = new T(std::forward<Args>(args)...);
		m_components.push_back(component);
		component->SetOwner(this);
		m_componentIds[component->GetTypeID()] = m_components.size();
		std::ranges::sort(m_components, [&](Component* a, Component* b)
		{
			return a->GetOrderID() < b->GetOrderID();
		});

		std::ranges::for_each(std::views::iota(0, static_cast<int>(m_components.size())), [&](int i)
		{
			m_componentIds[m_components[i]->GetTypeID()] = i;
		});

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

	Transform m_transform{};
	const Index m_index;
	const Index m_parentIndex;
	std::vector<GameObject::Index> m_childrenIndices;

private:
	friend class RenderScene;
	friend class ModelLoader;
	const size_t m_typeID{ GENERATE_CLASS_GUID };
	const size_t m_instanceID{ GENERATE_GUID };
	HashingString m_name{};
	HashingString m_tag{};
	Scene* m_pScene{};
	std::unordered_map<uint32_t, size_t> m_componentIds{};
	std::vector<Component*> m_components{};



	//debug layer
	Bone* selectedBone{ nullptr };
};

