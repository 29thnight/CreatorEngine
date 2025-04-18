#pragma once
#include "Core.Minimal.h"

class GameObject;
class ComponentFactory : public Singleton<ComponentFactory>
{
private:
	friend class Singleton;
	ComponentFactory() = default;
	~ComponentFactory() = default;

public:
	void LoadComponent(GameObject* obj, const MetaYml::detail::iterator_value& itNode);
};

static auto& ComponentFactorys = ComponentFactory::GetInstance();