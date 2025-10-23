#pragma once
#include "Core.Minimal.h"
#include "DLLAcrossSingleton.h"
#include "ReflectionYml.h"

class GameObject;
class ComponentFactory : public DLLCore::Singleton<ComponentFactory>
{
private:
	friend class Singleton;
	ComponentFactory() = default;
	~ComponentFactory() = default;

public:
	void Initialize();
        void LoadComponent(GameObject* obj, const MetaYml::Node& itNode, bool isEditorToGame = false);

	std::map<std::string, const Meta::Type*> m_componentTypes{};
};

static auto ComponentFactorys = ComponentFactory::GetInstance();