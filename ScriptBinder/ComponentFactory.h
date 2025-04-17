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
	//void LoadComponent(const MetaYml::iterator_value& itNode, GameObject* obj
};