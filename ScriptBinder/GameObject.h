#pragma once
#include "Core.Minimal.h"
#include "IObject.h"
#include "TypeTrait.h"
#include "Component.h"
#include "Transform.h"

class Scene;
class GameObject : public IObject
{
public:
	using Index = int;
	virtual void Initialize() {};
	virtual void FixedUpdate(float fixedTick) {};
	virtual void Update(float tick) {};
	virtual void LateUpdate(float tick) {};
	std::string ToString() const override;

	template<typename T>
	T* AddComponent()
	{
		T* component = new T();
		_components.push_back(component);
		component->SetOwner(this);
		_componentIds[component->GetId()] = _components.size();

		std::ranges::sort(_components, [&](Component* a, Component* b)
		{
			return a->ID() < b->ID();
		});

		foreach(iota(0, static_cast<int>(_components.size())), [&](int i)
		{
			_componentIds[_components[i]->GetId()] = i;
		});

		component->Initialize();

		return component;
	}

	template<typename T, typename... Args>
	T* AddComponent(Args&&... args)
	{
		T* component = new T(std::forward<Args>(args)...);
		_components.push_back(component);
		component->SetOwner(this);
		_componentIds[component->ID()] = _components.size();
		std::ranges::sort(_components, [&](Component* a, Component* b)
		{
			return a->ID() < b->ID();
		});

		foreach(iota(0, static_cast<int>(_components.size())), [&](int i)
		{
			_componentIds[_components[i]->ID()] = i;
		});

		component->Initialize();

		return component;
	}

	template<typename T>
	T* GetComponent(uint32 id)
	{
		auto it = _componentIds.find(id);
		if (it == _componentIds.end())
			return nullptr;
		return static_cast<T*>(&_components[it->second]);
	}

	template<typename T>
	T* GetComponent()
	{
		for (auto& component : _components)
		{
			if (T* castedComponent = dynamic_cast<T*>(component))
				return castedComponent;
		}
		return nullptr;
	}

	template<typename T>
	std::vector<T*> GetComponents() {
		std::vector<T*> comps;
		for (auto& component : _components)
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
	const unsigned int m_instanceID{ GENERATE_ID };
	std::string m_name{};
	Scene* m_pScene{};
	Transform m_transform{};
	std::unordered_map<std::type_index, size_t> m_componentIds{};
	std::vector<Component*> m_components{};
};

