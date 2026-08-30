#pragma once
#include "Core.Minimal.h"
#include "ClassProperty.h"
#include "AuthoringNodeView.h" // D3-a-5: 완료 기준 9가 명시한 표면

class Entity;
class ComponentFactory : public Singleton<ComponentFactory>
{
private:
	friend class Singleton;
	ComponentFactory() = default;
	~ComponentFactory() = default;

public:
	void Initialize();
	void LoadComponent(Entity* obj, const Authoring::NodeView& itNode, bool isEditorToGame = false);

	std::map<std::string, const Meta::Type*> m_componentTypes{};
};

static auto ComponentFactorys = ComponentFactory::GetInstance();