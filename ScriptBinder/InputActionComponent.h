#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IUpdatable.h"
#include "IOnDestroy.h"
#include "InputActionComponent.generated.h"
#include "ActionMap.h"
class ActionMap;
class InputActionComponent : public Component, public IUpdatable, public IOnDestroy
{
public:
   ReflectInputActionComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(InputActionComponent)

	void Update(float tick) override;
	void OnDestroy() override;

	ActionMap* AddActionMap(std::string name);
	void DeleteActionMap(std::string name);
	ActionMap* FindActionMap(std::string name);

	[[Property]]
	std::vector<ActionMap*> m_actionMaps;
};

