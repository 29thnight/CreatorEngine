#pragma once
#include "Utility_Framework/Core.Definition.h"
#include <string>
#include <functional>
#include <stdexcept>
#include <memory>
#include <unordered_map>
#include "RenderComponent.h"
#include "BoxColliderComponent.h"
#include "RigidbodyComponent.h"
#include "Utility_Framework/Logger.h"

template<typename T>
concept ComponentType = std::is_base_of<Component, T>::value;

class ComponentFactory : public Singleton<ComponentFactory>
{
	friend class Singleton;
public:
	using CreateFunc = std::function<Component* ()>;

	void RegisterComponent(const std::string& type, CreateFunc creator) 
	{
		if (_creators.find(type) != _creators.end())
		{
			throw std::runtime_error("Component type already registered");
		}
		_creators[type] = creator;
	}

	template<ComponentType T, typename... Args>
	void RegisterComponentArgs(const std::string& type, Args&... args) 
	{
		RegisterComponent(type, [&args...]() -> T*
		{ 
			return new T(args...);
		});
	}

	Component* CreateComponent(const std::string& type) {
		auto it = _creators.find(type);
		if (it == _creators.end())
		{
			throw std::runtime_error("Component type not registered"+type);
		}
		return it->second();
	}

	void Clear() { _creators.clear(); }

private:
	std::unordered_map<std::string, CreateFunc> _creators;


	ComponentFactory() = default;
	~ComponentFactory() = default;

};

template<ComponentType T>
inline void RegisterComponent()
{
	ComponentFactory::GetInstance()->RegisterComponent(MetaType<T>::type.data(), []() -> T*
	{
		return new T();
	});
}


template<ComponentType T, typename... Args>
inline void RegisterComponent(Args&... args)
{
	ComponentFactory::GetInstance()->RegisterComponentArgs<T>(MetaType<T>::type.data(), args...);
}

inline void UnregisterAllComponent()
{
	ComponentFactory::GetInstance()->Clear();
}