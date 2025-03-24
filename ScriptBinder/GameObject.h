#pragma once
#include "Core.Minimal.h"
#include "IObject.h"
#include "TypeTrait.h"
#include "Component.h"
#include "Transform.h"

class Scene;
class Bone;
class GameObject : public IObject
{
public:
	using Index = int;
	GameObject(const std::string_view& name, GameObject::Index index, GameObject::Index parentIndex);
	GameObject(GameObject&) = delete;
	GameObject(GameObject&&) noexcept = default;
	GameObject& operator=(GameObject&) = delete;

	virtual void Initialize() {};
	virtual void FixedUpdate(float fixedTick) {};
	virtual void Update(float tick) {};
	virtual void LateUpdate(float tick) {};
	std::string ToString() const override;

	void ShowBoneHierarchy(Bone* bone);
	void RenderBoneEditor();
	void EditorMeshRenderer();

	template<typename T>
	T* AddComponent()
	{
		T* component = new T();
		m_components.push_back(component);
		component->SetOwner(this);
		m_componentIds[component->GetId()] = m_components.size();

		std::ranges::sort(m_components, [&](Component* a, Component* b)
		{
			return a->ID() < b->ID();
		});

		foreach(iota(0, static_cast<int>(m_components.size())), [&](int i)
		{
			m_componentIds[m_components[i]->GetId()] = i;
		});

		component->Initialize();

		return component;
	}

	template<typename T, typename... Args>
	T* AddComponent(Args&&... args)
	{
		T* component = new T(std::forward<Args>(args)...);
		m_components.push_back(component);
		component->SetOwner(this);
		m_componentIds[component->ID()] = m_components.size();
		std::ranges::sort(m_components, [&](Component* a, Component* b)
		{
			return a->ID() < b->ID();
		});

		foreach(iota(0, static_cast<int>(m_components.size())), [&](int i)
		{
			m_componentIds[m_components[i]->ID()] = i;
		});

		component->Initialize();

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

private:
	const size_t m_typeID{ GENERATE_CLASS_GUID };
	const size_t m_instanceID{ GENERATE_GUID };
	std::string m_name{};
	Scene* m_pScene{};
	Transform m_transform{};
	std::unordered_map<std::type_index, size_t> m_componentIds{};
	std::vector<Component*> m_components{};

	//debug layer
	Bone* selectedBone{ nullptr };
};

