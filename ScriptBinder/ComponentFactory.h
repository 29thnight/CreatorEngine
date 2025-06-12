#pragma once
#include "Core.Minimal.h"
#include "ReflectionYml.h"

class GameObject;
class ComponentFactory : public Singleton<ComponentFactory>
{
private:
	friend class Singleton;
	ComponentFactory() = default;
	~ComponentFactory() = default;

public:
	void Initialize();
	void LoadComponent(GameObject* obj, const MetaYml::detail::iterator_value& itNode, bool isEditorToGame = false);

	std::unordered_map<std::string, const Meta::Type*> m_componentTypes{};
};

static auto& ComponentFactorys = ComponentFactory::GetInstance();